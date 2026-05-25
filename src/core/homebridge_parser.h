#pragma once

#include <string>
#include <vector>

#include "appliance/appliance.h"
#include "core/coredef.h"

WAVE_NAMESPACE_BEGIN
CORE_NAMESPACE_BEGIN

struct ParsedHomebridgeConfig
{
	std::vector<appliance::ApplianceDefinition> appliances;
};

ParsedHomebridgeConfig parseHomebridgeConfig(const std::string& json_text);
ParsedHomebridgeConfig parseHomebridgeConfigFile(const std::string& path);

std::string slugifyId(const std::string& name);

CORE_NAMESPACE_END
WAVE_NAMESPACE_END
