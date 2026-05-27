#include "gesture_probability_gate.h"

#include "core/coredef.h"

WAVE_NAMESPACE_BEGIN

namespace
{
	bool cooldownElapsed(
		const std::chrono::steady_clock::time_point& last,
		uint32_t cooldown_ms)
	{
		if (cooldown_ms == 0)
			return true;
		const auto elapsed = std::chrono::steady_clock::now() - last;
		return elapsed >= std::chrono::milliseconds(cooldown_ms);
	}

	const char* stateName(const GestureGateState state)
	{
		return state == GestureGateState::ArmedHigh ? "armed_high" : "idle";
	}
}

uint32_t GestureProbabilityGate::elapsedMs(
	const std::chrono::steady_clock::time_point& since)
{
	if (!since.time_since_epoch().count())
		return 0;
	const auto elapsed = std::chrono::steady_clock::now() - since;
	return static_cast<uint32_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

void GestureProbabilityGate::configure(
	const GestureSetManifest& manifest,
	const std::unordered_map<uint32_t, GestureTriggerConfig>& binding_overrides)
{
	m_gates.clear();
	for (const auto& [class_id, binding_config] : binding_overrides)
	{
		GateState gate {};
		if (const auto it = manifest.triggersByClassId.find(class_id);
			it != manifest.triggersByClassId.end())
		{
			gate.config = it->second;
		}
		gate.config.mode = binding_config.mode;
		gate.config.repeatIntervalMs = binding_config.repeatIntervalMs;
		m_gates[class_id] = gate;
	}
}

void GestureProbabilityGate::reset()
{
	for (auto& [_, gate] : m_gates)
	{
		gate.state = GestureGateState::Idle;
		gate.holdActive = false;
		gate.holdSince = {};
	}
}

std::vector<GestureTriggerEvent> GestureProbabilityGate::update(
	const std::vector<float>& class_scores)
{
	std::vector<GestureTriggerEvent> events;
	events.reserve(m_gates.size());
	const auto now = std::chrono::steady_clock::now();

	for (auto& [class_id, gate] : m_gates)
	{
		const size_t idx = class_id;
		const float score =
			idx < class_scores.size() ? class_scores[idx] : 0.f;

		gate.lastScore = score;

		GestureTriggerEvent event {};
		event.gestureClassId = class_id;
		event.score = score;

		switch (gate.state)
		{
		case GestureGateState::Idle:
			if (score >= gate.config.highThreshold)
			{
				if (!gate.holdActive)
				{
					gate.holdActive = true;
					gate.holdSince = now;
				}
				else if (elapsedMs(gate.holdSince) >= gate.config.minHighHoldMs)
				{
					if (cooldownElapsed(gate.lastFire, gate.config.cooldownMs))
					{
						gate.lastFire = now;
						event.fired = true;
					}
					gate.state = GestureGateState::ArmedHigh;
					gate.holdActive = false;
					gate.holdSince = {};
				}
			}
			else
			{
				gate.holdActive = false;
				gate.holdSince = {};
			}
			break;
		case GestureGateState::ArmedHigh:
			if (score >= gate.config.highThreshold)
			{
				gate.holdActive = false;
				gate.holdSince = {};
				if (gate.config.mode == GestureTriggerMode::Repeat &&
					gate.config.repeatIntervalMs > 0 &&
					elapsedMs(gate.lastFire) >= gate.config.repeatIntervalMs)
				{
					gate.lastFire = now;
					event.fired = true;
				}
			}
			else if (score <= gate.config.lowThreshold)
			{
				if (!gate.holdActive)
				{
					gate.holdActive = true;
					gate.holdSince = now;
				}
				else if (elapsedMs(gate.holdSince) >= gate.config.minLowHoldMs)
				{
					gate.state = GestureGateState::Idle;
					gate.holdActive = false;
					gate.holdSince = {};
				}
			}
			else
			{
				gate.holdActive = false;
				gate.holdSince = {};
			}
			break;
		}

		events.push_back(event);
	}

	return events;
}

std::vector<GestureGateDebug> GestureProbabilityGate::debugSnapshot() const
{
	std::vector<GestureGateDebug> out;
	out.reserve(m_gates.size());

	for (const auto& [class_id, gate] : m_gates)
	{
		GestureGateDebug debug {};
		debug.gestureClassId = class_id;
		debug.score = gate.lastScore;
		debug.state = stateName(gate.state);
		debug.triggerMode = std::string(gestureTriggerModeName(gate.config.mode));
		debug.highThreshold = gate.config.highThreshold;
		debug.lowThreshold = gate.config.lowThreshold;
		debug.cooldownMs = gate.config.cooldownMs;
		debug.minHighHoldMs = gate.config.minHighHoldMs;
		debug.minLowHoldMs = gate.config.minLowHoldMs;
		debug.repeatIntervalMs = gate.config.repeatIntervalMs;
		debug.ready = cooldownElapsed(gate.lastFire, gate.config.cooldownMs);

		debug.holdRequiredMs = gate.state == GestureGateState::Idle
			? gate.config.minHighHoldMs
			: gate.config.minLowHoldMs;
		debug.holdProgressMs = gate.holdActive ? elapsedMs(gate.holdSince) : 0;

		if (gate.holdActive)
		{
			debug.state = gate.state == GestureGateState::Idle
				? "arming_high"
				: "arming_low";
		}

		out.push_back(debug);
	}

	return out;
}

WAVE_NAMESPACE_END
