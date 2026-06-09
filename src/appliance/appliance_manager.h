#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "appliance/appliance.h"
#include "core/coredef.h"

WAVE_NAMESPACE_BEGIN

APPLIANCE_NAMESPACE_BEGIN

class ApplianceManager
{
public:
	void loadFromDefinitions(std::vector<ApplianceDefinition> definitions);
	void primeConnections();

	size_t applianceCount() const;
	nlohmann::json appliancesJson(std::string_view locale_tag) const;

	bool executeInput(
		const std::string& appliance_id,
		const std::string& input_id,
		std::optional<InputTriggerMode> trigger_mode_override = std::nullopt,
		std::string* error = nullptr) const;

	bool hasAppliance(const std::string& appliance_id) const;
	bool isTizenAppliance(const std::string& appliance_id) const;
	bool hasInput(const std::string& appliance_id, const std::string& input_id) const;
	std::string applianceName(const std::string& appliance_id) const;
	std::string inputLabel(
		const std::string& appliance_id,
		const std::string& input_id,
		std::string_view locale_tag) const;

private:
	void applyDefinitions(std::vector<ApplianceDefinition> definitions);
	CommandTransportPtr getOrCreateTransport(const ApplianceTransportConfig& config);
	std::shared_ptr<Appliance> snapshotAppliance(const std::string& appliance_id) const;

	mutable std::mutex m_mutex;
	std::unordered_map<std::string, std::shared_ptr<Appliance>> m_appliances;
	std::unordered_map<std::string, CommandTransportPtr> m_transports;
};

APPLIANCE_NAMESPACE_END
WAVE_NAMESPACE_END
