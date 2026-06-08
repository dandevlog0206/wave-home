#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/coredef.h"
#include "device/gesture_trigger_config.h"

WAVE_NAMESPACE_BEGIN
CORE_NAMESPACE_BEGIN

struct StoredBindingEntry
{
	std::string deviceId;
	std::string controlId;
	std::string controlLabel;
	uint32_t gestureClassId = 0;
	GestureTriggerMode triggerMode = GestureTriggerMode::Pulse;
	uint32_t repeatIntervalMs = 600;
};

struct ServerStateDocument
{
	std::string activeSetId;
	std::unordered_map<std::string, std::vector<StoredBindingEntry>> bindings_by_set;
	nlohmann::json settings = nlohmann::json::object();
};

class ServerStateStore
{
public:
	explicit ServerStateStore(std::filesystem::path path);

	const std::filesystem::path& path() const { return m_path; }

	ServerStateDocument load() const;
	void save(const ServerStateDocument& document) const;

private:
	void ensureExists() const;

	std::filesystem::path m_path;
};

CORE_NAMESPACE_END
WAVE_NAMESPACE_END
