#include "core/appliance_config_store.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/locale.h"

WAVE_NAMESPACE_BEGIN
CORE_NAMESPACE_BEGIN

namespace
{
	std::string readFileText(const std::filesystem::path& path)
	{
		std::ifstream in(path);
		if (!in)
			throw std::runtime_error("cannot open appliance config: " + path.string());

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
			throw std::runtime_error("cannot write appliance config: " + path.string());
		out << json.dump(2) << '\n';
	}

	std::string slugifyId(const std::string& name)
	{
		std::string out;
		out.reserve(name.size());
		bool prev_dash = false;

		for (const unsigned char ch : name)
		{
			if (std::isalnum(ch))
			{
				out.push_back(static_cast<char>(std::tolower(ch)));
				prev_dash = false;
			}
			else if (!prev_dash)
			{
				out.push_back('-');
				prev_dash = true;
			}
		}

		while (!out.empty() && out.back() == '-')
			out.pop_back();
		return out.empty() ? "appliance" : out;
	}

	std::string jsonString(
		const nlohmann::json& object,
		const char* key,
		const std::string& fallback = {})
	{
		if (!object.contains(key) || !object.at(key).is_string())
			return fallback;
		return object.at(key).get<std::string>();
	}

	appliance::ApplianceKind parseApplianceKind(const std::string& kind)
	{
		if (kind == "tizen")
			return appliance::ApplianceKind::Tizen;
		if (kind == "tuya")
			return appliance::ApplianceKind::Tuya;
		return appliance::ApplianceKind::Unknown;
	}

	appliance::ApplianceInputKind parseInputKind(const std::string& kind)
	{
		if (kind == "power")
			return appliance::ApplianceInputKind::Power;
		if (kind == "volume")
			return appliance::ApplianceInputKind::Volume;
		if (kind == "channel")
			return appliance::ApplianceInputKind::Channel;
		if (kind == "navigation")
			return appliance::ApplianceInputKind::Navigation;
		if (kind == "app")
			return appliance::ApplianceInputKind::App;
		if (kind == "source")
			return appliance::ApplianceInputKind::Source;
		if (kind == "toggle")
			return appliance::ApplianceInputKind::Toggle;
		return appliance::ApplianceInputKind::Command;
	}

	appliance::InputTriggerMode parseInputTriggerMode(const std::string& mode)
	{
		return mode == "toggle"
			? appliance::InputTriggerMode::Toggle
			: appliance::InputTriggerMode::Pulse;
	}

	std::vector<appliance::ApplianceCommand> parseCommands(const nlohmann::json& array)
	{
		std::vector<appliance::ApplianceCommand> commands;
		if (!array.is_array())
			return commands;

		for (const auto& item : array)
		{
			if (!item.is_object())
				continue;
			const std::string channel = jsonString(item, "channel");
			const std::string payload = jsonString(item, "payload");
			if (channel.empty())
				continue;
			commands.push_back({channel, payload});
		}
		return commands;
	}

	std::string defaultTransportKind(const appliance::ApplianceKind kind)
	{
		if (kind == appliance::ApplianceKind::Tizen)
			return "tizen";
		if (kind == appliance::ApplianceKind::Tuya)
			return "tuya";
		return "mqtt";
	}

	std::string defaultTransportEndpoint(
		const appliance::ApplianceKind kind,
		const nlohmann::json& options)
	{
		if (kind == appliance::ApplianceKind::Tizen)
		{
			const std::string ip = jsonString(options, "ip");
			const int secure_port = options.value("securePort", 8002);
			if (!ip.empty())
				return "wss://" + ip + ":" + std::to_string(secure_port);
			return {};
		}
		if (kind == appliance::ApplianceKind::Tuya)
		{
			const std::string device_id = jsonString(options, "deviceId", jsonString(options, "id"));
			if (!device_id.empty())
				return "tuya://" + device_id;
			return {};
		}
		return "mqtt://127.0.0.1:1883";
	}

	void upsertInput(
		std::vector<appliance::ApplianceInputDefinition>& inputs,
		std::unordered_set<std::string>& ids,
		appliance::ApplianceInputDefinition input)
	{
		if (input.id.empty())
			input.id = slugifyId(input.label.empty() ? input.labelKey : input.label);
		if (input.id.empty())
			return;
		if (ids.insert(input.id).second)
			inputs.push_back(std::move(input));
	}

	appliance::ApplianceInputDefinition makePowerInput()
	{
		appliance::ApplianceInputDefinition input;
		input.id = "power";
		input.labelKey = std::string(locale::key::kDeviceInputPower);
		input.kind = appliance::ApplianceInputKind::Power;
		input.triggerMode = appliance::InputTriggerMode::Toggle;
		input.onCommands.push_back({"power", "ON"});
		input.offCommands.push_back({"power", "OFF"});
		return input;
	}

