#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../core/coredef.h"

WAVE_NAMESPACE_BEGIN

enum class GestureTriggerMode
{
	Pulse,
	Toggle,
	Repeat
};

inline constexpr std::string_view gestureTriggerModeName(const GestureTriggerMode mode)
{
	switch (mode)
	{
	case GestureTriggerMode::Toggle:
		return "toggle";
	case GestureTriggerMode::Repeat:
		return "repeat";
	case GestureTriggerMode::Pulse:
	default:
		return "pulse";
	}
}

inline GestureTriggerMode gestureTriggerModeFromString(const std::string_view value)
{
	if (value == "toggle")
		return GestureTriggerMode::Toggle;
	if (value == "repeat")
		return GestureTriggerMode::Repeat;
	return GestureTriggerMode::Pulse;
}

struct GestureTriggerConfig
{
	GestureTriggerMode mode = GestureTriggerMode::Pulse;
	float highThreshold = 0.55f;
	float lowThreshold = 0.35f;
	uint32_t cooldownMs = 800;
	uint32_t minHighHoldMs = 120;
	uint32_t minLowHoldMs = 120;
	uint32_t repeatIntervalMs = 600;
};

struct GestureDefinition
{
	uint32_t gestureClassId = 0;
	std::string name;
	std::string mediaPreview;
};

struct GestureSetManifest
{
	std::string id;
	std::string name;
	std::string description;
	std::string modelJsonPath;
	std::vector<uint32_t> gestureClassIds;
	std::vector<GestureDefinition> gestures;
	std::unordered_map<uint32_t, GestureTriggerConfig> triggersByClassId;
	std::unordered_map<uint32_t, std::string> classLabels;
};

WAVE_NAMESPACE_END
