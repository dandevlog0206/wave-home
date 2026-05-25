#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "appliance/command_transport.h"
#include "core/coredef.h"

WAVE_NAMESPACE_BEGIN
MQTT_NAMESPACE_BEGIN

struct Broker
{
	std::string host = "127.0.0.1";
	uint16_t port = 1883;
	std::string clientId = "wave-home";
	uint16_t keepAliveSec = 30;

	std::string endpoint() const;
};

Broker parseBrokerUrl(const std::string& url);

class PersistentMqttTransport final : public appliance::ICommandTransport
{
public:
	explicit PersistentMqttTransport(Broker broker);
	~PersistentMqttTransport() override;

	PersistentMqttTransport(const PersistentMqttTransport&) = delete;
	PersistentMqttTransport& operator=(const PersistentMqttTransport&) = delete;

	bool publish(const std::string& channel, const std::string& payload) override;
	appliance::TransportConnectionState connectionState() const override;
	std::string lastError() const override;
	nlohmann::json debugJson() const override;

	const Broker& broker() const { return m_broker; }

private:
	bool connectLocked();
	bool publishLocked(const std::string& channel, const std::string& payload);
	void disconnectLocked();
	std::chrono::milliseconds nextBackoff();

	Broker m_broker;
	mutable std::mutex m_mutex;
	int m_fd = -1;
	appliance::TransportConnectionState m_state =
		appliance::TransportConnectionState::Disconnected;
	std::string m_lastError;
	uint32_t m_retryCount = 0;
	std::chrono::steady_clock::time_point m_nextRetryAt {};
};

MQTT_NAMESPACE_END
WAVE_NAMESPACE_END