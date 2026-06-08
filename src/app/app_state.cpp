#include "app/app_state.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <unordered_set>

#include <drogon/drogon.h>

#include <thread>
#include <nlohmann/json.hpp>

#include "core/appliance_config_store.h"
#include "core/server_state_store.h"

WAVE_NAMESPACE_BEGIN

namespace
{
	std::optional<appliance::InputTriggerMode> applianceTriggerModeForBinding(
		const GestureTriggerMode mode)
	{
		switch (mode)
		{
		case GestureTriggerMode::Toggle:
			return appliance::InputTriggerMode::Toggle;
		case GestureTriggerMode::Repeat:
		case GestureTriggerMode::Pulse:
		default:
			return appliance::InputTriggerMode::Pulse;
		}
	}
}

AppState& AppState::instance()
{
	static AppState state;
	return state;
}

void AppState::setConfigRoot(const std::string& root)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_configRoot = root;
}

std::string AppState::gestureRoot() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_gestureRoot;
}

bool AppState::loadAppliancesConfig(std::string* error)
{
	std::string path;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		path = applianceConfigPathLocked();
	}

	try
	{
		core::ApplianceConfigStore store(path);
		auto document = store.load();
		m_applianceManager.loadFromDefinitions(std::move(document.appliances));

		std::vector<std::string> warnings;
		bool bindings_pruned = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			bindings_pruned = pruneAllBindingsLocked(&warnings);
		}
		for (const auto& warning : warnings)
			LOG_WARN << warning;

		if (bindings_pruned)
		{
			std::string persist_error;
			if (!persistServerState(&persist_error))
				LOG_WARN << "server state save failed after appliance reload: " << persist_error;
		}
		return true;
	}
	catch (const std::exception& ex)
	{
		if (error)
			*error = ex.what();
		return false;
	}
}

bool AppState::loadServerState(std::string* error)
{
	std::string path;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		path = serverStatePathLocked();
	}

	try
	{
		core::ServerStateStore store(path);
		const auto document = store.load();

		std::vector<std::string> warnings;
		bool normalized = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_serverSettings = document.settings.is_object()
				? document.settings
				: nlohmann::json::object();

			if (!document.activeSetId.empty())
			{
				if (m_gestures.findSet(document.activeSetId))
				{
					m_gestures.setActiveSetId(document.activeSetId);
				}
				else
				{
					normalized = true;
					warnings.push_back(
						"server_state: unknown activeSetId '" + document.activeSetId +
						"', using gesture registry default");
				}
			}

			m_bindings_by_set.clear();
			for (const auto& [set_id, stored_bindings] : document.bindings_by_set)
			{
				auto& bindings = m_bindings_by_set[set_id];
				bindings.reserve(stored_bindings.size());
				for (const auto& stored : stored_bindings)
				{
					BindingEntry binding;
					binding.deviceId = stored.deviceId;
					binding.controlId = stored.controlId;
					binding.controlLabel = stored.controlLabel;
					binding.gestureClassId = stored.gestureClassId;
					binding.gestureName = m_gestures.gestureName(
						set_id,
						stored.gestureClassId);
					binding.triggerMode = stored.triggerMode;
					binding.repeatIntervalMs = stored.repeatIntervalMs;
					bindings.push_back(std::move(binding));
				}
			}

			normalized = pruneAllBindingsLocked(&warnings) || normalized;
		}

		for (const auto& warning : warnings)
			LOG_WARN << warning;

		if (normalized)
		{
			std::string persist_error;
			if (!persistServerState(&persist_error))
				LOG_WARN << "server state save failed after normalization: " << persist_error;
		}
		return true;
	}
	catch (const std::exception& ex)
	{
		if (error)
			*error = ex.what();
		return false;
	}
}

void AppState::startSensorPipeline(const std::string& root)
{
	m_sensorPipeline.start(root);
	std::unordered_map<uint32_t, GestureTriggerConfig> overrides;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		overrides = bindingTriggerOverridesLocked();
	}
	syncSensorTriggerBindings(overrides);
}

void AppState::stopSensorPipeline()
{
	m_sensorPipeline.stop();
}

