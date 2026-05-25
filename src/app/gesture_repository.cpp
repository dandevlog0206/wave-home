#include "gesture_repository.h"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

WAVE_NAMESPACE_BEGIN

bool GestureRepository::load(const std::string& gesture_set_root)
{
	m_root = gesture_set_root;
	m_sets.clear();
	m_setIds.clear();

	const std::filesystem::path registry_path = std::filesystem::path(m_root) / "registry.json";
	if (!std::filesystem::exists(registry_path))
		return false;

	std::ifstream in(registry_path);
	nlohmann::json registry;
	in >> registry;

	m_activeSetId = registry.value("defaultActiveSetId", "set0");

	for (const auto& entry : registry.at("sets"))
	{
		if (!entry.value("enabled", true))
			continue;
		const std::string id = entry.at("id").get<std::string>();
		if (!loadSetFile(id))
			continue;
		m_setIds.push_back(id);
	}

	return !m_setIds.empty();
}

bool GestureRepository::loadSetFile(const std::string& set_id)
{
	const std::filesystem::path set_path = std::filesystem::path(m_root) / set_id / "set.json";
	if (!std::filesystem::exists(set_path))
		return false;

	std::ifstream in(set_path);
	nlohmann::json doc;
	in >> doc;

	GestureSetManifest manifest {};
	manifest.id = doc.at("id").get<std::string>();
	manifest.name = doc.value("name", set_id);
	manifest.description = doc.value("description", "");
	manifest.modelJsonPath =
		(std::filesystem::path(m_root) / set_id / doc.at("modelPath").get<std::string>()).string();

	if (doc.contains("classLabels"))
	{
		for (const auto& [key, val] : doc.at("classLabels").items())
			manifest.classLabels[std::stoul(key)] = val.get<std::string>();
	}

	for (const auto& cid : doc.at("gestureClassIds"))
		manifest.gestureClassIds.push_back(cid.get<uint32_t>());

	for (const auto& gesture : doc.at("gestures"))
	{
		const uint32_t class_id = gesture.at("gestureClassId").get<uint32_t>();

		GestureDefinition def {};
		def.gestureClassId = class_id;
		def.name = gesture.at("name").get<std::string>();
		if (gesture.contains("media") && gesture.at("media").contains("preview"))
			def.mediaPreview = gesture.at("media").at("preview").get<std::string>();
		manifest.gestures.push_back(def);

		GestureTriggerConfig cfg {};
		if (gesture.contains("trigger"))
		{
			const auto& t = gesture.at("trigger");
			cfg.mode = gestureTriggerModeFromString(t.value("mode", "pulse"));
			cfg.highThreshold = t.value("highThreshold", 0.55f);
			cfg.lowThreshold = t.value("lowThreshold", 0.35f);
			cfg.cooldownMs = t.value("cooldownMs", 800u);
			cfg.minHighHoldMs = t.value("minHighHoldMs", 120u);
			cfg.minLowHoldMs = t.value("minLowHoldMs", 120u);
			cfg.repeatIntervalMs = t.value("repeatIntervalMs", 600u);
		}
		manifest.triggersByClassId[class_id] = cfg;
	}

	m_sets[set_id] = std::move(manifest);
	return true;
}

const GestureSetManifest* GestureRepository::findSet(const std::string& set_id) const
{
	const auto it = m_sets.find(set_id);
	return it == m_sets.end() ? nullptr : &it->second;
}

std::string GestureRepository::gestureName(const std::string& set_id, uint32_t class_id) const
{
	const auto* set = findSet(set_id);
	if (!set)
		return {};
	const auto it = set->classLabels.find(class_id);
	if (it != set->classLabels.end())
		return it->second;
	for (const auto& g : set->gestures)
		if (g.gestureClassId == class_id)
			return g.name;
	return {};
}

std::string GestureRepository::mediaUrl(const std::string& set_id, const std::string& relative_path) const
{
	if (relative_path.empty())
		return {};
	return "/api/v1/gesture-media/" + set_id + "/" + relative_path;
}

WAVE_NAMESPACE_END
