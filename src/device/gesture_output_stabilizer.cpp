#include "gesture_output_stabilizer.h"

namespace wave
{
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

	const char* stateName(GestureTriggerState state)
	{
		return state == GestureTriggerState::ArmedHigh ? "armed_high" : "idle";
	}
}

uint32_t GestureOutputStabilizer::elapsedMs(
	const std::chrono::steady_clock::time_point& since)
{
	if (!since.time_since_epoch().count())
		return 0;
	const auto elapsed = std::chrono::steady_clock::now() - since;
	return static_cast<uint32_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

void GestureOutputStabilizer::configure(const GestureSetManifest& manifest)
{
	m_channels.clear();
	m_noneClassId = 0;
	for (uint32_t class_id : manifest.gestureClassIds)
	{
		ChannelState ch {};
		const auto it = manifest.triggersByClassId.find(class_id);
		if (it != manifest.triggersByClassId.end())
			ch.config = it->second;
		m_channels[class_id] = ch;
	}
}

void GestureOutputStabilizer::reset()
{
	for (auto& [_, ch] : m_channels)
	{
		ch.state = GestureTriggerState::Idle;
		ch.toggleOutput = false;
		ch.holdActive = false;
		ch.holdSince = {};
	}
}

std::vector<StabilizedGestureEvent> GestureOutputStabilizer::update(
	const std::vector<float>& class_scores)
{
	std::vector<StabilizedGestureEvent> events;
	events.reserve(m_channels.size());
	const auto now = std::chrono::steady_clock::now();

	for (auto& [class_id, ch] : m_channels)
	{
		const size_t idx = class_id;
		const float score =
			idx < class_scores.size() ? class_scores[idx] : 0.f;

		ch.lastScore = score;

		StabilizedGestureEvent ev {};
		ev.gestureClassId = class_id;
		ev.score = score;

		switch (ch.state)
		{
		case GestureTriggerState::Idle:
			if (score >= ch.config.highThreshold)
			{
				if (!ch.holdActive)
				{
					ch.holdActive = true;
					ch.holdSince = now;
				}
				else if (elapsedMs(ch.holdSince) >= ch.config.minHighHoldMs)
				{
					ch.state = GestureTriggerState::ArmedHigh;
					ch.holdActive = false;
					ch.holdSince = {};
				}
			}
			else
			{
				ch.holdActive = false;
				ch.holdSince = {};
			}
			break;
		case GestureTriggerState::ArmedHigh:
			if (score <= ch.config.lowThreshold)
			{
				if (!ch.holdActive)
				{
					ch.holdActive = true;
					ch.holdSince = now;
				}
				else if (elapsedMs(ch.holdSince) >= ch.config.minLowHoldMs)
				{
					if (ch.config.mode == GestureTriggerMode::Toggle)
					{
						if (cooldownElapsed(ch.lastFire, ch.config.cooldownMs))
						{
							ch.toggleOutput = !ch.toggleOutput;
							ch.lastFire = now;
							ev.toggled = true;
						}
					}
					else if (cooldownElapsed(ch.lastFire, ch.config.cooldownMs))
					{
						ch.lastFire = now;
						ev.toggled = true;
						ch.toggleOutput = true;
					}
					ch.state = GestureTriggerState::Idle;
					ch.holdActive = false;
					ch.holdSince = {};
				}
			}
			else
			{
				ch.holdActive = false;
				ch.holdSince = {};
			}
			break;
		}

		if (ch.config.mode == GestureTriggerMode::Pulse && ch.toggleOutput)
		{
			ev.active = true;
			ch.toggleOutput = false;
		}
		else
		{
			ev.active = ch.toggleOutput;
		}

		events.push_back(ev);
	}

	return events;
}

std::vector<GestureChannelDebug> GestureOutputStabilizer::debugSnapshot() const
{
	std::vector<GestureChannelDebug> out;
	out.reserve(m_channels.size());

	for (const auto& [class_id, ch] : m_channels)
	{
		GestureChannelDebug d {};
		d.gestureClassId = class_id;
		d.score = ch.lastScore;
		d.state = stateName(ch.state);
		d.highThreshold = ch.config.highThreshold;
		d.lowThreshold = ch.config.lowThreshold;
		d.cooldownMs = ch.config.cooldownMs;
		d.minHighHoldMs = ch.config.minHighHoldMs;
		d.minLowHoldMs = ch.config.minLowHoldMs;
		d.toggleOutput = ch.toggleOutput;
		d.mode = ch.config.mode == GestureTriggerMode::Pulse ? "pulse" : "toggle";

		d.holdRequiredMs = ch.state == GestureTriggerState::Idle
			? ch.config.minHighHoldMs
			: ch.config.minLowHoldMs;
		d.holdProgressMs = ch.holdActive ? elapsedMs(ch.holdSince) : 0;

		if (ch.holdActive)
		{
			d.state = ch.state == GestureTriggerState::Idle
				? "arming_high"
				: "arming_low";
		}

		out.push_back(d);
	}

	return out;
}

} // namespace wave
