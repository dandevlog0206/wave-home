#pragma once

#include "appliance/appliance.h"

#include "core/coredef.h"

WAVE_NAMESPACE_BEGIN
APPLIANCE_NAMESPACE_BEGIN

class TizenAppliance final : public Appliance
{
public:
	TizenAppliance(ApplianceDefinition definition, CommandTransportPtr transport);

	ApplianceKind kind() const override { return ApplianceKind::Tizen; }

protected:
	std::string typeLabel(std::string_view locale_tag) const override;
};

APPLIANCE_NAMESPACE_END
WAVE_NAMESPACE_END
