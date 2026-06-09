#include "appliance/tuya_transport.h"

#include <algorithm>
#include <cctype>
#include <functional>

#include "core/coredef.h"

WAVE_NAMESPACE_BEGIN
APPLIANCE_NAMESPACE_BEGIN

namespace
{
	[[nodiscard]] std::string trimCopy(std::string value)
	{
		const auto not_space = [](const unsigned char ch) { return !std::isspace(ch); };
		value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
		value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
		return value;
	}

	[[nodiscard]] std::string upperCopy(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
			return static_cast<char>(std::toupper(ch));
		});
		return value;
	}

	[[nodiscard]] std::string getStringOption(
		const nlohmann::json& options,
		const char* key,
		const std::string& fallback = {})
	{
		if (!options.contains(key) || !options.at(key).is_string())
			return fallback;
		return options.at(key).get<std::string>();
	}

	[[nodiscard]] int getIntOption(
		const nlohmann::json& options,
		const char* key,
		const int fallback)
	{
		if (!options.contains(key) || !options.at(key).is_number_integer())
			return fallback;
		return options.at(key).get<int>();
	}

	[[nodiscard]] TuyaEndpointConfig parseConfig(
		const std::string& endpoint,
		const nlohmann::json& options)
	{
		TuyaEndpointConfig config;
		config.host = getStringOption(options, "ip");
		config.deviceId = getStringOption(options, "deviceId");
		if (config.deviceId.empty())
			config.deviceId = getStringOption(options, "id");
		config.localKey = getStringOption(options, "localKey");
		config.protocolVersion = getStringOption(options, "version", "3.3");
		config.switchDp = getStringOption(options, "switchDp", "1");
		config.port = static_cast<uint16_t>(std::max(1, getIntOption(options, "port", 6668)));
		config.timeoutMs = std::max(500, getIntOption(options, "timeoutMs", 3000));
		config.receiveMinBytes = std::max(16, getIntOption(options, "receiveMinBytes", 30));

		if (!config.host.empty())
			return config;

		std::string host = endpoint;
		const auto scheme_pos = host.find("://");
		if (scheme_pos != std::string::npos)
			host = host.substr(scheme_pos + 3);
		const auto slash_pos = host.find('/');
		if (slash_pos != std::string::npos)
			host = host.substr(0, slash_pos);
		if (host.rfind("tuya://", 0) == 0)
			host = host.substr(7);
		config.host = host;
		return config;
	}
} // namespace

TuyaCommandTransport::TuyaCommandTransport(std::string endpoint, nlohmann::json options) :
	m_endpoint(std::move(endpoint)),
	m_config(parseConfig(m_endpoint, options))
{
}

TuyaCommandTransport::~TuyaCommandTransport() = default;

bool TuyaCommandTransport::runClient(
	const std::function<bool(TuyaLanClient&, std::string*)>& action,
	std::string* error)
{
	std::lock_guard<std::mutex> command_lock(m_command_mutex);
	TuyaLanClient client(m_config);
	return action(client, error);
}

bool TuyaCommandTransport::sendQueryCommand(std::string* error)
{
	std::string decoded;
	const bool ok = runClient(
		[&](TuyaLanClient& client, std::string* client_error) {
			return client.queryStatus(&decoded, client_error);
		},
		error);
	if (ok)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_lastStatusJson = std::move(decoded);
	}
	return ok;
}

