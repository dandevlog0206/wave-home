#include "core/homebridge_parser.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/locale.h"

WAVE_NAMESPACE_BEGIN
CORE_NAMESPACE_BEGIN

namespace
{
	struct SamsungTokenData
	{
		std::string token;
		bool token_auth = true;
		bool power_state = true;
	};

	std::string readFile(const std::string& path)
	{
		std::ifstream in(path);
		if (!in)
			throw std::runtime_error("cannot open homebridge config: " + path);

		std::ostringstream buffer;
		buffer << in.rdbuf();
		return buffer.str();
	}

	std::optional<nlohmann::json> readJsonFile(const std::filesystem::path& path)
	{
		try
		{
			return nlohmann::json::parse(readFile(path.string()));
		}
		catch (const std::exception&)
		{
			return std::nullopt;
		}
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

	SamsungTokenData loadSamsungTokenData(const std::string& config_path)
	{
		SamsungTokenData token_data;
		const auto accessory_path = std::filesystem::path(config_path).parent_path() /
			"accessories" / "samsung-tizen.json";
		const auto json = readJsonFile(accessory_path);
		if (!json || !json->is_object())
			return token_data;

		for (const auto& [id, value] : json->items())
		{
			(void)id;
			if (!value.is_object())
				continue;

			if (token_data.token.empty() &&
				value.contains("token") &&
				value.at("token").is_string())
			{
				token_data.token = value.at("token").get<std::string>();
			}
			if (value.contains("tokenauth") && value.at("tokenauth").is_boolean())
				token_data.token_auth = value.at("tokenauth").get<bool>();
			if (value.contains("powerstate") && value.at("powerstate").is_boolean())
				token_data.power_state = value.at("powerstate").get<bool>();
			if (!token_data.token.empty())
				break;
		}

		return token_data;
	}

	std::string canonicalInputId(
		const std::string& label,
		const std::string& remote_key)
	{
		if (remote_key == "KEY_VOLUP")
			return "volume_up";
		if (remote_key == "KEY_VOLDOWN")
			return "volume_down";
		if (remote_key == "KEY_CHUP")
			return "channel_up";
		if (remote_key == "KEY_CHDOWN")
			return "channel_down";
		if (remote_key == "KEY_HOME")
			return "home";
		if (remote_key == "KEY_MUTE")
			return "mute";
		if (!label.empty())
			return slugifyId(label);
		return slugifyId(remote_key);
	}

	appliance::ApplianceInputKind kindFromRemoteKey(const std::string& remote_key)
	{
		if (remote_key == "KEY_VOLUP" || remote_key == "KEY_VOLDOWN")
			return appliance::ApplianceInputKind::Volume;
		if (remote_key == "KEY_CHUP" || remote_key == "KEY_CHDOWN")
			return appliance::ApplianceInputKind::Channel;
		if (remote_key == "KEY_HOME")
			return appliance::ApplianceInputKind::Navigation;
		if (remote_key == "KEY_HDMI1" || remote_key == "KEY_HDMI2" ||
			remote_key == "KEY_HDMI3" || remote_key == "KEY_HDMI4")
			return appliance::ApplianceInputKind::Source;
		return appliance::ApplianceInputKind::Command;
	}

	appliance::ApplianceCommand makeTransportCommand(
		const std::string& channel,
		const std::string& payload)
	{
		return {channel, payload};
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

	appliance::ApplianceInputDefinition makePowerInput(
		const std::string& label)
	{
		appliance::ApplianceInputDefinition input;
		input.id = "power";
		input.label = label;
		input.labelKey = input.label.empty()
			? std::string(locale::key::kDeviceInputPower)
			: "";
		input.kind = appliance::ApplianceInputKind::Power;
		input.triggerMode = appliance::InputTriggerMode::Toggle;
		input.onCommands.push_back(makeTransportCommand("power", "ON"));
		input.offCommands.push_back(makeTransportCommand("power", "OFF"));
		return input;
	}

	void addDefaultTizenInputs(
		std::vector<appliance::ApplianceInputDefinition>& inputs,
		std::unordered_set<std::string>& ids)
	{
		upsertInput(inputs, ids, makePowerInput(""));
		upsertInput(inputs, ids, {"volume_up", "", std::string(locale::key::kDeviceInputVolumeUp),
			appliance::ApplianceInputKind::Volume, appliance::InputTriggerMode::Pulse,
			{makeTransportCommand("remoteKey", "KEY_VOLUP")}, {}, {}, {}});
		upsertInput(inputs, ids, {"volume_down", "", std::string(locale::key::kDeviceInputVolumeDown),
			appliance::ApplianceInputKind::Volume, appliance::InputTriggerMode::Pulse,
			{makeTransportCommand("remoteKey", "KEY_VOLDOWN")}, {}, {}, {}});
		upsertInput(inputs, ids, {"channel_up", "", std::string(locale::key::kDeviceInputChannelUp),
			appliance::ApplianceInputKind::Channel, appliance::InputTriggerMode::Pulse,
			{makeTransportCommand("remoteKey", "KEY_CHUP")}, {}, {}, {}});
		upsertInput(inputs, ids, {"channel_down", "", std::string(locale::key::kDeviceInputChannelDown),
			appliance::ApplianceInputKind::Channel, appliance::InputTriggerMode::Pulse,
			{makeTransportCommand("remoteKey", "KEY_CHDOWN")}, {}, {}, {}});
		upsertInput(inputs, ids, {"home", "", std::string(locale::key::kDeviceInputHome),
			appliance::ApplianceInputKind::Navigation, appliance::InputTriggerMode::Pulse,
			{makeTransportCommand("remoteKey", "KEY_HOME")}, {}, {}, {}});
	}

	std::vector<nlohmann::json> mergedArray(
		const nlohmann::json& platform,
		const nlohmann::json& device,
		const char* key)
	{
		std::vector<nlohmann::json> out;
		if (platform.contains(key) && platform.at(key).is_array())
		{
			for (const auto& item : platform.at(key))
				out.push_back(item);
		}
		if (device.contains(key) && device.at(key).is_array())
		{
			for (const auto& item : device.at(key))
				out.push_back(item);
		}
		return out;
	}

	nlohmann::json mergedKeys(const nlohmann::json& platform, const nlohmann::json& device)
	{
		nlohmann::json keys = nlohmann::json::object();
		if (platform.contains("keys") && platform.at("keys").is_object())
			keys.update(platform.at("keys"));
		if (device.contains("keys") && device.at("keys").is_object())
			keys.update(device.at("keys"));
		return keys;
	}

	appliance::ApplianceDefinition parseSamsungDevice(
		const nlohmann::json& platform,
		const nlohmann::json& device,
		const SamsungTokenData& token_data)
	{
		appliance::ApplianceDefinition definition;
		definition.id = slugifyId(jsonString(device, "name", "tizen-device"));
		definition.kind = appliance::ApplianceKind::Tizen;
		definition.name = jsonString(device, "name", "Tizen TV");
		definition.typeLabelKey = std::string(locale::key::kDeviceTypeTelevision);
		const std::string ip = jsonString(device, "ip");
		const std::string mac = jsonString(device, "mac");
		const int secure_port = device.value("port", platform.value("port", 8002));
		definition.transport.kind = "tizen";
		definition.transport.endpoint = "wss://" + ip + ":" + std::to_string(secure_port);
		definition.transport.options["ip"] = ip;
		definition.transport.options["mac"] = mac;
		definition.transport.options["name"] = definition.name;
		definition.transport.options["securePort"] = secure_port;
		definition.transport.options["apiPort"] = 8001;
		definition.transport.options["timeoutMs"] = 1500;
		if (!token_data.token.empty())
			definition.transport.options["token"] = token_data.token;
		definition.transport.options["tokenAuth"] = token_data.token_auth;
		definition.transport.options["powerState"] = token_data.power_state;

		definition.metadata["ip"] = ip;
		definition.metadata["mac"] = mac;
		definition.metadata["plugin"] = "SamsungTizen";

		std::unordered_set<std::string> ids;

		for (const auto& input_json : mergedArray(platform, device, "inputs"))
		{
			const std::string type = jsonString(input_json, "type");
			const std::string name = jsonString(input_json, "name");
			const std::string value = jsonString(input_json, "value");

			appliance::ApplianceInputDefinition input;
			input.id = slugifyId(name);
			input.label = name;
			input.triggerMode = appliance::InputTriggerMode::Pulse;

			if (type == "command")
			{
				input.kind = kindFromRemoteKey(value);
				input.triggerCommands.push_back(makeTransportCommand("remoteKey", value));
				upsertInput(definition.inputs, ids, std::move(input));
			}
			else if (type == "app")
			{
				input.kind = appliance::ApplianceInputKind::App;
				input.triggerCommands.push_back(makeTransportCommand("app", value));
				upsertInput(definition.inputs, ids, std::move(input));
			}
			else if (type == "input")
			{
				input.kind = appliance::ApplianceInputKind::Source;
				input.triggerCommands.push_back(makeTransportCommand("source", value));
				input.meta["sourceType"] = type;
				upsertInput(definition.inputs, ids, std::move(input));
			}
			else if (type == "art")
			{
				input.kind = appliance::ApplianceInputKind::Source;
				input.triggerCommands.push_back(makeTransportCommand("art", value.empty() ? "art" : value));
				input.meta["sourceType"] = type;
				upsertInput(definition.inputs, ids, std::move(input));
			}
		}

		for (const auto& switch_json : mergedArray(platform, device, "switches"))
		{
			const std::string name = jsonString(switch_json, "name");
			appliance::ApplianceInputDefinition input;
			input.label = name;

			if (switch_json.value("power", false))
			{
				input = makePowerInput(name);
				upsertInput(definition.inputs, ids, std::move(input));
				continue;
			}
			if (switch_json.value("mute", false))
			{
				input.id = "mute";
				input.kind = appliance::ApplianceInputKind::Toggle;
				input.triggerMode = appliance::InputTriggerMode::Toggle;
				input.triggerCommands.push_back(makeTransportCommand("remoteKey", "KEY_MUTE"));
				upsertInput(definition.inputs, ids, std::move(input));
				continue;
			}

			if (jsonString(switch_json, "type") == "command")
			{
				const std::string value = jsonString(switch_json, "value");
				input.id = canonicalInputId(name, value);
				input.kind = kindFromRemoteKey(value);
				input.triggerMode = appliance::InputTriggerMode::Pulse;
				input.triggerCommands.push_back(makeTransportCommand("remoteKey", value));
				upsertInput(definition.inputs, ids, std::move(input));
			}
		}

		const nlohmann::json keys = mergedKeys(platform, device);
		for (const auto& [name, value] : keys.items())
		{
			if (!value.is_string())
				continue;

			appliance::ApplianceInputDefinition input;
			input.id = slugifyId(name);
			input.label = name;
			input.kind = kindFromRemoteKey(value.get<std::string>());
			input.triggerMode = appliance::InputTriggerMode::Pulse;
			input.triggerCommands.push_back(
				makeTransportCommand("remoteKey", value.get<std::string>()));
			upsertInput(definition.inputs, ids, std::move(input));
		}

		addDefaultTizenInputs(definition.inputs, ids);

		return definition;
	}

	std::vector<appliance::ApplianceDefinition> parseSamsungPlatforms(
		const nlohmann::json& root,
		const SamsungTokenData& token_data)
	{
		std::vector<appliance::ApplianceDefinition> devices;
		if (!root.contains("platforms") || !root.at("platforms").is_array())
			return devices;

		for (const auto& platform : root.at("platforms"))
		{
			if (!platform.is_object())
				continue;
			if (jsonString(platform, "platform") != "SamsungTizen")
				continue;
			if (!platform.contains("devices") || !platform.at("devices").is_array())
				continue;

			for (const auto& device : platform.at("devices"))
			{
				if (!device.is_object())
					continue;
				devices.push_back(parseSamsungDevice(platform, device, token_data));
			}
		}

		return devices;
	}

	std::vector<appliance::ApplianceDefinition> parseTuyaPlatforms(const nlohmann::json& root)
	{
		std::vector<appliance::ApplianceDefinition> devices;
		if (!root.contains("platforms") || !root.at("platforms").is_array())
			return devices;

		for (const auto& platform : root.at("platforms"))
		{
			if (!platform.is_object())
				continue;
			if (jsonString(platform, "platform") != "TuyaPlatform")
				continue;
			if (!platform.contains("options") || !platform.at("options").is_object())
				continue;

			const auto& options = platform.at("options");
			if (!options.contains("deviceOverrides") ||
				!options.at("deviceOverrides").is_array())
				continue;

			for (const auto& override_config : options.at("deviceOverrides"))
			{
				if (!override_config.is_object())
					continue;

				const std::string raw_id = jsonString(override_config, "id");
				if (raw_id.empty() || raw_id == "global")
					continue;

				appliance::ApplianceDefinition definition;
				definition.id = slugifyId(raw_id);
				definition.kind = appliance::ApplianceKind::Tuya;
				definition.name = raw_id;
				definition.transport.kind = "mqtt";
				definition.transport.endpoint = "mqtt://127.0.0.1:1883";
				definition.metadata["plugin"] = "TuyaPlatform";
				definition.metadata["category"] = jsonString(override_config, "category");

				if (override_config.contains("schema") &&
					override_config.at("schema").is_array())
				{
					for (const auto& schema : override_config.at("schema"))
					{
						if (!schema.is_object())
							continue;

						appliance::ApplianceInputDefinition input;
						input.id = slugifyId(jsonString(schema, "newCode", jsonString(schema, "code")));
						input.label = jsonString(schema, "newCode", jsonString(schema, "code"));
						input.kind = schema.value("type", "") == "Boolean"
							? appliance::ApplianceInputKind::Toggle
							: appliance::ApplianceInputKind::Command;
						input.triggerMode = schema.value("type", "") == "Boolean"
							? appliance::InputTriggerMode::Toggle
							: appliance::InputTriggerMode::Pulse;
						input.meta = schema;
						definition.inputs.push_back(std::move(input));
					}
				}

				devices.push_back(std::move(definition));
			}
		}

		return devices;
	}

	ParsedHomebridgeConfig parseHomebridgeConfigImpl(
		const std::string& json_text,
		const SamsungTokenData& token_data)
	{
		ParsedHomebridgeConfig result;
		const nlohmann::json root = nlohmann::json::parse(json_text);

		auto samsung_devices = parseSamsungPlatforms(root, token_data);
		auto tuya_devices = parseTuyaPlatforms(root);

		result.appliances.insert(
			result.appliances.end(),
			std::make_move_iterator(samsung_devices.begin()),
			std::make_move_iterator(samsung_devices.end()));
		result.appliances.insert(
			result.appliances.end(),
			std::make_move_iterator(tuya_devices.begin()),
			std::make_move_iterator(tuya_devices.end()));
		return result;
	}
} // namespace

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
	return out.empty() ? "device" : out;
}

ParsedHomebridgeConfig parseHomebridgeConfig(const std::string& json_text)
{
	return parseHomebridgeConfigImpl(json_text, SamsungTokenData {});
}

ParsedHomebridgeConfig parseHomebridgeConfigFile(const std::string& path)
{
	return parseHomebridgeConfigImpl(readFile(path), loadSamsungTokenData(path));
}

CORE_NAMESPACE_END
WAVE_NAMESPACE_END
