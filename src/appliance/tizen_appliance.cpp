#include "appliance/tizen_appliance.h"

WAVE_NAMESPACE_BEGIN
APPLIANCE_NAMESPACE_BEGIN

TizenAppliance::TizenAppliance(ApplianceDefinition definition, CommandTransportPtr transport) :
	Appliance(std::move(definition),
	std::move(transport))
{
}

std::string TizenAppliance::typeLabel(const std::string_view locale_tag) const
{
	return core::locale::text(locale_tag, core::locale::key::kDeviceTypeTelevision);
}

APPLIANCE_NAMESPACE_END
WAVE_NAMESPACE_END