bool TuyaCommandTransport::sendPowerCommand(
	const std::string& payload,
	std::string* error)
{
	const std::string command = upperCopy(trimCopy(payload));
	if (command.empty())
	{
		if (error)
			*error = "missing power command";
		return false;
	}

	if (command == "ON")
	{
		return runClient(
			[&](TuyaLanClient& client, std::string* client_error) {
				std::string decoded;
				return client.setSwitch(true, &decoded, client_error);
			},
			error);
	}

	if (command == "OFF")
	{
		return runClient(
			[&](TuyaLanClient& client, std::string* client_error) {
				std::string decoded;
				return client.setSwitch(false, &decoded, client_error);
			},
			error);
	}

	if (command == "TOGGLE")
	{
		std::string status_json;
		if (!runClient(
				[&](TuyaLanClient& client, std::string* client_error) {
					return client.queryStatus(&status_json, client_error);
				},
				error))
			return false;

		const auto current = tuya_protocol::parseSwitchDp(status_json, m_config.switchDp);
		if (!current.has_value())
		{
			if (error)
				*error = "failed to read current switch state";
			return false;
		}

		return runClient(
			[&](TuyaLanClient& client, std::string* client_error) {
				std::string decoded;
				return client.setSwitch(!*current, &decoded, client_error);
			},
			error);
	}

	if (error)
		*error = "unsupported power command: " + command;
	return false;
}

bool TuyaCommandTransport::sendDpsCommand(const std::string& payload, std::string* error)
{
	const std::string trimmed = trimCopy(payload);
	if (trimmed.empty())
	{
		if (error)
			*error = "missing dps payload";
		return false;
	}

	try
	{
		nlohmann::json parsed = nlohmann::json::parse(trimmed);
		nlohmann::json dps;
		if (parsed.contains("dps") && parsed.at("dps").is_object())
			dps = parsed.at("dps");
		else if (parsed.is_object())
			dps = parsed;
		else
		{
			if (error)
				*error = "dps payload must be a JSON object";
			return false;
		}

		return runClient(
			[&](TuyaLanClient& client, std::string* client_error) {
				std::string decoded;
				return client.setDps(dps, &decoded, client_error);
			},
			error);
	}
	catch (const std::exception& ex)
	{
		if (error)
			*error = std::string("invalid dps json: ") + ex.what();
		return false;
	}
}

bool TuyaCommandTransport::publish(const std::string& channel, const std::string& payload)
{
	std::string error;
	bool ok = false;
	if (channel == "power")
		ok = sendPowerCommand(payload, &error);
	else if (channel == "dps")
		ok = sendDpsCommand(payload, &error);
	else if (channel == "query")
		ok = sendQueryCommand(&error);
	else
		error = "unsupported tuya command channel: " + channel;

	std::lock_guard<std::mutex> lock(m_mutex);
	if (ok)
	{
		m_state = TransportConnectionState::Connected;
		m_lastError.clear();
	}
	else
	{
		setErrorLocked(error.empty() ? "tuya command failed" : error);
		if (m_state != TransportConnectionState::Error)
			m_state = TransportConnectionState::Disconnected;
	}
	return ok;
}

void TuyaCommandTransport::primeConnection()
{
	std::string decoded;
	std::string error;
	const bool ok = runClient(
		[&](TuyaLanClient& client, std::string* client_error) {
			return client.queryStatus(&decoded, client_error);
		},
		&error);

	std::lock_guard<std::mutex> lock(m_mutex);
	m_lastError.clear();
	if (ok)
	{
		m_lastStatusJson = std::move(decoded);
		m_state = TransportConnectionState::Connected;
	}
	else
	{
		setErrorLocked(error);
		if (m_state != TransportConnectionState::Error)
			m_state = TransportConnectionState::Disconnected;
	}
}

TransportConnectionState TuyaCommandTransport::connectionState() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_state;
}

std::string TuyaCommandTransport::lastError() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_lastError;
}

nlohmann::json TuyaCommandTransport::debugJson() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return {
		{"connection", connectionStateName(m_state)},
		{"endpoint", m_endpoint},
		{"host", m_config.host},
		{"port", m_config.port},
		{"deviceId", m_config.deviceId},
		{"protocolVersion", m_config.protocolVersion},
		{"switchDp", m_config.switchDp},
		{"localKeyPresent", !m_config.localKey.empty()},
		{"lastStatus", m_lastStatusJson},
		{"lastError", m_lastError},
	};
}

void TuyaCommandTransport::setErrorLocked(const std::string& error)
{
	m_state = TransportConnectionState::Error;
	m_lastError = error.empty() ? "unknown tuya transport error" : error;
}

APPLIANCE_NAMESPACE_END
WAVE_NAMESPACE_END
