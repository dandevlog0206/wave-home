#include "app/api_controller.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "app/app_state.h"
#include "core/locale.h"

WAVE_NAMESPACE_BEGIN

namespace
{
	drogon::HttpResponsePtr jsonResponse(const nlohmann::json& body, drogon::HttpStatusCode code = drogon::k200OK)
	{
		auto resp = drogon::HttpResponse::newHttpResponse();
		resp->setStatusCode(code);
		resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
		resp->setBody(body.dump());
		return resp;
	}

	drogon::HttpResponsePtr errorResponse(
		const std::string& locale_tag,
		const std::string& code,
		const std::string_view message_key,
		drogon::HttpStatusCode status = drogon::k400BadRequest)
	{
		return jsonResponse(
			{{"error",
				{{"code", code},
					{"message", core::locale::text(locale_tag, message_key)}}}},
			status);
	}

	std::string requestLocale(const drogon::HttpRequestPtr& req)
	{
		if (req)
		{
			const auto explicit_locale = req->getHeader("X-Wave-Locale");
			if (!explicit_locale.empty())
				return core::locale::resolveTag(explicit_locale);
			const auto accept_language = req->getHeader("Accept-Language");
			if (!accept_language.empty())
				return core::locale::resolveTag(accept_language);
		}
		return "en-US";
	}

	std::string radarValue(const std::string& locale_tag, const RadarState& radar)
	{
		if (radar.connected && radar.status == "ok")
			return core::locale::text(locale_tag, core::locale::key::kRadarStatusOk);
		if (radar.status == "connecting")
			return core::locale::text(locale_tag, core::locale::key::kRadarStatusConnecting);
		return core::locale::text(locale_tag, core::locale::key::kRadarStatusOffline);
	}

	std::string radarDetail(const std::string& locale_tag, const RadarState& radar)
	{
		if (radar.reconnectCountdownSec > 0)
		{
			return core::locale::format(
				locale_tag,
				core::locale::key::kRadarDetailConnectingCountdown,
				{{"seconds", std::to_string(radar.reconnectCountdownSec)}});
		}

		if (!radar.detail.empty())
			return core::locale::text(locale_tag, radar.detail);
		return core::locale::text(locale_tag, core::locale::key::kRadarDetailDisconnected);
	}

	bool gestureBound(uint32_t class_id)
	{
		for (const auto& b : AppState::instance().bindings())
			if (b.gestureClassId == class_id)
				return true;
		return false;
	}

	std::string triggerModeName(const GestureTriggerMode mode)
	{
		return std::string(gestureTriggerModeName(mode));
	}
}

void ApiController::dashboardSummary(const drogon::HttpRequestPtr& req, HTTPCallback&& callback)
{
	const auto locale_tag = requestLocale(req);
	const auto radar = AppState::instance().radarSnapshot();
	const auto& repo = AppState::instance().gestures();
	const auto* active = repo.findSet(repo.activeSetId());

	int bound_devices = 0;
	for (const auto& b : AppState::instance().bindings())
		(void)b;
	bound_devices = static_cast<int>(AppState::instance().bindings().size());

	nlohmann::json body = {
		{"radar",
			{{"status", radarValue(locale_tag, radar)},
				{"detail", radarDetail(locale_tag, radar)},
				{"connected", radar.connected},
				{"lastPacketAt", radar.lastPacketAt},
				{"frameRateHz", radar.frameRateHz},
				{"targetCount", radar.targetCount},
				{"reconnectCountdownSec", radar.reconnectCountdownSec}}},
		{"todayRecognitionCount", AppState::instance().todayRecognitionCount()},
		{"iot",
			{{"connectedActive", bound_devices},
				{"total", static_cast<int>(AppState::instance().applianceManager().applianceCount())}}},
		{"activeGestureSet",
			{{"id", repo.activeSetId()},
				{"name", active ? active->name : repo.activeSetId()},
				{"gestureCount", active ? active->gestureClassIds.size() : 0}}}};

	callback(jsonResponse(body));
}

