#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

enum class GestureTriggerMode
{
	Toggle,
	Pulse
};

struct GestureTriggerConfig
{
	GestureTriggerMode mode = GestureTriggerMode::Toggle;
	float highThreshold = 0.55f;
	float lowThreshold = 0.35f;
	uint32_t cooldownMs = 800;
	uint32_t minHighHoldMs = 120;
	uint32_t minLowHoldMs = 120;
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