bool AppState::setActiveGestureSet(
	const std::string& set_id,
	bool* bindings_pruned,
	std::string* error)
{
	bool should_reload_pipeline = false;
	bool pruned = false;
	std::string previous_active_set;
	std::unordered_map<uint32_t, GestureTriggerConfig> previous_overrides;
	std::unordered_map<uint32_t, GestureTriggerConfig> next_overrides;

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_gestures.findSet(set_id))
		{
			if (error)
				*error = "gesture set not found";
			return false;
		}

		if (m_gestures.activeSetId() != set_id)
		{
			should_reload_pipeline = m_sensorPipeline.isRunning();
			previous_active_set = m_gestures.activeSetId();
			previous_overrides = bindingTriggerOverridesLocked();
			m_gestures.setActiveSetId(set_id);
		}

		pruned = pruneBindingsForSetLocked(set_id);
		next_overrides = bindingTriggerOverridesLocked();
	}

	if (should_reload_pipeline && !m_sensorPipeline.reloadActiveSet(set_id))
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_gestures.setActiveSetId(previous_active_set);
		}
		syncSensorTriggerBindings(previous_overrides);
		if (error)
			*error = "failed to reload active gesture set";
		return false;
	}

	syncSensorTriggerBindings(next_overrides);
	if (bindings_pruned)
		*bindings_pruned = pruned;

	std::string persist_error;
	if (!persistServerState(&persist_error))
		appendDevLog("warn", "server_state save failed: " + persist_error);
	return true;
}

void AppState::setGestureRoot(const std::string& root)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_gestureRoot = root;
}

bool AppState::loadRepository()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_gestures.load(m_gestureRoot);
}

void AppState::setServerStartedAt(const std::chrono::steady_clock::time_point t)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_serverStarted = t;
}

namespace
{
	constexpr size_t kProfilingHistorySize = 180;

	void appendProfilingSample(std::deque<float>& history, const float value)
	{
		history.push_back(value);
		while (history.size() > kProfilingHistorySize)
			history.pop_front();
	}
}

void AppState::setNcnnProfilingEnabled(const bool enabled)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_ncnnProfilingEnabled = enabled;
	if (!enabled)
	{
		m_combinedInferenceMs.clear();
		m_cpuPercentHistory.clear();
	}
}

bool AppState::ncnnProfilingEnabled() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_ncnnProfilingEnabled;
}

void AppState::recordInferencePipelineMs(const float pipeline_ms)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_ncnnProfilingEnabled)
		return;
	appendProfilingSample(m_combinedInferenceMs, pipeline_ms);
}

void AppState::sampleCpuForProfiling()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_ncnnProfilingEnabled)
		return;

	float percent = 0.f;
	if (!m_cpuSampler.sampleProcessCpuPercent(&percent))
		return;

	appendProfilingSample(m_cpuPercentHistory, percent);
}

void AppState::fillProfilingExtras(InferenceProfilingView& view) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	view.combinedMs.assign(m_combinedInferenceMs.begin(), m_combinedInferenceMs.end());
	view.cpuPercent.assign(m_cpuPercentHistory.begin(), m_cpuPercentHistory.end());
}

void AppState::updateRadar(const RadarState& radar)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_radar = radar;
	}
	broadcastDevUpdate();
}

void AppState::updateInference(
	const InferenceSnapshot& inference,
	const std::vector<wave::GestureGateDebug>& gates)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_inference = inference;
		m_gateDebug = gates;
	}
	broadcastDevUpdate();
}

void AppState::recordGestureTrigger(const uint32_t gesture_class_id, const float score)
{
	std::vector<BindingEntry> matched;
	std::string active_set;
	std::string gesture_name;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		active_set = m_gestures.activeSetId();
		gesture_name = m_gestures.gestureName(active_set, gesture_class_id);
		for (const auto& b : activeBindingsLocked())
		{
			if (b.gestureClassId == gesture_class_id)
				matched.push_back(b);
		}
	}

	if (matched.empty())
	{
		if (gesture_class_id == 0)
			return;
		HistoryEvent ev {};
		ev.gestureClassId = gesture_class_id;
		ev.gestureName = gesture_name;
		ev.deviceName = "—";
		ev.actionLabel = "제스처 인식";
		ev.triggeredAt = nowIsoUtc();
		ev.confidence = static_cast<int>(score * 100.f);
		ev.source = "gesture_binding";
		pushHistory(ev);
		return;
	}

	std::thread(
		[self = this,
		 bindings = std::move(matched),
		 gesture_class_id,
		 score,
		 gesture_name = std::move(gesture_name),
		 active_set = std::move(active_set)]() mutable {
			self->dispatchBindingActions(
				std::move(bindings),
				gesture_class_id,
				score,
				std::move(gesture_name),
				std::move(active_set));
		})
		.detach();
}

