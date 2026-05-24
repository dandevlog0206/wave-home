#pragma once

#include "device/gesture_trigger_config.h"

#include <string>
#include <unordered_map>
#include <vector>

class GestureRepository
{
public:
	bool load(const std::string& gesture_set_root);

	const std::string& root() const { return m_root; }
	const std::string& activeSetId() const { return m_activeSetId; }
	void setActiveSetId(std::string set_id) { m_activeSetId = std::move(set_id); }

	const std::vector<std::string>& setIds() const { return m_setIds; }
	const GestureSetManifest* findSet(const std::string& set_id) const;

	std::string gestureName(const std::string& set_id, uint32_t class_id) const;
	std::string mediaUrl(const std::string& set_id, const std::string& relative_path) const;

private:
	bool loadSetFile(const std::string& set_id);

	std::string m_root;
	std::string m_activeSetId;
	std::vector<std::string> m_setIds;
	std::unordered_map<std::string, GestureSetManifest> m_sets;
};