	void addDefaultTuyaInputs(
		std::vector<appliance::ApplianceInputDefinition>& inputs,
		std::unordered_set<std::string>& ids)
	{
		upsertInput(inputs, ids, {"power-on", "전원 켜기", "",
			appliance::ApplianceInputKind::Power, appliance::InputTriggerMode::Pulse,
			{{"power", "ON"}}, {}, {}, {}});
		upsertInput(inputs, ids, {"power-off", "전원 끄기", "",
			appliance::ApplianceInputKind::Power, appliance::InputTriggerMode::Pulse,
			{{"power", "OFF"}}, {}, {}, {}});
		upsertInput(inputs, ids, {"power-toggle", "전원 토글", "",
			appliance::ApplianceInputKind::Power, appliance::InputTriggerMode::Pulse,
			{{"power", "TOGGLE"}}, {}, {}, {}});
	}

	void addDefaultTizenInputs(
		std::vector<appliance::ApplianceInputDefinition>& inputs,
		std::unordered_set<std::string>& ids)
	{
		upsertInput(inputs, ids, makePowerInput());
		upsertInput(inputs, ids, {"volume_up", "", std::string(locale::key::kDeviceInputVolumeUp),
			appliance::ApplianceInputKind::Volume, appliance::InputTriggerMode::Pulse,
			{{"remoteKey", "KEY_VOLUP"}}, {}, {}, {}});
		upsertInput(inputs, ids, {"volume_down", "", std::string(locale::key::kDeviceInputVolumeDown),
			appliance::ApplianceInputKind::Volume, appliance::InputTriggerMode::Pulse,
			{{"remoteKey", "KEY_VOLDOWN"}}, {}, {}, {}});
		upsertInput(inputs, ids, {"channel_up", "", std::string(locale::key::kDeviceInputChannelUp),
			appliance::ApplianceInputKind::Channel, appliance::InputTriggerMode::Pulse,
			{{"remoteKey", "KEY_CHUP"}}, {}, {}, {}});
		upsertInput(inputs, ids, {"channel_down", "", std::string(locale::key::kDeviceInputChannelDown),
			appliance::ApplianceInputKind::Channel, appliance::InputTriggerMode::Pulse,
			{{"remoteKey", "KEY_CHDOWN"}}, {}, {}, {}});
		upsertInput(inputs, ids, {"home", "", std::string(locale::key::kDeviceInputHome),
			appliance::ApplianceInputKind::Navigation, appliance::InputTriggerMode::Pulse,
			{{"remoteKey", "KEY_HOME"}}, {}, {}, {}});
	}

	appliance::ApplianceInputDefinition parseInputDefinition(const nlohmann::json& json)
	{
		appliance::ApplianceInputDefinition input;
		input.id = jsonString(json, "id");
		input.label = jsonString(json, "label");
		input.labelKey = jsonString(json, "labelKey");
		input.kind = parseInputKind(jsonString(json, "kind", "command"));
		input.triggerMode = parseInputTriggerMode(jsonString(json, "triggerMode", "pulse"));
		if (json.contains("triggerCommands"))
			input.triggerCommands = parseCommands(json.at("triggerCommands"));
		if (json.contains("onCommands"))
			input.onCommands = parseCommands(json.at("onCommands"));
		if (json.contains("offCommands"))
			input.offCommands = parseCommands(json.at("offCommands"));
		if (json.contains("meta") && json.at("meta").is_object())
			input.meta = json.at("meta");
		return input;
	}