void AppState::dispatchBindingActions(
	std::vector<BindingEntry> bindings,
	const uint32_t gesture_class_id,
	const float score,
	std::string gesture_name,
	std::string active_set)
{
	(void)active_set;
	for (const auto& b : bindings)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			bool still_bound = false;
			for (const auto& current : activeBindingsLocked())
			{
				if (current.gestureClassId == gesture_class_id &&
					current.deviceId == b.deviceId &&
					current.controlId == b.controlId)
				{
					still_bound = true;
					break;
				}
			}
			if (!still_bound)
				continue;
		}

		std::string error;
		const bool ok = m_applianceManager.executeInput(
			b.deviceId,
			b.controlId,
			applianceTriggerModeForBinding(b.triggerMode),
			&error);
		HistoryEvent ev {};
		ev.gestureClassId = gesture_class_id;
		ev.gestureName = gesture_name;
		ev.deviceId = b.deviceId;
		ev.deviceName = m_applianceManager.applianceName(b.deviceId);
		ev.actionLabel = b.controlLabel + (ok ? "" : " (제어 실패)");
		ev.triggeredAt = nowIsoUtc();
		ev.confidence = static_cast<int>(score * 100.f);
		ev.source = "gesture_binding";
		pushHistory(ev);

		appendDevLog(
			ok ? "info" : "warn",
			(ok ? "IoT 제어 · " : "IoT 제어 실패 · ") + ev.deviceName + " / " + b.controlLabel);
	}
}

RadarState AppState::radarSnapshot() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_radar;
}

InferenceSnapshot AppState::inferenceSnapshot() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_inference;
}

std::vector<wave::GestureGateDebug> AppState::gateSnapshot() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_gateDebug;
}

uint32_t AppState::todayRecognitionCount() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_todayCount;
}

int64_t AppState::serverUptimeSeconds() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_serverStarted.time_since_epoch().count() == 0)
		return 0;
	const auto elapsed = std::chrono::steady_clock::now() - m_serverStarted;
	return std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
}

std::vector<HistoryEvent> AppState::historySince(
	const std::string& since_iso,
	const size_t limit) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	std::vector<HistoryEvent> out;
	for (const auto& ev : m_history)
	{
		if (!since_iso.empty() && ev.triggeredAt <= since_iso)
			continue;
		out.push_back(ev);
		if (out.size() >= limit)
			break;
	}
	return out;
}

std::vector<BindingEntry>& AppState::activeBindingsLocked()
{
	return m_bindings_by_set[m_gestures.activeSetId()];
}

const std::vector<BindingEntry>& AppState::activeBindingsLocked() const
{
	const auto it = m_bindings_by_set.find(m_gestures.activeSetId());
	if (it == m_bindings_by_set.end())
	{
		static const std::vector<BindingEntry> kEmpty;
		return kEmpty;
	}
	return it->second;
}

std::vector<BindingEntry> AppState::bindings() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return activeBindingsLocked();
}

bool AppState::setBinding(
	const std::string& device_id,
	const std::string& control_id,
	const std::string& control_label,
	const std::optional<uint32_t> gesture_class_id,
	const GestureTriggerMode trigger_mode,
	const uint32_t repeat_interval_ms)
{
	std::unordered_map<uint32_t, GestureTriggerConfig> overrides;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto& bindings = activeBindingsLocked();

		if (gesture_class_id.has_value())
		{
			bindings.erase(
				std::remove_if(
					bindings.begin(),
					bindings.end(),
					[&](const BindingEntry& b) {
						return b.gestureClassId == *gesture_class_id &&
							(b.deviceId != device_id || b.controlId != control_id);
					}),
				bindings.end());
		}

		bindings.erase(
			std::remove_if(
				bindings.begin(),
				bindings.end(),
				[&](const BindingEntry& b) {
					return b.deviceId == device_id && b.controlId == control_id;
				}),
			bindings.end());

		if (gesture_class_id.has_value())
		{
			BindingEntry entry {};
			entry.deviceId = device_id;
			entry.controlId = control_id;
			entry.controlLabel = control_label;
			entry.gestureClassId = *gesture_class_id;
			entry.gestureName =
				m_gestures.gestureName(m_gestures.activeSetId(), *gesture_class_id);
			entry.triggerMode = trigger_mode;
			entry.repeatIntervalMs = std::max<uint32_t>(100, repeat_interval_ms);
			bindings.push_back(entry);
		}
		overrides = bindingTriggerOverridesLocked();
	}
	syncSensorTriggerBindings(overrides);
	schedulePersistServerState();
	return true;
}

