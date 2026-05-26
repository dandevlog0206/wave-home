#include "appliance/appliance_manager.h"

#include <utility>

#include "appliance/mqtt_transport.h"
#include "appliance/tizen_appliance.h"
#include "appliance/tizen_transport.h"
#include "appliance/tuya_appliance.h"

WAVE_NAMESPACE_BEGIN
APPLIANCE_NAMESPACE_BEGIN

namespace
{
	std::shared_ptr<Appliance> makeApplianceInstance(
		const ApplianceDefinition& definition,
		const CommandTransportPtr& transport)
	{
		switch (definition.kind)
		{
		case ApplianceKind::Tizen:
			return std::make_shared<TizenAppliance>(definition, transport);
		case ApplianceKind::Tuya:
			return std::make_shared<TuyaAppliance>(definition, transport);
		case ApplianceKind::Unknown:
		default:
			return nullptr;
		}
	}

	CommandTransportPtr makeCommandTransport(const ApplianceTransportConfig& config)
	{
		if (config.kind == "mqtt")
		{
			return std::make_shared<mqtt::PersistentMqttTransport>(
				mqtt::parseBrokerUrl(config.endpoint));
		}
		if (config.kind == "tizen")
		{
			return std::make_shared<TizenCommandTransport>(
				config.endpoint,
				config.options);
		}
		return nullptr;
	}
} // namespace

void ApplianceManager::loadFromDefinitions(std::vector<ApplianceDefinition> definitions)
{
	applyDefinitions(std::move(definitions));
}

void ApplianceManager::primeConnections()
{
	std::vector<CommandTransportPtr> transports;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		transports.reserve(m_transports.size());
		for (const auto& [cache_key, transport] : m_transports)
		{
			(void)cache_key;
			if (transport)
				transports.push_back(transport);
		}
	}

	for (const auto& transport : transports)
		transport->primeConnection();
}

CommandTransportPtr ApplianceManager::getOrCreateTransport(const ApplianceTransportConfig& config)
{
	const std::string cache_key = config.kind + ":" + config.endpoint;
	const auto it = m_transports.find(cache_key);
	if (it != m_transports.end())
		return it->second;

	auto transport = makeCommandTransport(config);
	if (transport)
		m_transports[cache_key] = transport;
	return transport;
}

void ApplianceManager::applyDefinitions(std::vector<ApplianceDefinition> definitions)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	std::unordered_map<std::string, std::shared_ptr<Appliance>> next_appliances;
	for (const auto& definition : definitions)
	{
		auto transport = getOrCreateTransport(definition.transport);
		auto appliance = makeApplianceInstance(definition, transport);
		if (appliance)
			next_appliances[definition.id] = std::move(appliance);
	}

	m_appliances = std::move(next_appliances);
}

std::shared_ptr<Appliance> ApplianceManager::snapshotAppliance(const std::string& appliance_id) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto it = m_appliances.find(appliance_id);
	return it != m_appliances.end() ? it->second : nullptr;
}

size_t ApplianceManager::applianceCount() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_appliances.size();
}

nlohmann::json ApplianceManager::appliancesJson(const std::string_view locale_tag) const
{
	nlohmann::json items = nlohmann::json::array();
	std::lock_guard<std::mutex> lock(m_mutex);
	for (const auto& [id, appliance] : m_appliances)
	{
		(void)id;
		items.push_back(appliance->toDeviceJson(locale_tag));
	}
	return {{"items", items}};
}

bool ApplianceManager::executeInput(
	const std::string& appliance_id,
	const std::string& input_id,
	const std::optional<InputTriggerMode> trigger_mode_override,
	std::string* error) const
{
	auto appliance = snapshotAppliance(appliance_id);
	if (!appliance)
	{
		if (error)
			*error = "device not found";
		return false;
	}
	return appliance->executeInput(input_id, trigger_mode_override, error);
}

bool ApplianceManager::hasAppliance(const std::string& appliance_id) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_appliances.find(appliance_id) != m_appliances.end();
}

bool ApplianceManager::hasInput(
	const std::string& appliance_id,
	const std::string& input_id) const
{
	auto appliance = snapshotAppliance(appliance_id);
	return appliance ? appliance->hasInput(input_id) : false;
}

std::string ApplianceManager::applianceName(const std::string& appliance_id) const
{
	auto appliance = snapshotAppliance(appliance_id);
	return appliance ? appliance->name() : appliance_id;
}

std::string ApplianceManager::inputLabel(
	const std::string& appliance_id,
	const std::string& input_id,
	const std::string_view locale_tag) const
{
	auto appliance = snapshotAppliance(appliance_id);
	return appliance ? appliance->inputLabel(input_id, locale_tag) : input_id;
}

APPLIANCE_NAMESPACE_END
WAVE_NAMESPACE_END
