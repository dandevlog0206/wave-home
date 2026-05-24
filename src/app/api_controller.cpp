#include "api_controller.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include "app_state.h"
#include "pipeline_service.h"


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
		const std::string& code,
		const std::string& message,
		drogon::HttpStatusCode status = drogon::k400BadRequest)
	{
		return jsonResponse({{"error", {{"code", code}, {"message", message}}}}, status);
	}

	nlohmann::json dummyDevicesJson()
	{
		return {
			{"items",
				{
					{{"id", "light-living"},
						{"name", "조명"},
						{"room", "거실"},
						{"state", "켜짐"},
						{"connection", "online"},
						{"controls", {{{"id", "power"}, {"label", "전원 on/off"}}}},
						{"hasActiveBindings", false}},
					{{"id", "tv-living"},
						{"name", "TV"},
						{"room", "거실"},
						{"state", "켜짐"},
						{"connection", "online"},
						{"controls",
							{
								{{"id", "power"}, {"label", "전원 on/off"}},
								{{"id", "volume_up"}, {"label", "볼륨 up"}},
								{{"id", "volume_down"}, {"label", "볼륨 down"}},
								{{"id", "channel_up"}, {"label", "채널 up"}},
								{{"id", "channel_down"}, {"label", "채널 down"}},
							}},
						{"hasActiveBindings", false}},
					{{"id", "ac-bedroom"},
						{"name", "에어컨"},
						{"room", "침실"},
						{"state", "켜짐"},
						{"connection", "online"},
						{"controls", {{{"id", "power"}, {"label", "전원 on/off"}}}},
						{"hasActiveBindings", false}},
					{{"id", "doorlock-entrance"},
						{"name", "도어락"},
						{"room", "현관"},
						{"state", "꺼짐"},
						{"connection", "online"},
						{"controls",
							{
								{{"id", "lock"}, {"label", "잠금"}},
								{{"id", "unlock"}, {"label", "잠금 해제"}},
							}},
						{"hasActiveBindings", false}},
				}},
			{"activeGestureSetId", AppState::instance().gestures().activeSetId()}};
	}

	bool gestureBound(uint32_t class_id)
	{
		for (const auto& b : AppState::instance().bindings())
			if (b.gestureClassId == class_id)
				return true;
		return false;
	}
}

void ApiController::dashboardSummary(const drogon::HttpRequestPtr&, HTTPCallback&& callback)
{
	const auto radar = AppState::instance().radarSnapshot();
	const auto& repo = AppState::instance().gestures();
	const auto* active = repo.findSet(repo.activeSetId());

	int bound_devices = 0;
	for (const auto& b : AppState::instance().bindings())
		(void)b;
	bound_devices = static_cast<int>(AppState::instance().bindings().size());

	nlohmann::json body = {
		{"radar",
			{{"status", radar.status},
				{"detail", radar.detail},
				{"connected", radar.connected},
				{"lastPacketAt", radar.lastPacketAt},
				{"frameRateHz", radar.frameRateHz},
				{"targetCount", radar.targetCount}}},
		{"todayRecognitionCount", AppState::instance().todayRecognitionCount()},
		{"iot", {{"connectedActive", bound_devices}, {"total", 4}}},
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

void ApiController::gestureSetDetail(const drogon::HttpRequestPtr&, HTTPCallback&& callback, const std::string& setId)
{
	const auto* set = AppState::instance().gestures().findSet(setId);
	if (!set)
	{
		callback(errorResponse("NOT_FOUND", "gesture set not found", drogon::k404NotFound));
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
	const auto json = req->getJsonObject();
	if (!json || !json->isMember("setId"))
	{
		callback(errorResponse("BAD_REQUEST", "setId required"));
		return;
	}

	const std::string set_id = (*json)["setId"].asString();
	if (!AppState::instance().gestures().findSet(set_id))
	{
		callback(errorResponse("NOT_FOUND", "gesture set not found", drogon::k404NotFound));
		return;
	}

	AppState::instance().gestures().setActiveSetId(set_id);
	AppState::instance().clearAllBindings();
	AppState::instance().reloadSensorActiveSet(set_id);

	callback(jsonResponse({{"activeSetId", set_id}, {"bindingsCleared", true}}));
}

void ApiController::devices(const drogon::HttpRequestPtr&, HTTPCallback&& callback)
{
	auto body = dummyDevicesJson();
	const auto bindings = AppState::instance().bindings();
	for (auto& item : body["items"])
	{
		const std::string device_id = item["id"];
		bool has = false;
		for (const auto& b : bindings)
		{
			if (b.deviceId == device_id)
			{
				has = true;
				break;
			}
		}
		item["hasActiveBindings"] = has;
	}
	callback(jsonResponse(body));
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
		callback(errorResponse("BAD_REQUEST", "deviceId and controlId required"));
		return;
	}

	const std::string device_id = (*json)["deviceId"].asString();
	const std::string control_id = (*json)["controlId"].asString();
	const std::string control_label = json->get("controlLabel", control_id).asString();

	uint32_t gesture_class_id = 0;
	if (json->isMember("gestureClassId") && !(*json)["gestureClassId"].isNull())
		gesture_class_id = (*json)["gestureClassId"].asUInt();

	if (!AppState::instance().setBinding(device_id, control_id, control_label, gesture_class_id))
	{
		callback(errorResponse("CONFLICT", "gesture already assigned", drogon::k409Conflict));
		return;
	}

	callback(jsonResponse({
		{"deviceId", device_id},
		{"controlId", control_id},
		{"gestureClassId", gesture_class_id},
	}));
}

void ApiController::deleteBinding(const drogon::HttpRequestPtr& req, HTTPCallback&& callback)
{
	const auto device_id = req->getOptionalParameter<std::string>("deviceId");
	if (!device_id)
	{
		callback(errorResponse("BAD_REQUEST", "deviceId required"));
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
