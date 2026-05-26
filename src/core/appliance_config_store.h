#pragma once

#include <filesystem>
#include <vector>

#include "appliance/appliance.h"
#include "core/coredef.h"

WAVE_NAMESPACE_BEGIN
CORE_NAMESPACE_BEGIN

struct ApplianceConfigDocument
{
	std::vector<appliance::ApplianceDefinition> appliances;
};

class ApplianceConfigStore
{
public:
	explicit ApplianceConfigStore(std::filesystem::path path);

	const std::filesystem::path& path() const { return m_path; }

	ApplianceConfigDocument load() const;

private:
	void ensureExists() const;

	std::filesystem::path m_path;
};

CORE_NAMESPACE_END
WAVE_NAMESPACE_END
