#pragma once

#include "gesture_trigger_config.h"

#include <string>

class GestureSetCatalog
{
public:
	bool loadFromDirectory(const std::string& gesture_set_root);
	void setRoot(const std::string& gesture_set_root) { m_root = gesture_set_root; }
	bool loadSet(const std::string& set_id);

	const GestureSetManifest& activeSet() const { return m_active; }
	const std::string& activeSetId() const { return m_activeSetId; }

private:
	std::string m_root;
	std::string m_activeSetId;
	GestureSetManifest m_active;
};
