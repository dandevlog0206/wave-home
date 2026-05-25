#pragma once

#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "core/coredef.h"

WAVE_NAMESPACE_BEGIN
APPLIANCE_NAMESPACE_BEGIN

enum class TransportConnectionState
{
	Disconnected,
	Connecting,
	Connected,
	Error
};

class ICommandTransport
{
public:
	virtual ~ICommandTransport() = default;

	virtual bool publish(const std::string& channel, const std::string& payload) = 0;
	virtual TransportConnectionState connectionState() const = 0;
	virtual std::string lastError() const = 0;
	virtual nlohmann::json debugJson() const = 0;
};

using CommandTransportPtr = std::shared_ptr<ICommandTransport>;

std::string connectionStateName(TransportConnectionState state);

APPLIANCE_NAMESPACE_END
WAVE_NAMESPACE_END
