#pragma once

#include "gesture_trigger_config.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace wave
{

enum class GestureTriggerState
{
	Idle,
	ArmedHigh
};

struct StabilizedGestureEvent
{
	uint32_t gestureClassId = 0;
	bool active = false;
	bool toggled = false;
	float score = 0.f;
};

struct GestureChannelDebug
{
	uint32_t gestureClassId = 0;
	std::string state = "idle";
	float score = 0.f;
	float highThreshold = 0.f;
	float lowThreshold = 0.f;
	uint32_t cooldownMs = 0;
	uint32_t minHighHoldMs = 0;
	uint32_t minLowHoldMs = 0;
	uint32_t holdProgressMs = 0;
	uint32_t holdRequiredMs = 0;
	bool toggleOutput = false;
	std::string mode = "toggle";
};

/// Schmitt-trigger style stabilizer for per-class model outputs (toggle / pulse).
class GestureOutputStabilizer
{
public:
	void configure(const GestureSetManifest& manifest);

	void reset();

	std::vector<StabilizedGestureEvent> update(const std::vector<float>& class_scores);

	std::vector<GestureChannelDebug> debugSnapshot() const;

private:
	struct ChannelState
	{
		GestureTriggerConfig config {};
		GestureTriggerState state = GestureTriggerState::Idle;
		bool toggleOutput = false;
		std::chrono::steady_clock::time_point lastFire {};
		std::chrono::steady_clock::time_point holdSince {};
		bool holdActive = false;
		float lastScore = 0.f;
	};

	static uint32_t elapsedMs(const std::chrono::steady_clock::time_point& since);

	std::unordered_map<uint32_t, ChannelState> m_channels;
	uint32_t m_noneClassId = 0;
};

} // namespace wave
