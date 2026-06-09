#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "appliance/command_transport.h"
#include "core/coredef.h"

WAVE_NAMESPACE_BEGIN
APPLIANCE_NAMESPACE_BEGIN

class TizenCommandTransport final : public ICommandTransport
{
public:
	TizenCommandTransport(std::string endpoint, nlohmann::json options);
	~TizenCommandTransport() override;

	TizenCommandTransport(const TizenCommandTransport&) = delete;
	TizenCommandTransport& operator=(const TizenCommandTransport&) = delete;

	bool publish(const std::string& channel, const std::string& payload) override;
	void primeConnection() override;
	TransportConnectionState connectionState() const override;
	std::string lastError() const override;
	nlohmann::json debugJson() const override;

private:
	struct Session;

	bool sessionDeadLocked() const;
	bool ensureSessionLocked(std::string* error);
	bool trySendRemotePayloadLocked(const std::string& payload, std::string* error);
	bool sendRemotePayload(const std::string& payload, std::string* error);
	bool waitForSessionReady(std::string* error);
	void closeSessionLocked();
	bool drainInboundLocked(std::string* error);

	bool sendRemoteKey(const std::string& key);
	bool sendPowerCommand(const std::string& payload);
	bool sendAppCommand(const std::string& app_id);
	bool sendSourceCommand(const std::string& source);
	bool sendArtCommand(const std::string& payload);
	bool wakeOnLan();

	void setErrorLocked(const std::string& error);
	void startSessionWatch();
	void stopSessionWatch();
	void sessionWatchLoop();
	void maintainSessionLocked(std::string* error);

	std::string m_endpoint;
	nlohmann::json m_options;
	mutable std::mutex m_mutex;
	std::unique_ptr<Session> m_session;
	TransportConnectionState m_state = TransportConnectionState::Disconnected;
	std::string m_lastError;
	std::thread m_watch_thread;
	std::atomic<bool> m_watch_stop {false};
};

APPLIANCE_NAMESPACE_END
WAVE_NAMESPACE_END