void AppState::clearBindingsForDevice(const std::string& device_id)
{
	std::unordered_map<uint32_t, GestureTriggerConfig> overrides;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto& bindings = activeBindingsLocked();
		bindings.erase(
			std::remove_if(
				bindings.begin(),
				bindings.end(),
				[&](const BindingEntry& b) { return b.deviceId == device_id; }),
			bindings.end());
		overrides = bindingTriggerOverridesLocked();
	}
	syncSensorTriggerBindings(overrides);
	schedulePersistServerState();
}

void AppState::clearAllBindings()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		activeBindingsLocked().clear();
	}
	syncSensorTriggerBindings({});
	schedulePersistServerState();
}

bool AppState::sensorPipelineRunning() const
{
	return m_sensorPipeline.isRunning();
}

nlohmann::json AppState::appliancesApiJson(const std::string_view locale_tag) const
{
	auto body = m_applianceManager.appliancesJson(locale_tag);
	const auto binding_list = bindings();
	for (auto& item : body["items"])
	{
		const std::string device_id = item["id"];
		bool has = false;
		for (const auto& b : binding_list)
		{
			if (b.deviceId == device_id)
			{
				has = true;
				break;
			}
		}
		item["hasActiveBindings"] = has;
	}
	body["activeGestureSetId"] = gestures().activeSetId();
	return body;
}

std::string AppState::applianceConfigPathLocked() const
{
	const std::filesystem::path root = m_configRoot.empty()
		? std::filesystem::path("config")
		: std::filesystem::path(m_configRoot);
	return (root / "appliances.json").string();
}

std::string AppState::serverStatePathLocked() const
{
	const std::filesystem::path root = m_configRoot.empty()
		? std::filesystem::path("config")
		: std::filesystem::path(m_configRoot);
	return (root / "server_state.json").string();
}

void AppState::schedulePersistServerState() const
{
	std::thread([this] {
		std::string persist_error;
		if (!persistServerState(&persist_error) && !persist_error.empty())
			const_cast<AppState*>(this)->appendDevLog(
				"warn",
				"server_state save failed: " + persist_error);
	}).detach();
}

bool AppState::persistServerState(std::string* error) const
{
	core::ServerStateDocument document;
	std::string path;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		path = serverStatePathLocked();
		document.activeSetId = m_gestures.activeSetId();
		document.settings = m_serverSettings;
		for (const auto& [set_id, bindings] : m_bindings_by_set)
		{
			auto& stored_bindings = document.bindings_by_set[set_id];
			stored_bindings.reserve(bindings.size());
			for (const auto& binding : bindings)
			{
				core::StoredBindingEntry stored;
				stored.deviceId = binding.deviceId;
				stored.controlId = binding.controlId;
				stored.controlLabel = binding.controlLabel;
				stored.gestureClassId = binding.gestureClassId;
				stored.triggerMode = binding.triggerMode;
				stored.repeatIntervalMs = binding.repeatIntervalMs;
				stored_bindings.push_back(std::move(stored));
			}
		}
	}

	try
	{
		core::ServerStateStore(path).save(document);
		return true;
	}
	catch (const std::exception& ex)
	{
		if (error)
			*error = ex.what();
		return false;
	}
}

bool AppState::bindingSupportedLocked(
	const std::string& set_id,
	const BindingEntry& binding) const
{
	if (binding.deviceId.empty() || binding.controlId.empty())
		return false;

	const auto* gesture_set = m_gestures.findSet(set_id);
	if (!gesture_set)
		return false;
	if (gesture_set->triggersByClassId.find(binding.gestureClassId) ==
		gesture_set->triggersByClassId.end())
	{
		return false;
	}
	if (!m_applianceManager.hasAppliance(binding.deviceId))
		return false;
	return m_applianceManager.hasInput(binding.deviceId, binding.controlId);
}