void ApiController::gestureHistory(const drogon::HttpRequestPtr& req, HTTPCallback&& callback)
{
	const size_t limit = req->getOptionalParameter<size_t>("limit").value_or(50);
	const std::string since = req->getOptionalParameter<std::string>("since").value_or("");

	nlohmann::json items = nlohmann::json::array();
	for (const auto& ev : AppState::instance().historySince(since, limit))
	{
		items.push_back({
			{"id", ev.id},
			{"gestureClassId", ev.gestureClassId},
			{"gestureName", ev.gestureName},
			{"deviceId", ev.deviceId},
			{"deviceName", ev.deviceName},
			{"actionLabel", ev.actionLabel},
			{"triggeredAt", ev.triggeredAt},
			{"confidence", ev.confidence},
			{"source", ev.source},
		});
	}

	callback(jsonResponse({{"items", items}, {"hasMore", false}}));
}

void ApiController::gestureSets(const drogon::HttpRequestPtr&, HTTPCallback&& callback)
{
	const auto& repo = AppState::instance().gestures();
	nlohmann::json items = nlohmann::json::array();
	for (const auto& id : repo.setIds())
	{
		const auto* set = repo.findSet(id);
		if (!set)
			continue;
		items.push_back({
			{"id", id},
			{"name", set->name},
			{"description", set->description},
			{"gestureClassIds", set->gestureClassIds},
			{"gestureCount", set->gestureClassIds.size()},
		});
	}
	callback(jsonResponse({{"activeSetId", repo.activeSetId()}, {"items", items}}));
}

void ApiController::gestureSetDetail(
	const drogon::HttpRequestPtr& req,
	HTTPCallback&& callback,
	const std::string& setId)
{
	const auto locale_tag = requestLocale(req);
	const auto* set = AppState::instance().gestures().findSet(setId);
	if (!set)
	{
		callback(errorResponse(
			locale_tag,
			"NOT_FOUND",
			core::locale::key::kErrorGestureSetNotFound,
			drogon::k404NotFound));
		return;
	}

	const bool is_active = AppState::instance().gestures().activeSetId() == setId;
	nlohmann::json gestures = nlohmann::json::array();
	for (const auto& g : set->gestures)
	{
		const std::string status =
			is_active && gestureBound(g.gestureClassId) ? "active" : "inactive";
		gestures.push_back({
			{"gestureClassId", g.gestureClassId},
			{"name", g.name},
			{"imageUrl", AppState::instance().gestures().mediaUrl(setId, g.mediaPreview)},
			{"videoUrl", ""},
			{"status", status},
		});
	}

	callback(jsonResponse({
		{"id", setId},
		{"name", set->name},
		{"description", set->description},
		{"gestures", gestures},
	}));
}

void ApiController::setActiveGestureSet(const drogon::HttpRequestPtr& req, HTTPCallback&& callback)
{
	const auto locale_tag = requestLocale(req);
	const auto json = req->getJsonObject();
	if (!json || !json->isMember("setId"))
	{
		callback(errorResponse(
			locale_tag,
			"BAD_REQUEST",
			core::locale::key::kErrorSetIdRequired));
		return;
	}

	const std::string set_id = (*json)["setId"].asString();
	if (!AppState::instance().gestures().findSet(set_id))
	{
		callback(errorResponse(
			locale_tag,
			"NOT_FOUND",
			core::locale::key::kErrorGestureSetNotFound,
			drogon::k404NotFound));
		return;
	}

	auto& state = AppState::instance();
	if (state.sensorPipelineRunning() && !state.reloadSensorActiveSet(set_id))
	{
		state.appendDevLog("error", "제스처 세트 활성화 실패: " + set_id);
		callback(errorResponse(
			locale_tag,
			"ACTIVATION_FAILED",
			core::locale::key::kErrorActivationFailed,
			drogon::k503ServiceUnavailable));
		return;
	}

	state.gestures().setActiveSetId(set_id);
	state.clearAllBindings();
	state.appendDevLog("info", "활성 제스처 세트: " + set_id);

	callback(jsonResponse({{"activeSetId", set_id}, {"bindingsCleared", true}}));
}

void ApiController::devices(const drogon::HttpRequestPtr& req, HTTPCallback&& callback)
{
	callback(jsonResponse(AppState::instance().appliancesApiJson(requestLocale(req))));
}

