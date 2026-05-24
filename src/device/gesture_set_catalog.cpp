#include "gesture_set_catalog.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <filesystem>
#include <stdexcept>

namespace
{
	GestureTriggerMode parseMode(const std::string& mode)
	{
		if (mode == "pulse")
			return GestureTriggerMode::Pulse;
		return GestureTriggerMode::Toggle;
	}
}

bool GestureSetCatalog::loadFromDirectory(const std::string& gesture_set_root)
{
	m_root = gesture_set_root;
	const std::filesystem::path registry_path = std::filesystem::path(m_root) / "registry.json";
	if (!std::filesystem::exists(registry_path))
		return false;

	std::ifstream in(registry_path);
	nlohmann::json registry;
	in >> registry;

	const std::string default_id = registry.value("defaultActiveSetId", "set0");
	return loadSet(default_id);
}

bool GestureSetCatalog::loadSet(const std::string& set_id)
{
	const std::filesystem::path set_path = std::filesystem::path(m_root) / set_id / "set.json";
	if (!std::filesystem::exists(set_path))
		return false;

	std::ifstream in(set_path);
	nlohmann::json doc;
	in >> doc;

	m_activeSetId = set_id;
	m_active = GestureSetManifest {};
	m_active.id = doc.at("id").get<std::string>();
	m_active.name = doc.value("name", set_id);
	m_active.description = doc.value("description", "");
	m_active.modelJsonPath = (std::filesystem::path(m_root) / set_id / doc.at("modelPath").get<std::string>()).string();

	if (doc.contains("classLabels"))
	{
		for (const auto& [key, val] : doc.at("classLabels").items())
			m_active.classLabels[std::stoul(key)] = val.get<std::string>();
	}

	for (const auto& cid : doc.at("gestureClassIds"))
		m_active.gestureClassIds.push_back(cid.get<uint32_t>());

	for (const auto& gesture : doc.at("gestures"))
	{
		const uint32_t class_id = gesture.at("gestureClassId").get<uint32_t>();

		GestureDefinition def {};
		def.gestureClassId = class_id;
		def.name = gesture.at("name").get<std::string>();
		if (gesture.contains("media") && gesture.at("media").contains("preview"))
			def.mediaPreview = gesture.at("media").at("preview").get<std::string>();
		m_active.gestures.push_back(def);

		GestureTriggerConfig cfg {};
		if (gesture.contains("trigger"))
		{
			const auto& t = gesture.at("trigger");
			cfg.mode = parseMode(t.value("mode", "toggle"));
			cfg.highThreshold = t.value("highThreshold", 0.55f);
			cfg.lowThreshold = t.value("lowThreshold", 0.35f);
			cfg.cooldownMs = t.value("cooldownMs", 800u);
			cfg.minHighHoldMs = t.value("minHighHoldMs", 120u);
			cfg.minLowHoldMs = t.value("minLowHoldMs", 120u);
		}
		m_active.triggersByClassId[class_id] = cfg;
	}

	return true;
}
