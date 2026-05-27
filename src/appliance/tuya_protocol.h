#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/coredef.h"

WAVE_NAMESPACE_BEGIN
APPLIANCE_NAMESPACE_BEGIN

struct TuyaEndpointConfig
{
	std::string host;
	uint16_t port = 6668;
	std::string deviceId;
	std::string localKey;
	std::string protocolVersion = "3.3";
	std::string switchDp = "1";
	int timeoutMs = 3000;
	int receiveMinBytes = 30;
};

class TuyaLanClient
{
public:
	explicit TuyaLanClient(TuyaEndpointConfig config);

	bool isConnected() const { return m_connected; }
	void disconnect();

	bool queryStatus(std::string* decoded_json, std::string* error);
	bool setSwitch(bool on, std::string* decoded_json, std::string* error);
	bool setDps(const nlohmann::json& dps, std::string* decoded_json, std::string* error);

private:
	bool transact(
		const std::vector<uint8_t>& request,
		std::vector<uint8_t>& response,
		std::string* error);

	std::vector<uint8_t> encryptionKey() const;

	TuyaEndpointConfig m_config;
	bool m_connected = false;
	uint32_t m_sequence = 0;
};

namespace tuya_protocol
{
	std::string generateDpQueryPayload(const std::string& device_id);
	std::string generateControlPayload(
		const std::string& device_id,
		const nlohmann::json& dps);
	std::vector<uint8_t> buildMessage(
		uint8_t command,
		const std::string& payload_json,
		const std::vector<uint8_t>& encryption_key,
		uint32_t& sequence);
	std::string decodeMessage(
		const std::vector<uint8_t>& buffer,
		const std::vector<uint8_t>& encryption_key);
	std::optional<bool> parseSwitchDp(
		const std::string& decoded_json,
		const std::string& switch_dp);
} // namespace tuya_protocol

APPLIANCE_NAMESPACE_END
WAVE_NAMESPACE_END