void ApiController::testDeviceControl(
	const drogon::HttpRequestPtr& req,
	HTTPCallback&& callback,
	const std::string& deviceId,
	const std::string& controlId)
{
	const auto locale_tag = requestLocale(req);
	auto& state = AppState::instance();
	if (!state.applianceManager().hasAppliance(deviceId))
	{
		callback(errorResponse(
			locale_tag,
			"NOT_FOUND",
			core::locale::key::kErrorDeviceNotFound,
			drogon::k404NotFound));
		return;
	}

	std::string error;
	const bool ok = state.applianceManager().executeInput(deviceId, controlId, std::nullopt, &error);
	if (!ok)
	{
		state.appendDevLog("warn", "테스트 제어 실패 · " + deviceId + " / " + controlId);
		callback(errorResponse(
			locale_tag,
			"CONTROL_FAILED",
			core::locale::key::kErrorControlFailed,
			drogon::k503ServiceUnavailable));
		return;
	}

	state.appendDevLog("info", "테스트 제어 · " + deviceId + " / " + controlId);
	callback(jsonResponse({{"ok", true}, {"deviceId", deviceId}, {"controlId", controlId}}));
}

void ApiController::bindings(const drogon::HttpRequestPtr&, HTTPCallback&& callback)
{
	nlohmann::json arr = nlohmann::json::array();
	for (const auto& b : AppState::instance().bindings())
	{
		arr.push_back({
			{"deviceId", b.deviceId},
			{"controlId", b.controlId},
			{"controlLabel", b.controlLabel},
			{"gestureClassId", b.gestureClassId},
			{"gestureName", b.gestureName},
			{"triggerMode", triggerModeName(b.triggerMode)},
			{"repeatIntervalMs", b.repeatIntervalMs},
		});
	}
	callback(jsonResponse({
		{"activeSetId", AppState::instance().gestures().activeSetId()},
		{"bindings", arr},
	}));
}

void ApiController::putBinding(const drogon::HttpRequestPtr& req, HTTPCallback&& callback)
{
	const auto json = req->getJsonObject();
	if (!json || !json->isMember("deviceId") || !json->isMember("controlId"))
	{
		callback(errorResponse(
			requestLocale(req),
			"BAD_REQUEST",
			core::locale::key::kErrorDeviceIdControlIdRequired));
		return;
	}

	const std::string device_id = (*json)["deviceId"].asString();
	const std::string control_id = (*json)["controlId"].asString();
	const std::string control_label = json->get("controlLabel", control_id).asString();
	const GestureTriggerMode trigger_mode = gestureTriggerModeFromString(
		json->get("triggerMode", "pulse").asString());
	const uint32_t repeat_interval_ms = std::max<uint32_t>(
		100u,
		json->get("repeatIntervalMs", 600).asUInt());

	uint32_t gesture_class_id = 0;
	if (json->isMember("gestureClassId") && !(*json)["gestureClassId"].isNull())
		gesture_class_id = (*json)["gestureClassId"].asUInt();

	if (!AppState::instance().setBinding(
			device_id,
			control_id,
			control_label,
			gesture_class_id,
			trigger_mode,
			repeat_interval_ms))
	{
		callback(errorResponse(
			requestLocale(req),
			"CONFLICT",
			core::locale::key::kErrorGestureAlreadyAssigned,
			drogon::k409Conflict));
		return;
	}

	callback(jsonResponse({
		{"deviceId", device_id},
		{"controlId", control_id},
		{"gestureClassId", gesture_class_id},
		{"triggerMode", triggerModeName(trigger_mode)},
		{"repeatIntervalMs", repeat_interval_ms},
	}));
}

void ApiController::deleteBinding(const drogon::HttpRequestPtr& req, HTTPCallback&& callback)
{
	const auto device_id = req->getOptionalParameter<std::string>("deviceId");
	if (!device_id)
	{
		callback(errorResponse(
			requestLocale(req),
			"BAD_REQUEST",
			core::locale::key::kErrorDeviceIdRequired));
		return;
	}
	AppState::instance().clearBindingsForDevice(*device_id);
	auto resp = drogon::HttpResponse::newHttpResponse();
	resp->setStatusCode(drogon::k204NoContent);
	callback(resp);
}

void ApiController::deleteAllBindings(const drogon::HttpRequestPtr&, HTTPCallback&& callback)
{
	AppState::instance().clearAllBindings();
	auto resp = drogon::HttpResponse::newHttpResponse();
	resp->setStatusCode(drogon::k204NoContent);
	callback(resp);
}

WAVE_NAMESPACE_END
