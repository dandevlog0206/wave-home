#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "appliance/command_transport.h"
#include "core/coredef.h"
#include "core/locale.h"

WAVE_NAMESPACE_BEGIN
APPLIANCE_NAMESPACE_BEGIN

enum class ApplianceKind
{
	Unknown,
	Tizen,
	Tuya
};

enum class ApplianceInputKind
{
	Command,
	Power,
	Volume,
	Channel,
	Navigation,
	App,
	Source,
	Toggle
};

enum class InputTriggerMode
{
	Pulse,
	Toggle
};

struct ApplianceCommand
{
	std::string channel;
	std::string payload;
};

struct ApplianceTransportConfig
{
	std::string kind = "mqtt";
	std::string endpoint = "mqtt://127.0.0.1:1883";
	nlohmann::json options;
};

struct ApplianceInputDefinition
{
	std::string id;
	std::string label;
	std::string labelKey;
	ApplianceInputKind kind = ApplianceInputKind::Command;
	InputTriggerMode triggerMode = InputTriggerMode::Pulse;
	std::vector<ApplianceCommand> triggerCommands;
	std::vector<ApplianceCommand> onCommands;
	std::vector<ApplianceCommand> offCommands;
	nlohmann::json meta;
};

struct ApplianceDefinition
{
	std::string id;
	ApplianceKind kind = ApplianceKind::Unknown;
	std::string name;
	std::string room;
	std::string typeLabelKey;
	ApplianceTransportConfig transport;
	std::vector<ApplianceInputDefinition> inputs;
	nlohmann::json metadata;
};

class Appliance
{
public:
	Appliance(ApplianceDefinition definition, CommandTransportPtr transport);
	virtual ~Appliance() = default;

	Appliance(const Appliance&) = delete;
	Appliance& operator=(const Appliance&) = delete;

	virtual ApplianceKind kind() const = 0;

	const std::string& id() const { return m_definition.id; }
	const std::string& name() const { return m_definition.name; }
	const ApplianceTransportConfig& transportConfig() const { return m_definition.transport; }
	const std::vector<ApplianceInputDefinition>& inputs() const { return m_definition.inputs; }

	virtual void reconfigure(const ApplianceDefinition& definition);

	bool executeInput(
		const std::string& input_id,
		std::optional<InputTriggerMode> trigger_mode_override = std::nullopt,
		std::string* error = nullptr);

	bool hasInput(const std::string& input_id) const;

	std::string inputLabel(
		const std::string& input_id,
		std::string_view locale_tag) const;

	nlohmann::json toDeviceJson(std::string_view locale_tag) const;

protected:
	virtual std::string typeLabel(std::string_view locale_tag) const = 0;

	virtual nlohmann::json buildTransportJson() const;
	virtual bool executeInputInternal(
		ApplianceInputDefinition& input,
		std::optional<InputTriggerMode> trigger_mode_override,
		std::string* error);

	bool publishCommands(
		const std::vector<ApplianceCommand>& commands,
		std::string* error) const;

	std::string resolveLabel(
		const ApplianceInputDefinition& input,
		std::string_view locale_tag) const;

	static std::string inputKindName(ApplianceInputKind kind);
	static std::string triggerModeName(InputTriggerMode mode);

	mutable std::mutex m_mutex;
	ApplianceDefinition m_definition;
	CommandTransportPtr m_transport;
	std::unordered_map<std::string, bool> m_toggleStates;
};

APPLIANCE_NAMESPACE_END
WAVE_NAMESPACE_END
