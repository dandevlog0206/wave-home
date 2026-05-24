#include "app_state.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>

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

bool AppState::reloadSensorActiveSet(const std::string& set_id)
{
	return m_sensorPipeline.reloadActiveSet(set_id);
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
	const std::vector<wave::GestureChannelDebug>& channels)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_inference = inference;
		m_channels = channels;
	}
	broadcastDevUpdate();
}

void AppState::recordGestureTrigger(const uint32_t gesture_class_id, const float score)
{
	if (gesture_class_id == 0)
		return;

	std::string active_set;
	std::string gesture_name;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		active_set = m_gestures.activeSetId();
		gesture_name = m_gestures.gestureName(active_set, gesture_class_id);
	}

	HistoryEvent ev {};
	ev.id = 0;
	ev.gestureClassId = gesture_class_id;
	ev.gestureName = gesture_name;
	ev.deviceName = "—";
	ev.actionLabel = "제스처 인식";
	ev.triggeredAt = nowIsoUtc();
	ev.confidence = static_cast<int>(score * 100.f);
	ev.source = "gesture_binding";

	pushHistory(ev);
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

std::vector<wave::GestureChannelDebug> AppState::channelSnapshot() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_channels;
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
	const uint32_t gesture_class_id)
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
		m_bindings.push_back(entry);
	}
	return true;
}

void AppState::clearBindingsForDevice(const std::string& device_id)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_bindings.erase(
		std::remove_if(
			m_bindings.begin(),
			m_bindings.end(),
			[&](const BindingEntry& b) { return b.deviceId == device_id; }),
		m_bindings.end());
}

void AppState::clearAllBindings()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_bindings.clear();
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
	std::vector<wave::GestureChannelDebug> channels;
	int64_t uptime = 0;
	std::string active_set;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		radar = m_radar;
		inf = m_inference;
		channels = m_channels;
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
	j["channels"] = nlohmann::json::array();
	for (const auto& ch : channels)
	{
		j["channels"].push_back({
			{"gestureClassId", ch.gestureClassId},
			{"score", ch.score},
			{"state", ch.state},
			{"mode", ch.mode},
			{"highThreshold", ch.highThreshold},
			{"lowThreshold", ch.lowThreshold},
			{"cooldownMs", ch.cooldownMs},
			{"minHighHoldMs", ch.minHighHoldMs},
			{"minLowHoldMs", ch.minLowHoldMs},
			{"holdProgressMs", ch.holdProgressMs},
			{"holdRequiredMs", ch.holdRequiredMs},
			{"toggleOutput", ch.toggleOutput},
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
