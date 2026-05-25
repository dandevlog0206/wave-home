#include "appliance/mqtt_transport.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <vector>

WAVE_NAMESPACE_BEGIN
MQTT_NAMESPACE_BEGIN

namespace
{
	void appendUtf8String(std::vector<uint8_t>& out, const std::string& s)
	{
		const uint16_t len = htons(static_cast<uint16_t>(s.size()));
		out.push_back(static_cast<uint8_t>(len >> 8));
		out.push_back(static_cast<uint8_t>(len & 0xff));
		out.insert(out.end(), s.begin(), s.end());
	}

	void encodeRemainingLength(uint32_t length, std::vector<uint8_t>& out)
	{
		do
		{
			uint8_t encoded = static_cast<uint8_t>(length % 128);
			length /= 128;
			if (length > 0)
				encoded |= 0x80;
			out.push_back(encoded);
		} while (length > 0);
	}

	bool sendAll(const int fd, const uint8_t* data, const size_t len)
	{
		size_t sent = 0;
		while (sent < len)
		{
			const ssize_t n = ::send(fd, data + sent, len - sent, MSG_NOSIGNAL);
			if (n <= 0)
				return false;
			sent += static_cast<size_t>(n);
		}
		return true;
	}

	int connectTcp(const std::string& host, const uint16_t port)
	{
		addrinfo hints {};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;

		addrinfo* result = nullptr;
		const std::string port_str = std::to_string(port);
		if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result) != 0)
			return -1;

		int fd = -1;
		for (addrinfo* it = result; it != nullptr; it = it->ai_next)
		{
			fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
			if (fd < 0)
				continue;
			if (::connect(fd, it->ai_addr, it->ai_addrlen) == 0)
				break;
			::close(fd);
			fd = -1;
		}

		freeaddrinfo(result);
		return fd;
	}

	bool mqttConnect(const int fd, const Broker& broker)
	{
		std::vector<uint8_t> payload;
		appendUtf8String(payload, "MQTT");
		payload.push_back(4);
		payload.push_back(0x02);
		payload.push_back(static_cast<uint8_t>(broker.keepAliveSec >> 8));
		payload.push_back(static_cast<uint8_t>(broker.keepAliveSec & 0xff));
		appendUtf8String(payload, broker.clientId);

		std::vector<uint8_t> packet;
		packet.push_back(0x10);
		encodeRemainingLength(static_cast<uint32_t>(payload.size()), packet);
		packet.insert(packet.end(), payload.begin(), payload.end());

		if (!sendAll(fd, packet.data(), packet.size()))
			return false;

		uint8_t header[2] {};
		if (::recv(fd, header, sizeof(header), 0) != static_cast<ssize_t>(sizeof(header)))
			return false;
		if (header[0] != 0x20 || header[1] != 0x02)
			return false;

		uint8_t ack[2] {};
		if (::recv(fd, ack, sizeof(ack), 0) != static_cast<ssize_t>(sizeof(ack)))
			return false;
		return ack[1] == 0x00;
	}

	bool mqttPublish(const int fd, const std::string& topic, const std::string& body)
	{
		std::vector<uint8_t> payload;
		appendUtf8String(payload, topic);
		payload.insert(payload.end(), body.begin(), body.end());

		std::vector<uint8_t> packet;
		packet.push_back(0x30);
		encodeRemainingLength(static_cast<uint32_t>(payload.size()), packet);
		packet.insert(packet.end(), payload.begin(), payload.end());
		return sendAll(fd, packet.data(), packet.size());
	}
} // namespace

std::string Broker::endpoint() const
{
	return "mqtt://" + host + ":" + std::to_string(port);
}

Broker parseBrokerUrl(const std::string& url)
{
	Broker broker;
	std::string normalized = url;
	if (normalized.rfind("mqtt://", 0) == 0)
		normalized = normalized.substr(7);
	else if (normalized.rfind("mqtts://", 0) == 0)
		normalized = normalized.substr(8);

	const size_t colon = normalized.find(':');
	if (colon == std::string::npos)
	{
		broker.host = normalized;
		return broker;
	}

	broker.host = normalized.substr(0, colon);
	try
	{
		broker.port = static_cast<uint16_t>(std::stoul(normalized.substr(colon + 1)));
	}
	catch (...)
	{
		broker.port = 1883;
	}
	return broker;
}

PersistentMqttTransport::PersistentMqttTransport(Broker broker) :
	m_broker(std::move(broker))
{
}

PersistentMqttTransport::~PersistentMqttTransport()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	disconnectLocked();
}

std::chrono::milliseconds PersistentMqttTransport::nextBackoff()
{
	const auto clamped = std::min<uint32_t>(m_retryCount, 5);
	const auto delay = 250u * (1u << clamped);
	++m_retryCount;
	return std::chrono::milliseconds(delay);
}

void PersistentMqttTransport::disconnectLocked()
{
	if (m_fd >= 0)
		::close(m_fd);
	m_fd = -1;
	if (m_state != appliance::TransportConnectionState::Error)
		m_state = appliance::TransportConnectionState::Disconnected;
}

bool PersistentMqttTransport::connectLocked()
{
	const auto now = std::chrono::steady_clock::now();
	if (m_fd >= 0)
		return true;
	if (m_nextRetryAt.time_since_epoch().count() != 0 && now < m_nextRetryAt)
		return false;

	m_state = appliance::TransportConnectionState::Connecting;

	const int fd = connectTcp(m_broker.host, m_broker.port);
	if (fd < 0)
	{
		m_lastError = "tcp connect failed";
		m_state = appliance::TransportConnectionState::Error;
		m_nextRetryAt = now + nextBackoff();
		return false;
	}

	if (!mqttConnect(fd, m_broker))
	{
		::close(fd);
		m_lastError = "mqtt connect failed";
		m_state = appliance::TransportConnectionState::Error;
		m_nextRetryAt = now + nextBackoff();
		return false;
	}

	m_fd = fd;
	m_retryCount = 0;
	m_nextRetryAt = {};
	m_lastError.clear();
	m_state = appliance::TransportConnectionState::Connected;
	return true;
}

bool PersistentMqttTransport::publishLocked(
	const std::string& channel,
	const std::string& payload)
{
	if (channel.empty())
	{
		m_lastError = "empty channel";
		return false;
	}
	return mqttPublish(m_fd, channel, payload);
}

bool PersistentMqttTransport::publish(const std::string& channel, const std::string& payload)
{
	for (int attempt = 0; attempt < 2; ++attempt)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!connectLocked())
			continue;
		if (publishLocked(channel, payload))
		{
			m_lastError.clear();
			m_state = appliance::TransportConnectionState::Connected;
			return true;
		}

		m_lastError = "mqtt publish failed";
		m_state = appliance::TransportConnectionState::Error;
		disconnectLocked();
		m_nextRetryAt = std::chrono::steady_clock::now() + nextBackoff();
	}

	return false;
}

appliance::TransportConnectionState PersistentMqttTransport::connectionState() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_state;
}

std::string PersistentMqttTransport::lastError() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_lastError;
}

nlohmann::json PersistentMqttTransport::debugJson() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return {
		{"kind", "mqtt"},
		{"endpoint", m_broker.endpoint()},
		{"state", appliance::connectionStateName(m_state)},
		{"lastError", m_lastError},
	};
}

MQTT_NAMESPACE_END
WAVE_NAMESPACE_END