bool AppState::pruneBindingsForSetLocked(
	const std::string& set_id,
	std::vector<std::string>* warnings)
{
	auto& bindings = m_bindings_by_set[set_id];
	std::vector<BindingEntry> filtered;
	filtered.reserve(bindings.size());
	std::unordered_set<std::string> control_keys;
	std::unordered_set<uint32_t> gesture_ids;
	bool changed = false;

	for (auto binding : bindings)
	{
		if (!bindingSupportedLocked(set_id, binding))
		{
			changed = true;
			if (warnings)
			{
				warnings->push_back(
					"server_state: dropped invalid binding [" + set_id + "] '" +
					binding.deviceId + "/" + binding.controlId + "'");
			}
			continue;
		}

		const std::string control_key = binding.deviceId + "\n" + binding.controlId;
		if (!control_keys.insert(control_key).second)
		{
			changed = true;
			if (warnings)
			{
				warnings->push_back(
					"server_state: dropped duplicate control binding [" + set_id + "] '" +
					binding.deviceId + "/" + binding.controlId + "'");
			}
			continue;
		}

		if (!gesture_ids.insert(binding.gestureClassId).second)
		{
			changed = true;
			if (warnings)
			{
				warnings->push_back(
					"server_state: dropped duplicate gesture binding [" + set_id + "] '" +
					std::to_string(binding.gestureClassId) + "'");
			}
			continue;
		}

		const std::string gesture_name = m_gestures.gestureName(
			set_id,
			binding.gestureClassId);
		if (binding.gestureName != gesture_name)
		{
			binding.gestureName = gesture_name;
			changed = true;
		}
		if (binding.controlLabel.empty())
		{
			binding.controlLabel = m_applianceManager.inputLabel(
				binding.deviceId,
				binding.controlId,
				"en-US");
			changed = true;
		}

		filtered.push_back(std::move(binding));
	}

	if (changed)
		bindings = std::move(filtered);
	return changed;
}

bool AppState::pruneAllBindingsLocked(std::vector<std::string>* warnings)
{
	bool changed = false;
	std::vector<std::string> set_ids;
	set_ids.reserve(m_bindings_by_set.size());
	for (const auto& [set_id, _] : m_bindings_by_set)
		set_ids.push_back(set_id);
	for (const auto& set_id : set_ids)
		changed = pruneBindingsForSetLocked(set_id, warnings) || changed;
	return changed;
}

std::unordered_map<uint32_t, GestureTriggerConfig> AppState::bindingTriggerOverridesLocked() const
{
	std::unordered_map<uint32_t, GestureTriggerConfig> overrides;
	const auto* active_set = m_gestures.findSet(m_gestures.activeSetId());
	for (const auto& binding : activeBindingsLocked())
	{
		GestureTriggerConfig config {};
		if (active_set)
		{
			if (const auto it = active_set->triggersByClassId.find(binding.gestureClassId);
				it != active_set->triggersByClassId.end())
			{
				config = it->second;
			}
		}
		config.mode = binding.triggerMode;
		config.repeatIntervalMs = binding.repeatIntervalMs;
		overrides[binding.gestureClassId] = config;
	}
	return overrides;
}

void AppState::syncSensorTriggerBindings(
	const std::unordered_map<uint32_t, GestureTriggerConfig>& overrides)
{
	if (m_sensorPipeline.isRunning())
		m_sensorPipeline.reloadTriggerBindings(overrides);
}

void AppState::appendDevLog(const std::string& level, const std::string& message)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_devLogs.push_back({nowIsoUtc(), level, message});
		while (m_devLogs.size() > 300)
			m_devLogs.pop_front();
	}
	broadcastDevUpdate();
}

void AppState::registerDevSocket(const drogon::WebSocketConnectionPtr& conn)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (std::find(m_devSockets.begin(), m_devSockets.end(), conn) == m_devSockets.end())
		m_devSockets.push_back(conn);
}

void AppState::unregisterDevSocket(const drogon::WebSocketConnectionPtr& conn)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_devSockets.erase(
		std::remove(m_devSockets.begin(), m_devSockets.end(), conn),
		m_devSockets.end());
}

void AppState::broadcastDevUpdate()
{
	const std::string payload = buildDevJson();
	std::vector<drogon::WebSocketConnectionPtr> sockets;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		sockets = m_devSockets;
	}
	for (const auto& conn : sockets)
	{
		if (conn && conn->connected())
			conn->send(payload);
	}
}

