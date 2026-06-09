#pragma once

#include <functional>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "appliance/command_transport.h"
#include "appliance/tuya_protocol.h"
#include "core/coredef.h"

WAVE_NAMESPACE_BEGIN
APPLIANCE_NAMESPACE_BEGIN

class TuyaCommandTransport final : public ICommandTransport
{
public:
	TuyaCommandTransport(std::string endpoint, nlohmann::json options);
	~TuyaCommandTransport() override;

	TuyaCommandTransport(const TuyaCommandTransport&) = delete;
	TuyaCommandTransport& operator=(const TuyaCommandTransport&) = delete;

	bool publish(const std::string& channel, const std::string& payload) override;
	void primeConnection() override;
	TransportConnectionState connectionState() const override;
	std::string lastError() const override;
	nlohmann::json debugJson() const override;

private:
	bool sendPowerCommand(const std::string& payload, std::string* error);
	bool sendDpsCommand(const std::string& payload, std::string* error);
	bool sendQueryCommand(std::string* error);
	bool runClient(
		const std::function<bool(TuyaLanClient&, std::string*)>& action,
		std::string* error);

	void setErrorLocked(const std::string& error);

	TuyaEndpointConfig m_config;
	std::string m_endpoint;
	mutable std::mutex m_mutex;
	mutable std::mutex m_command_mutex;
	TransportConnectionState m_state = TransportConnectionState::Disconnected;
	std::string m_lastError;
	std::string m_lastStatusJson;
};

APPLIANCE_NAMESPACE_END
WAVE_NAMESPACE_END
