#pragma once

#include "device/gesture_output_stabilizer.h"
#include "gesture_repository.h"

#include <drogon/WebSocketConnection.h>

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

struct HistoryEvent
{
	uint64_t id = 0;
	uint32_t gestureClassId = 0;
	std::string gestureName;
	std::string deviceId;
	std::string deviceName;
	std::string actionLabel;
	std::string triggeredAt;
	int confidence = 0;
	std::string source = "gesture_binding";
};

struct BindingEntry
{
	std::string deviceId;
	std::string controlId;
	std::string controlLabel;
	uint32_t gestureClassId = 0;
	std::string gestureName;
};

struct RadarState
{
	bool connected = false;
	std::string status = "offline";
	std::string detail = "센서 미연결";
	std::string lastPacketAt;
	double frameRateHz = 0.0;
	uint32_t targetCount = 0;
	std::string ip;
	std::string mac;
	std::string model;
};

struct InferenceSnapshot
{
	std::vector<float> probabilities;
	std::vector<float> embeddingMap;
	uint32_t embedDim = 0;
	uint32_t sequenceLength = 0;
	bool sequenceReady = false;
};

class AppState
{
public:
	static AppState& instance();

	void setGestureRoot(const std::string& root);
	bool loadRepository();

	void setServerStartedAt(std::chrono::steady_clock::time_point t);

	void updateRadar(const RadarState& radar);
	void updateInference(const InferenceSnapshot& inference, const std::vector<wave::GestureChannelDebug>& channels);

	void recordGestureTrigger(uint32_t gesture_class_id, float score);

	std::string gestureRoot() const;
	GestureRepository& gestures() { return m_gestures; }
	const GestureRepository& gestures() const { return m_gestures; }

	void startSensorPipeline(const std::string& root) { m_sensorPipeline.start(root); }
	void stopSensorPipeline() { m_sensorPipeline.stop(); }
	bool reloadSensorActiveSet(const std::string& set_id);

	RadarState radarSnapshot() const;
	InferenceSnapshot inferenceSnapshot() const;
	std::vector<wave::GestureChannelDebug> channelSnapshot() const;

	uint32_t todayRecognitionCount() const;
	int64_t serverUptimeSeconds() const;

	std::vector<HistoryEvent> historySince(const std::string& since_iso, size_t limit) const;

	std::vector<BindingEntry> bindings() const;
	bool setBinding(const std::string& device_id, const std::string& control_id,
		const std::string& control_label, uint32_t gesture_class_id);
	void clearBindingsForDevice(const std::string& device_id);
	void clearAllBindings();

	void registerDevSocket(const drogon::WebSocketConnectionPtr& conn);
	void unregisterDevSocket(const drogon::WebSocketConnectionPtr& conn);
	void broadcastDevUpdate();

	std::string buildDevJson() const;

private:
	AppState() = default;

	void pushHistory(const HistoryEvent& ev);
	std::string nowIsoUtc() const;

	mutable std::mutex m_mutex;
	std::string m_gestureRoot;
	GestureRepository m_gestures;

	RadarState m_radar;
	InferenceSnapshot m_inference;
	SensorPipeline m_sensorPipeline;
	std::vector<wave::GestureChannelDebug> m_channels;

	std::chrono::steady_clock::time_point m_serverStarted {};
	std::deque<HistoryEvent> m_history;
	uint64_t m_nextHistoryId = 1;
	uint32_t m_todayCount = 0;
	std::string m_todayKey;

	std::vector<BindingEntry> m_bindings;

	std::vector<drogon::WebSocketConnectionPtr> m_devSockets;
};
