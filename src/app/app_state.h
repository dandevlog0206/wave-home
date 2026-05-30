#pragma once

#include "appliance/appliance_manager.h"
#include "device/gesture_probability_gate.h"
#include "device/sensor_pipeline.h"
#include "gesture_repository.h"
#include "util/cpu_sampler.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <drogon/WebSocketConnection.h>
#include <nlohmann/json.hpp>

WAVE_NAMESPACE_BEGIN

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
	GestureTriggerMode triggerMode = GestureTriggerMode::Pulse;
	uint32_t repeatIntervalMs = 600;
};

struct RadarState
{
	bool connected = false;
	std::string status = "offline";
	std::string detail = "radar.detail.disconnected";
	std::string lastPacketAt;
	double frameRateHz = 0.0;
	uint32_t targetCount = 0;
	std::string ip;
	std::string mac;
	std::string model;
	uint32_t reconnectCountdownSec = 0;
};

struct InferenceProfilingView
{
	bool enabled = false;
	std::string frameEncoderName;
	std::string temporalAggregatorArchitecture;
	std::vector<float> frameEncoderMs;
	std::vector<float> temporalAggregatorMs;
	std::vector<float> combinedMs;
	std::vector<float> cpuPercent;
};

struct InferenceSnapshot
{
	std::vector<float> probabilities;
	std::vector<float> embeddingMap;
	uint32_t embedDim = 0;
	uint32_t sequenceLength = 0;
	bool sequenceReady = false;
	InferenceProfilingView profiling;
};

class AppState
{
public:
	static AppState& instance();

	void setConfigRoot(const std::string& root);
	void setGestureRoot(const std::string& root);
	bool loadAppliancesConfig(std::string* error = nullptr);
	bool loadServerState(std::string* error = nullptr);
	bool loadRepository();

	void setServerStartedAt(std::chrono::steady_clock::time_point t);
	void setNcnnProfilingEnabled(bool enabled);
	bool ncnnProfilingEnabled() const;
	void recordInferencePipelineMs(float pipeline_ms);
	void sampleCpuForProfiling();
	void fillProfilingExtras(InferenceProfilingView& view) const;

	void updateRadar(const RadarState& radar);
	void updateInference(const InferenceSnapshot& inference, const std::vector<wave::GestureGateDebug>& gates);

	void recordGestureTrigger(uint32_t gesture_class_id, float score);

	std::string gestureRoot() const;
	GestureRepository& gestures() { return m_gestures; }
	const GestureRepository& gestures() const { return m_gestures; }

	void startSensorPipeline(const std::string& root);
	void stopSensorPipeline();
	bool setActiveGestureSet(
		const std::string& set_id,
		bool* bindings_pruned = nullptr,
		std::string* error = nullptr);

	RadarState radarSnapshot() const;
	InferenceSnapshot inferenceSnapshot() const;
	std::vector<wave::GestureGateDebug> gateSnapshot() const;

	uint32_t todayRecognitionCount() const;
	int64_t serverUptimeSeconds() const;

	std::vector<HistoryEvent> historySince(const std::string& since_iso, size_t limit) const;

	std::vector<BindingEntry> bindings() const;
	bool setBinding(
		const std::string& device_id,
		const std::string& control_id,
		const std::string& control_label,
		std::optional<uint32_t> gesture_class_id,
		GestureTriggerMode trigger_mode,
		uint32_t repeat_interval_ms);
	void clearBindingsForDevice(const std::string& device_id);
	void clearAllBindings();

	bool sensorPipelineRunning() const;
	void appendDevLog(const std::string& level, const std::string& message);

	wave::appliance::ApplianceManager& applianceManager() { return m_applianceManager; }
	const wave::appliance::ApplianceManager& applianceManager() const { return m_applianceManager; }
	nlohmann::json appliancesApiJson(std::string_view locale_tag) const;

	void registerDevSocket(const drogon::WebSocketConnectionPtr& conn);
	void unregisterDevSocket(const drogon::WebSocketConnectionPtr& conn);
	void broadcastDevUpdate();

	std::string buildDevJson() const;

private:
	AppState() = default;

	void pushHistory(const HistoryEvent& ev);
	std::string nowIsoUtc() const;
	std::string applianceConfigPathLocked() const;
	std::string serverStatePathLocked() const;
	bool persistServerState(std::string* error = nullptr) const;
	void schedulePersistServerState() const;
	bool pruneInvalidBindingsLocked(std::vector<std::string>* warnings = nullptr);
	bool bindingSupportedLocked(const BindingEntry& binding) const;
	std::unordered_map<uint32_t, GestureTriggerConfig> bindingTriggerOverridesLocked() const;
	void syncSensorTriggerBindings(const std::unordered_map<uint32_t, GestureTriggerConfig>& overrides);
	void dispatchBindingActions(
		std::vector<BindingEntry> bindings,
		uint32_t gesture_class_id,
		float score,
		std::string gesture_name,
		std::string active_set);

	mutable std::mutex m_mutex;
	std::string m_configRoot;
	std::string m_gestureRoot;
	bool m_ncnnProfilingEnabled = false;
	std::deque<float> m_combinedInferenceMs;
	std::deque<float> m_cpuPercentHistory;
	util::CpuSampler m_cpuSampler;
	GestureRepository m_gestures;

	RadarState m_radar;
	InferenceSnapshot m_inference;
	SensorPipeline m_sensorPipeline;
	std::vector<wave::GestureGateDebug> m_gateDebug;

	std::chrono::steady_clock::time_point m_serverStarted {};
	std::deque<HistoryEvent> m_history;
	uint64_t m_nextHistoryId = 1;
	uint32_t m_todayCount = 0;
	std::string m_todayKey;

	std::vector<BindingEntry> m_bindings;
	nlohmann::json m_serverSettings = nlohmann::json::object();

	struct DevLogLine
	{
		std::string at;
		std::string level;
		std::string message;
	};
	std::deque<DevLogLine> m_devLogs;

	std::vector<drogon::WebSocketConnectionPtr> m_devSockets;

	wave::appliance::ApplianceManager m_applianceManager;
};

WAVE_NAMESPACE_END