std::string AppState::buildDevJson() const
{
	RadarState radar;
	InferenceSnapshot inf;
	std::vector<wave::GestureGateDebug> gates;
	std::deque<DevLogLine> logs;
	int64_t uptime = 0;
	std::string active_set;
	size_t binding_count = 0;
	uint32_t today_gesture_count = 0;
	bool ncnn_profiling = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		radar = m_radar;
		inf = m_inference;
		gates = m_gateDebug;
		logs = m_devLogs;
		binding_count = activeBindingsLocked().size();
		today_gesture_count = m_todayCount;
		ncnn_profiling = m_ncnnProfilingEnabled;
		if (m_serverStarted.time_since_epoch().count() != 0)
		{
			uptime = std::chrono::duration_cast<std::chrono::seconds>(
				std::chrono::steady_clock::now() - m_serverStarted).count();
		}
		active_set = m_gestures.activeSetId();
	}

	nlohmann::json j;
	j["type"] = "dev_snapshot";
	j["serverUptimeSec"] = uptime;
	j["activeSetId"] = active_set;
	j["devMeta"] = {
		{"bindingCount", binding_count},
		{"ncnnProfiling", ncnn_profiling},
		{"todayGestureCount", today_gesture_count},
	};
	j["radar"] = {
		{"connected", radar.connected},
		{"status", radar.status},
		{"detail", radar.detail},
		{"ip", radar.ip},
		{"mac", radar.mac},
		{"model", radar.model},
		{"frameRateHz", radar.frameRateHz},
		{"targetCount", radar.targetCount},
		{"lastPacketAt", radar.lastPacketAt},
		{"reconnectCountdownSec", radar.reconnectCountdownSec},
	};
	j["probabilities"] = inf.probabilities;
	if (const auto* set = AppState::instance().gestures().findSet(active_set))
	{
		nlohmann::json labels = nlohmann::json::object();
		for (const auto& [id, name] : set->classLabels)
			labels[std::to_string(id)] = name;
		j["classLabels"] = labels;
	}
	j["embedding"] = {
		{"values", inf.embeddingMap},
		{"embedDim", inf.embedDim},
		{"sequenceLength", inf.sequenceLength},
		{"ready", inf.sequenceReady},
	};
	if (inf.profiling.enabled)
	{
		InferenceProfilingView profile_view = inf.profiling;
		fillProfilingExtras(profile_view);
		j["inferenceProfile"] = {
			{"enabled", true},
			{"frameEncoder", {
				{"name", profile_view.frameEncoderName},
				{"samplesMs", profile_view.frameEncoderMs},
			}},
			{"temporalAggregator", {
				{"architecture", profile_view.temporalAggregatorArchitecture},
				{"samplesMs", profile_view.temporalAggregatorMs},
			}},
			{"combined", {{"samplesMs", profile_view.combinedMs}}},
			{"cpu", {{"samplesPercent", profile_view.cpuPercent}}},
		};
	}
	else
	{
		j["inferenceProfile"] = {{"enabled", false}};
	}
	j["logs"] = nlohmann::json::array();
	for (const auto& line : logs)
	{
		j["logs"].push_back({
			{"at", line.at},
			{"level", line.level},
			{"message", line.message},
		});
	}
	j["channels"] = nlohmann::json::array();
	for (const auto& ch : gates)
	{
		j["channels"].push_back({
			{"gestureClassId", ch.gestureClassId},
			{"score", ch.score},
			{"state", ch.state},
			{"triggerMode", ch.triggerMode},
			{"highThreshold", ch.highThreshold},
			{"lowThreshold", ch.lowThreshold},
			{"cooldownMs", ch.cooldownMs},
			{"minHighHoldMs", ch.minHighHoldMs},
			{"minLowHoldMs", ch.minLowHoldMs},
			{"repeatIntervalMs", ch.repeatIntervalMs},
			{"holdProgressMs", ch.holdProgressMs},
			{"holdRequiredMs", ch.holdRequiredMs},
			{"ready", ch.ready},
		});
	}
	return j.dump();
}

void AppState::pushHistory(const HistoryEvent& ev)
{
	HistoryEvent copy = ev;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const std::string day_key = copy.triggeredAt.substr(0, 10);
		if (m_todayKey != day_key)
		{
			m_todayKey = day_key;
			m_todayCount = 0;
		}
		copy.id = m_nextHistoryId++;
		m_history.push_front(copy);
		while (m_history.size() > 200)
			m_history.pop_back();
		++m_todayCount;
	}
}

std::string AppState::nowIsoUtc() const
{
	const auto now = std::chrono::system_clock::now();
	const auto t = std::chrono::system_clock::to_time_t(now);
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
						now.time_since_epoch()) %
					1000;

	std::tm tm {};
	gmtime_r(&t, &tm);

	std::ostringstream oss;
	oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S")
		<< '.' << std::setw(3) << std::setfill('0') << ms.count() << 'Z';
	return oss.str();
}

WAVE_NAMESPACE_END
