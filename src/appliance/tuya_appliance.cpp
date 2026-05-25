#include "appliance/tuya_appliance.h"

WAVE_NAMESPACE_BEGIN
APPLIANCE_NAMESPACE_BEGIN

TuyaAppliance::TuyaAppliance(
	ApplianceDefinition definition,
	CommandTransportPtr transport) :
	Appliance(std::move(definition), std::move(transport))
{
}

std::string TuyaAppliance::typeLabel(const std::string_view) const
{
	return "Tuya";
}

APPLIANCE_NAMESPACE_END
WAVE_NAMESPACE_END
