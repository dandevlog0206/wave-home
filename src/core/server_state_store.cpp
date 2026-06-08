#include "core/server_state_store.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

WAVE_NAMESPACE_BEGIN
CORE_NAMESPACE_BEGIN

namespace
{
	std::string readFileText(const std::filesystem::path& path)
	{
		std::ifstream in(path);
		if (!in)
			throw std::runtime_error("cannot open server state: " + path.string());

		std::ostringstream buffer;
		buffer << in.rdbuf();
		return buffer.str();
	}

	void writeJsonFile(const std::filesystem::path& path, const nlohmann::json& json)
	{
		const auto parent = path.parent_path();
		if (!parent.empty())
			std::filesystem::create_directories(parent);

		std::ofstream out(path);
		if (!out)
			throw std::runtime_error("cannot write server state: " + path.string());
		out << json.dump(2) << '\n';
	}
} // namespace

ServerStateStore::ServerStateStore(std::filesystem::path path) :
	m_path(std::move(path))
{
}

void ServerStateStore::ensureExists() const
{
	if (std::filesystem::exists(m_path))
		return;

	save(ServerStateDocument {});
}

ServerStateDocument ServerStateStore::load() const
{
	ensureExists();

	const nlohmann::json root = nlohmann::json::parse(readFileText(m_path));
	if (!root.is_object())
		throw std::runtime_error("server state root must be an object");

	ServerStateDocument document;
	if (root.contains("activeSetId") && root.at("activeSetId").is_string())
		document.activeSetId = root.at("activeSetId").get<std::string>();
	if (root.contains("settings") && root.at("settings").is_object())
		document.settings = root.at("settings");

	auto parse_binding_array =
		[](const nlohmann::json& arr) -> std::vector<StoredBindingEntry> {
			std::vector<StoredBindingEntry> entries;
			if (!arr.is_array())
				return entries;
			for (const auto& item : arr)
			{
				if (!item.is_object())
					continue;

				StoredBindingEntry entry;
				if (item.contains("deviceId") && item.at("deviceId").is_string())
					entry.deviceId = item.at("deviceId").get<std::string>();
				if (item.contains("controlId") && item.at("controlId").is_string())
					entry.controlId = item.at("controlId").get<std::string>();
				if (item.contains("controlLabel") && item.at("controlLabel").is_string())
					entry.controlLabel = item.at("controlLabel").get<std::string>();
				if (item.contains("gestureClassId") && item.at("gestureClassId").is_number_unsigned())
					entry.gestureClassId = item.at("gestureClassId").get<uint32_t>();
				entry.triggerMode = gestureTriggerModeFromString(
					item.value("triggerMode", "pulse"));
				entry.repeatIntervalMs = std::max<uint32_t>(
					100u,
					item.value("repeatIntervalMs", 600u));
				entries.push_back(std::move(entry));
			}
			return entries;
		};

	if (root.contains("bindingsBySet") && root.at("bindingsBySet").is_object())
	{
		for (const auto& [set_id, bindings_json] : root.at("bindingsBySet").items())
			document.bindings_by_set[set_id] = parse_binding_array(bindings_json);
	}
	else if (root.contains("bindings") && root.at("bindings").is_array())
	{
		const std::string legacy_set_id = document.activeSetId.empty()
			? "default"
			: document.activeSetId;
		document.bindings_by_set[legacy_set_id] = parse_binding_array(root.at("bindings"));
	}

	return document;
}

void ServerStateStore::save(const ServerStateDocument& document) const
{
	nlohmann::json bindings_by_set = nlohmann::json::object();
	for (const auto& [set_id, bindings] : document.bindings_by_set)
	{
		nlohmann::json arr = nlohmann::json::array();
		for (const auto& entry : bindings)
		{
			arr.push_back({
				{"deviceId", entry.deviceId},
				{"controlId", entry.controlId},
				{"controlLabel", entry.controlLabel},
				{"gestureClassId", entry.gestureClassId},
				{"triggerMode", std::string(gestureTriggerModeName(entry.triggerMode))},
				{"repeatIntervalMs", entry.repeatIntervalMs},
			});
		}
		bindings_by_set[set_id] = std::move(arr);
	}

	writeJsonFile(
		m_path,
		{
			{"version", 1},
			{"activeSetId", document.activeSetId},
			{"settings", document.settings.is_object() ? document.settings : nlohmann::json::object()},
			{"bindingsBySet", bindings_by_set},
		});
}

CORE_NAMESPACE_END
WAVE_NAMESPACE_END
