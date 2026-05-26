#include "appliance/appliance.h"

#include <utility>

WAVE_NAMESPACE_BEGIN
APPLIANCE_NAMESPACE_BEGIN

std::string connectionStateName(const TransportConnectionState state)
{
	switch (state)
	{
	case TransportConnectionState::Connected:
		return "online";
	case TransportConnectionState::Connecting:
		return "connecting";
	case TransportConnectionState::Error:
	case TransportConnectionState::Disconnected:
	default:
		return "offline";
	}
}

Appliance::Appliance(ApplianceDefinition definition, CommandTransportPtr transport) :
	m_definition(std::move(definition)),
	m_transport(std::move(transport))
{
}

void Appliance::reconfigure(const ApplianceDefinition& definition)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_definition = definition;
}

bool Appliance::publishCommands(
	const std::vector<ApplianceCommand>& commands,
	std::string* error) const
{
	if (!m_transport)
	{
		if (error)
			*error = "missing transport";
		return false;
	}

	for (const auto& command : commands)
	{
		if (command.channel.empty())
			continue;
		if (!m_transport->publish(command.channel, command.payload))
		{
			if (error)
				*error = m_transport->lastError();
			return false;
		}
	}

	return true;
}

bool Appliance::executeInputInternal(
	ApplianceInputDefinition& input,
	const std::optional<InputTriggerMode> trigger_mode_override,
	std::string* error)
{
	const InputTriggerMode mode = trigger_mode_override.value_or(input.triggerMode);
	if (mode == InputTriggerMode::Toggle)
	{
		const bool next_state = !m_toggleStates[input.id];
		const auto& commands = next_state && !input.onCommands.empty()
			? input.onCommands
			: (!next_state && !input.offCommands.empty()
				? input.offCommands
				: input.triggerCommands);
		const bool ok = publishCommands(commands, error);
		if (ok)
			m_toggleStates[input.id] = next_state;
		return ok;
	}

	if (!input.triggerCommands.empty())
		return publishCommands(input.triggerCommands, error);
	if (!input.onCommands.empty())
		return publishCommands(input.onCommands, error);
	return publishCommands(input.offCommands, error);
}

bool Appliance::executeInput(
	const std::string& input_id,
	const std::optional<InputTriggerMode> trigger_mode_override,
	std::string* error)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	for (auto& input : m_definition.inputs)
	{
		if (input.id == input_id)
			return executeInputInternal(input, trigger_mode_override, error);
	}

	if (error)
		*error = "input not found";
	return false;
}

bool Appliance::hasInput(const std::string& input_id) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	for (const auto& input : m_definition.inputs)
	{
		if (input.id == input_id)
			return true;
	}
	return false;
}

std::string Appliance::resolveLabel(
	const ApplianceInputDefinition& input,
	const std::string_view locale_tag) const
{
	if (!input.label.empty())
		return input.label;
	if (!input.labelKey.empty())
		return core::locale::text(locale_tag, input.labelKey);
	return input.id;
}

std::string Appliance::inputLabel(
	const std::string& input_id,
	const std::string_view locale_tag) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	for (const auto& input : m_definition.inputs)
	{
		if (input.id == input_id)
			return resolveLabel(input, locale_tag);
	}
	return input_id;
}

std::string Appliance::inputKindName(const ApplianceInputKind kind)
{
	switch (kind)
	{
	case ApplianceInputKind::Power:
		return "power";
	case ApplianceInputKind::Volume:
		return "volume";
	case ApplianceInputKind::Channel:
		return "channel";
	case ApplianceInputKind::Navigation:
		return "navigation";
	case ApplianceInputKind::App:
		return "app";
	case ApplianceInputKind::Source:
		return "source";
	case ApplianceInputKind::Toggle:
		return "toggle";
	case ApplianceInputKind::Command:
	default:
		return "command";
	}
}

std::string Appliance::triggerModeName(const InputTriggerMode mode)
{
	return mode == InputTriggerMode::Toggle ? "toggle" : "pulse";
}

nlohmann::json Appliance::buildTransportJson() const
{
	nlohmann::json redacted_options = m_definition.transport.options;
	if (redacted_options.contains("token"))
	{
		const bool has_token = redacted_options.at("token").is_string() &&
			!redacted_options.at("token").get<std::string>().empty();
		redacted_options.erase("token");
		redacted_options["tokenPresent"] = has_token;
	}

	nlohmann::json json = {
		{"kind", m_definition.transport.kind},
		{"endpoint", m_definition.transport.endpoint},
		{"options", redacted_options},
	};
	if (m_transport)
		json["state"] = m_transport->debugJson();
	return json;
}

nlohmann::json Appliance::toDeviceJson(const std::string_view locale_tag) const
{
	std::lock_guard<std::mutex> lock(m_mutex);

	nlohmann::json controls = nlohmann::json::array();
	nlohmann::json capabilities = nlohmann::json::array();
	for (const auto& input : m_definition.inputs)
	{
		const std::string label = resolveLabel(input, locale_tag);
		controls.push_back({
			{"id", input.id},
			{"label", label},
			{"triggerMode", triggerModeName(input.triggerMode)},
		});
		capabilities.push_back({
			{"id", input.id},
			{"label", label},
			{"type", inputKindName(input.kind)},
			{"triggerMode", triggerModeName(input.triggerMode)},
			{"meta", input.meta},
		});
	}

	return {
		{"id", m_definition.id},
		{"name", m_definition.name},
		{"type", typeLabel(locale_tag)},
		{"room", m_definition.room},
		{"state", core::locale::text(locale_tag, core::locale::key::kDeviceStateUnknown)},
		{"connection", m_transport ? connectionStateName(m_transport->connectionState()) : "offline"},
		{"controls", controls},
		{"capabilities", capabilities},
		{"transport", buildTransportJson()},
		{"metadata", m_definition.metadata},
		{"hasActiveBindings", false},
	};
}

APPLIANCE_NAMESPACE_END
WAVE_NAMESPACE_END
