#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "gesture_trigger_config.h"
#include "core/coredef.h"

WAVE_NAMESPACE_BEGIN

enum class GestureGateState
{
	Idle,
	ArmedHigh
};

struct GestureTriggerEvent
{
	uint32_t gestureClassId = 0;
	bool fired = false;
	float score = 0.f;
};

struct GestureGateDebug
{
	uint32_t gestureClassId = 0;
	std::string state = "idle";
	float score = 0.f;
	std::string triggerMode = "pulse";
	float highThreshold = 0.f;
	float lowThreshold = 0.f;
	uint32_t cooldownMs = 0;
	uint32_t minHighHoldMs = 0;
	uint32_t minLowHoldMs = 0;
	uint32_t repeatIntervalMs = 0;
	uint32_t holdProgressMs = 0;
	uint32_t holdRequiredMs = 0;
	bool ready = true;
};

/// Converts per-class model scores into trigger events via hysteresis, hold, cooldown, and repeat gating.
class GestureProbabilityGate
{
public:
	void configure(
		const GestureSetManifest& manifest,
		const std::unordered_map<uint32_t, GestureTriggerConfig>& binding_overrides);

	void reset();

	std::vector<GestureTriggerEvent> update(const std::vector<float>& class_scores);

	std::vector<GestureGateDebug> debugSnapshot() const;

private:
	struct GateState
	{
		GestureTriggerConfig config {};
		GestureGateState state = GestureGateState::Idle;
		std::chrono::steady_clock::time_point lastFire {};
		std::chrono::steady_clock::time_point holdSince {};
		bool holdActive = false;
		float lastScore = 0.f;
	};

	static uint32_t elapsedMs(const std::chrono::steady_clock::time_point& since);

	std::unordered_map<uint32_t, GateState> m_gates;
};

WAVE_NAMESPACE_END
