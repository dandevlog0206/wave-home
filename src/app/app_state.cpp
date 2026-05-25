#include "app/app_state.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

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

std::string AppState::gestureRoot() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_gestureRoot;
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

bool AppState::reloadSensorActiveSet(const std::string& set_id)
{
	const bool ok = m_sensorPipeline.reloadActiveSet(set_id);
	if (ok)
	{
		std::unordered_map<uint32_t, GestureTriggerConfig> overrides;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			overrides = bindingTriggerOverridesLocked();
		}
		syncSensorTriggerBindings(overrides);
	}
	return ok;
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
	if (gesture_class_id == 0)
		return;

	std::vector<BindingEntry> matched;
	std::string active_set;
	std::string gesture_name;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		active_set = m_gestures.activeSetId();
		gesture_name = m_gestures.gestureName(active_set, gesture_class_id);
		for (const auto& b : m_bindings)
		{
			if (b.gestureClassId == gesture_class_id)
				matched.push_back(b);
		}
	}

	if (matched.empty())
	{
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

	for (const auto& b : matched)
	{
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

std::vector<BindingEntry> AppState::bindings() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_bindings;
}

bool AppState::setBinding(
	const std::string& device_id,
	const std::string& control_id,
	const std::string& control_label,
	const uint32_t gesture_class_id,
	const GestureTriggerMode trigger_mode,
	const uint32_t repeat_interval_ms)
{
	std::unordered_map<uint32_t, GestureTriggerConfig> overrides;
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		for (const auto& b : m_bindings)
		{
			if (gesture_class_id != 0 && b.gestureClassId == gesture_class_id &&
			    (b.deviceId != device_id || b.controlId != control_id))
				return false;
		}

		m_bindings.erase(
			std::remove_if(
				m_bindings.begin(),
				m_bindings.end(),
				[&](const BindingEntry& b) {
					return b.deviceId == device_id && b.controlId == control_id;
				}),
			m_bindings.end());

		if (gesture_class_id != 0)
		{
			BindingEntry entry {};
			entry.deviceId = device_id;
			entry.controlId = control_id;
			entry.controlLabel = control_label;
			entry.gestureClassId = gesture_class_id;
			entry.gestureName =
				m_gestures.gestureName(m_gestures.activeSetId(), gesture_class_id);
			entry.triggerMode = trigger_mode;
			entry.repeatIntervalMs = std::max<uint32_t>(100, repeat_interval_ms);
			m_bindings.push_back(entry);
		}
		overrides = bindingTriggerOverridesLocked();
	}
	syncSensorTriggerBindings(overrides);
	return true;
}

void AppState::clearBindingsForDevice(const std::string& device_id)
{
	std::unordered_map<uint32_t, GestureTriggerConfig> overrides;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_bindings.erase(
			std::remove_if(
				m_bindings.begin(),
				m_bindings.end(),
				[&](const BindingEntry& b) { return b.deviceId == device_id; }),
			m_bindings.end());
		overrides = bindingTriggerOverridesLocked();
	}
	syncSensorTriggerBindings(overrides);
}

void AppState::clearAllBindings()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_bindings.clear();
	}
	syncSensorTriggerBindings({});
}

bool AppState::sensorPipelineRunning() const
{
	return m_sensorPipeline.isRunning();
}

void AppState::startHomebridgeWatcher(const std::string& config_path)
{
	stopHomebridgeWatcher();
	m_homebridge_watcher = std::make_unique<wave::core::HomebridgeWatcher>(
		m_applianceManager,
		config_path);
	m_homebridge_watcher->start([this](const std::string& msg) { appendDevLog("info", msg); });
}

void AppState::stopHomebridgeWatcher()
{
	if (m_homebridge_watcher)
	{
		m_homebridge_watcher->stop();
		m_homebridge_watcher.reset();
	}
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

std::unordered_map<uint32_t, GestureTriggerConfig> AppState::bindingTriggerOverridesLocked() const
{
	std::unordered_map<uint32_t, GestureTriggerConfig> overrides;
	const auto* active_set = m_gestures.findSet(m_gestures.activeSetId());
	for (const auto& binding : m_bindings)
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
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		radar = m_radar;
		inf = m_inference;
		gates = m_gateDebug;
		logs = m_devLogs;
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