	appliance::ApplianceDefinition parseApplianceDefinition(const nlohmann::json& json)
	{
		if (!json.is_object())
			throw std::runtime_error("appliance entry must be an object");

		appliance::ApplianceDefinition definition;
		definition.name = jsonString(json, "name", "Appliance");
		definition.id = jsonString(json, "id", slugifyId(definition.name));
		definition.kind = parseApplianceKind(jsonString(json, "kind"));
		if (definition.kind == appliance::ApplianceKind::Unknown)
			throw std::runtime_error("unsupported appliance kind for id: " + definition.id);
		definition.room = jsonString(json, "room");
		definition.typeLabelKey = jsonString(json, "typeLabelKey");
		if (definition.typeLabelKey.empty() &&
			definition.kind == appliance::ApplianceKind::Tizen)
		{
			definition.typeLabelKey = std::string(locale::key::kDeviceTypeTelevision);
		}

		if (json.contains("metadata") && json.at("metadata").is_object())
			definition.metadata = json.at("metadata");

		if (json.contains("transport") && json.at("transport").is_object())
		{
			const auto& transport = json.at("transport");
			definition.transport.kind = jsonString(
				transport,
				"kind",
				defaultTransportKind(definition.kind));
			if (transport.contains("options") && transport.at("options").is_object())
				definition.transport.options = transport.at("options");
			definition.transport.endpoint = jsonString(
				transport,
				"endpoint",
				defaultTransportEndpoint(definition.kind, definition.transport.options));
		}
		else
		{
			definition.transport.kind = defaultTransportKind(definition.kind);
			definition.transport.endpoint = defaultTransportEndpoint(
				definition.kind,
				definition.transport.options);
		}

		if (definition.kind == appliance::ApplianceKind::Tizen)
		{
			if (!definition.transport.options.contains("name"))
				definition.transport.options["name"] = definition.name;
			if (!definition.transport.options.contains("securePort"))
				definition.transport.options["securePort"] = 8002;
			if (!definition.transport.options.contains("apiPort"))
				definition.transport.options["apiPort"] = 8001;
			if (!definition.transport.options.contains("timeoutMs"))
				definition.transport.options["timeoutMs"] = 1500;
			if (!definition.transport.options.contains("socketTimeoutMs"))
				definition.transport.options["socketTimeoutMs"] = 500;
			if (!definition.transport.options.contains("sessionMaxIdleMs"))
				definition.transport.options["sessionMaxIdleMs"] = 5000;
			if (!definition.transport.options.contains("sessionKeepalive"))
				definition.transport.options["sessionKeepalive"] = true;
			if (!definition.transport.options.contains("sessionKeepaliveIntervalMs"))
				definition.transport.options["sessionKeepaliveIntervalMs"] = 3000;
		}
		if (definition.kind == appliance::ApplianceKind::Tuya)
		{
			if (!definition.transport.options.contains("deviceId"))
				definition.transport.options["deviceId"] =
					jsonString(definition.transport.options, "id");
			if (!definition.transport.options.contains("port"))
				definition.transport.options["port"] = 6668;
			if (!definition.transport.options.contains("version"))
				definition.transport.options["version"] = "3.3";
			if (!definition.transport.options.contains("switchDp"))
				definition.transport.options["switchDp"] = "1";
			if (!definition.transport.options.contains("timeoutMs"))
				definition.transport.options["timeoutMs"] = 3000;
		}
		if (definition.transport.endpoint.empty())
		{
			throw std::runtime_error(
				"missing transport endpoint for appliance id: " + definition.id);
		}

		std::unordered_set<std::string> input_ids;
		if (json.contains("inputs") && json.at("inputs").is_array())
		{
			for (const auto& input_json : json.at("inputs"))
				upsertInput(definition.inputs, input_ids, parseInputDefinition(input_json));
		}

		const bool include_default_inputs = json.value(
			"includeDefaultInputs",
			definition.kind == appliance::ApplianceKind::Tizen ||
				definition.kind == appliance::ApplianceKind::Tuya);
		if (include_default_inputs &&
			definition.kind == appliance::ApplianceKind::Tizen)
		{
			addDefaultTizenInputs(definition.inputs, input_ids);
		}
		if (include_default_inputs &&
			definition.kind == appliance::ApplianceKind::Tuya)
		{
			addDefaultTuyaInputs(definition.inputs, input_ids);
		}

		return definition;
	}
} // namespace

ApplianceConfigStore::ApplianceConfigStore(std::filesystem::path path) :
	m_path(std::move(path))
{
}

void ApplianceConfigStore::ensureExists() const
{
	if (std::filesystem::exists(m_path))
		return;

	writeJsonFile(
		m_path,
		{
			{"version", 1},
			{"appliances", nlohmann::json::array()},
		});
}

ApplianceConfigDocument ApplianceConfigStore::load() const
{
	ensureExists();

	const nlohmann::json root = nlohmann::json::parse(readFileText(m_path));
	if (!root.is_object())
		throw std::runtime_error("appliance config root must be an object");

	ApplianceConfigDocument document;
	std::unordered_set<std::string> ids;
	if (root.contains("appliances") && root.at("appliances").is_array())
	{
		for (const auto& appliance_json : root.at("appliances"))
		{
			auto definition = parseApplianceDefinition(appliance_json);
			if (!ids.insert(definition.id).second)
			{
				throw std::runtime_error(
					"duplicate appliance id in config: " + definition.id);
			}
			document.appliances.push_back(std::move(definition));
		}
	}

	return document;
}

CORE_NAMESPACE_END
WAVE_NAMESPACE_END
