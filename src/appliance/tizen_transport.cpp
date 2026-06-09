#include "appliance/tizen_transport.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <asio.hpp>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <nlohmann/json.hpp>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>

#include "core/coredef.h"

WAVE_NAMESPACE_BEGIN
APPLIANCE_NAMESPACE_BEGIN

namespace
{
	struct SocketHandle
	{
		int fd = -1;

		SocketHandle() = default;
		explicit SocketHandle(const int value) : fd(value) {}

		SocketHandle(const SocketHandle&) = delete;
		SocketHandle& operator=(const SocketHandle&) = delete;

		SocketHandle(SocketHandle&& other) noexcept : fd(other.fd)
		{
			other.fd = -1;
		}

		SocketHandle& operator=(SocketHandle&& other) noexcept
		{
			if (this != &other)
			{
				reset();
				fd = other.fd;
				other.fd = -1;
			}
			return *this;
		}

		~SocketHandle()
		{
			reset();
		}

		void reset()
		{
			if (fd >= 0)
			{
				::close(fd);
				fd = -1;
			}
		}

		[[nodiscard]] int release()
		{
			const int value = fd;
			fd = -1;
			return value;
		}
	};

	struct SslCtxDeleter
	{
		void operator()(SSL_CTX* ctx) const
		{
			if (ctx != nullptr)
				SSL_CTX_free(ctx);
		}
	};

	struct SslDeleter
	{
		void operator()(SSL* ssl) const
		{
			if (ssl != nullptr)
				SSL_free(ssl);
		}
	};

	struct TizenEndpoint
	{
		std::string host;
		uint16_t secure_port = 8002;
		uint16_t api_port = 8001;
	};

	enum class TizenPowerState
	{
		Unknown,
		On,
		Standby
	};

	struct TizenSession
	{
		std::shared_ptr<asio::io_context> io;
		std::unique_ptr<asio::ip::tcp::socket> socket;
		std::unique_ptr<SSL_CTX, SslCtxDeleter> ssl_ctx;
		std::unique_ptr<SSL, SslDeleter> ssl;
	};

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

	[[nodiscard]] TizenEndpoint parseEndpoint(
		const std::string& endpoint,
		const nlohmann::json& options)
	{
		TizenEndpoint parsed;
		parsed.host = getStringOption(options, "ip");
		parsed.secure_port = static_cast<uint16_t>(std::max(1, getIntOption(options, "securePort", 8002)));
		parsed.api_port = static_cast<uint16_t>(std::max(1, getIntOption(options, "apiPort", 8001)));

		if (!parsed.host.empty())
			return parsed;

		std::string host = endpoint;
		const auto scheme_pos = host.find("://");
		if (scheme_pos != std::string::npos)
			host = host.substr(scheme_pos + 3);

		const auto slash_pos = host.find('/');
		if (slash_pos != std::string::npos)
			host = host.substr(0, slash_pos);

		const auto colon_pos = host.find(':');
		if (colon_pos != std::string::npos)
		{
			parsed.host = host.substr(0, colon_pos);
			const std::string port_text = host.substr(colon_pos + 1);
			if (!port_text.empty())
			{
				try
				{
					parsed.secure_port = static_cast<uint16_t>(std::stoi(port_text));
				}
				catch (const std::exception&)
				{
				}
			}
		}
		else
		{
			parsed.host = host;
		}

		return parsed;
	}

	[[nodiscard]] bool asioConnect(
		asio::io_context& io,
		asio::ip::tcp::socket& socket,
		const std::string& host,
		const uint16_t port,
		const std::chrono::milliseconds timeout,
		std::string* error)
	{
		asio::ip::tcp::resolver resolver(io);
		asio::error_code resolve_ec;
		const auto endpoints = resolver.resolve(host, std::to_string(port), resolve_ec);
		if (resolve_ec)
		{
			if (error)
				*error = "tizen host resolve failed: " + resolve_ec.message();
			return false;
		}

		asio::steady_timer timer(io);
		bool connected = false;
		bool completed = false;

		timer.expires_after(timeout);
		timer.async_wait([&](const std::error_code& timer_ec) {
			if (!timer_ec && !completed)
				socket.cancel();
		});

		asio::async_connect(socket, endpoints, [&](const std::error_code& connect_ec, const asio::ip::tcp::endpoint&) {
			completed = true;
			connected = !connect_ec;
			timer.cancel();
		});

		io.restart();
		io.run();

		if (connected)
		{
			asio::error_code mode_ec;
			socket.non_blocking(false, mode_ec);
			if (mode_ec)
			{
				if (error)
					*error = "tizen socket mode setup failed: " + mode_ec.message();
				return false;
			}
		}

		if (!connected && error)
			*error = "tizen tcp connect timeout";
		return connected;
	}

	[[nodiscard]] bool asioWriteAll(
		asio::ip::tcp::socket& socket,
		const std::string& data,
		std::string* error)
	{
		asio::error_code ec;
		asio::write(socket, asio::buffer(data), ec);
		if (ec)
		{
			if (error)
				*error = "tizen tcp write failed: " + ec.message();
			return false;
		}
		return true;
	}

	[[nodiscard]] bool asioReadUntil(
		asio::io_context& io,
		asio::ip::tcp::socket& socket,
		std::string& buffer,
		const std::string& delimiter,
		const std::chrono::milliseconds timeout,
		std::string* error)
	{
		buffer.clear();
		std::array<char, 1024> chunk {};
		const auto deadline = std::chrono::steady_clock::now() + timeout;

		while (buffer.find(delimiter) == std::string::npos)
		{
			if (std::chrono::steady_clock::now() >= deadline)
			{
				if (error)
					*error = "tizen tcp read timeout";
				return false;
			}

			asio::steady_timer timer(io);
			bool completed = false;
			std::size_t received = 0;
			asio::error_code read_ec;

			timer.expires_after(std::chrono::milliseconds(250));
			timer.async_wait([&](const std::error_code& timer_ec) {
				if (!timer_ec && !completed)
					socket.cancel();
			});

			socket.async_read_some(asio::buffer(chunk), [&](const std::error_code& ec, std::size_t n) {
				completed = true;
				read_ec = ec;
				received = n;
				timer.cancel();
			});

			io.restart();
			io.run();

			if (read_ec == asio::error::operation_aborted)
				continue;
			if (read_ec && read_ec != asio::error::eof)
			{
				if (error)
					*error = "tizen tcp read failed: " + read_ec.message();
				return false;
			}
			if (received == 0)
				continue;

			buffer.append(chunk.data(), received);
		}
		return true;
	}

	[[nodiscard]] bool httpRequest(
		const std::string& host,
		const uint16_t port,
		const std::string& request,
		std::string* response,
		const int timeout_ms,
		std::string* error)
	{
		const auto timeout = std::chrono::milliseconds(std::max(500, timeout_ms));
		asio::io_context io;
		asio::ip::tcp::socket socket(io);
		if (!asioConnect(io, socket, host, port, timeout, error))
			return false;
		if (!asioWriteAll(socket, request, error))
			return false;
		return asioReadUntil(io, socket, *response, "\r\n\r\n", timeout, error);
	}

	[[nodiscard]] bool sslWriteAll(SSL* ssl, const std::string& data, std::string* error)
	{
		size_t offset = 0;
		while (offset < data.size())
		{
			const int written = ::SSL_write(ssl, data.data() + offset, static_cast<int>(data.size() - offset));
			if (written <= 0)
			{
				if (error)
					*error = ::ERR_error_string(::ERR_get_error(), nullptr);
				return false;
			}
			offset += static_cast<size_t>(written);
		}
		return true;
	}

	[[nodiscard]] bool httpRequestUntilClose(
		const std::string& host,
		const uint16_t port,
		const std::string& request,
		std::string* response,
		const int timeout_ms,
		std::string* error)
	{
		const auto timeout = std::chrono::milliseconds(std::max(500, timeout_ms));
		asio::io_context io;
		asio::ip::tcp::socket socket(io);
		if (!asioConnect(io, socket, host, port, timeout, error))
			return false;
		if (!asioWriteAll(socket, request, error))
			return false;

		response->clear();
		std::array<char, 1024> chunk {};
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (true)
		{
			if (std::chrono::steady_clock::now() >= deadline)
			{
				if (error)
					*error = "tizen http read timeout";
				return false;
			}

			asio::steady_timer timer(io);
			bool completed = false;
			std::size_t received = 0;
			asio::error_code read_ec;

			timer.expires_after(std::chrono::milliseconds(250));
			timer.async_wait([&](const std::error_code& timer_ec) {
				if (!timer_ec && !completed)
					socket.cancel();
			});

			socket.async_read_some(asio::buffer(chunk), [&](const std::error_code& ec, std::size_t n) {
				completed = true;
				read_ec = ec;
				received = n;
				timer.cancel();
			});

			io.restart();
			io.run();

			if (read_ec == asio::error::operation_aborted)
				continue;
			if (read_ec == asio::error::eof)
				return true;
			if (read_ec)
			{
				if (error)
					*error = "tizen http read failed: " + read_ec.message();
				return false;
			}
			if (received == 0)
				return true;

			response->append(chunk.data(), received);
		}
	}

	[[nodiscard]] bool sslReadUntilHttpHeaders(
		SSL* ssl,
		std::string* response,
		std::string* error)
	{
		response->clear();
		std::array<char, 1024> buffer {};
		while (response->find("\r\n\r\n") == std::string::npos)
		{
			const int received = ::SSL_read(ssl, buffer.data(), static_cast<int>(buffer.size()));
			if (received <= 0)
			{
				if (error)
					*error = ::ERR_error_string(::ERR_get_error(), nullptr);
				return false;
			}
			response->append(buffer.data(), static_cast<size_t>(received));
		}
		return true;
	}

	[[nodiscard]] std::string base64Encode(const std::string& value)
	{
		if (value.empty())
			return {};

		std::string out;
		out.resize(4 * ((value.size() + 2) / 3));
		const int size = ::EVP_EncodeBlock(
			reinterpret_cast<unsigned char*>(out.data()),
			reinterpret_cast<const unsigned char*>(value.data()),
			static_cast<int>(value.size()));
		out.resize(size > 0 ? static_cast<size_t>(size) : 0);
		return out;
	}

	[[nodiscard]] std::string randomWebSocketKey()
	{
		std::array<unsigned char, 16> raw {};
		if (::RAND_bytes(raw.data(), static_cast<int>(raw.size())) != 1)
		{
			std::random_device random;
			for (auto& byte : raw)
				byte = static_cast<unsigned char>(random());
		}
		return base64Encode(std::string(
			reinterpret_cast<const char*>(raw.data()),
			raw.size()));
	}

	[[nodiscard]] bool sendWsTextFrame(
		SSL* ssl,
		const std::string& payload,
		std::string* error)
	{
		std::vector<unsigned char> frame;
		frame.reserve(payload.size() + 16);
		frame.push_back(0x81);

		if (payload.size() > 125)
		{
			frame.push_back(0x80 | 126);
			frame.push_back(static_cast<unsigned char>((payload.size() >> 8) & 0xff));
			frame.push_back(static_cast<unsigned char>(payload.size() & 0xff));
		}
		else
		{
			frame.push_back(static_cast<unsigned char>(0x80 | payload.size()));
		}

		std::array<unsigned char, 4> mask {};
		if (::RAND_bytes(mask.data(), static_cast<int>(mask.size())) != 1)
		{
			std::random_device random;
			for (auto& byte : mask)
				byte = static_cast<unsigned char>(random());
		}
		frame.insert(frame.end(), mask.begin(), mask.end());

		for (size_t i = 0; i < payload.size(); ++i)
			frame.push_back(static_cast<unsigned char>(payload[i]) ^ mask[i % mask.size()]);

		return sslWriteAll(
			ssl,
			std::string(reinterpret_cast<const char*>(frame.data()), frame.size()),
			error);
	}

	[[nodiscard]] std::optional<std::string> readWsTextFrame(
		SSL* ssl,
		std::string* error)
	{
		unsigned char header[2] {};
		const int header_rc = ::SSL_read(ssl, header, sizeof(header));
		if (header_rc <= 0)
		{
			if (error)
				*error = ::ERR_error_string(::ERR_get_error(), nullptr);
			return std::nullopt;
		}
		if (header_rc != 2)
		{
			if (error)
				*error = "incomplete websocket header";
			return std::nullopt;
		}

		const unsigned char opcode = header[0] & 0x0f;
		size_t payload_len = header[1] & 0x7f;
		const bool masked = (header[1] & 0x80u) != 0;

		if (payload_len == 126)
		{
			unsigned char ext[2] {};
			if (::SSL_read(ssl, ext, sizeof(ext)) != 2)
			{
				if (error)
					*error = "incomplete websocket extended length";
				return std::nullopt;
			}
			payload_len = (static_cast<size_t>(ext[0]) << 8) | ext[1];
		}
		else if (payload_len == 127)
		{
			if (error)
				*error = "unsupported websocket frame length";
			return std::nullopt;
		}

		std::array<unsigned char, 4> mask {};
		if (masked && ::SSL_read(ssl, mask.data(), static_cast<int>(mask.size())) !=
			static_cast<int>(mask.size()))
		{
			if (error)
				*error = "incomplete websocket mask";
			return std::nullopt;
		}

		std::string payload(payload_len, '\0');
		size_t offset = 0;
		while (offset < payload_len)
		{
			const int rc = ::SSL_read(
				ssl,
				payload.data() + offset,
				static_cast<int>(payload_len - offset));
			if (rc <= 0)
			{
				if (error)
					*error = "incomplete websocket payload";
				return std::nullopt;
			}
			offset += static_cast<size_t>(rc);
		}

		if (masked)
		{
			for (size_t i = 0; i < payload.size(); ++i)
				payload[i] = static_cast<char>(payload[i] ^ mask[i % mask.size()]);
		}

		if (opcode == 0x1)
			return payload;
		if (opcode == 0x8)
		{
			if (error)
				*error = "tizen websocket closed";
			return std::nullopt;
		}

		return std::string {};
	}

	[[nodiscard]] std::optional<TizenSession> openTizenSession(
		const TizenEndpoint& endpoint,
		const nlohmann::json& options,
		std::string* learned_token,
		std::string* error)
	{
		const int timeout_ms = std::max(500, getIntOption(options, "timeoutMs", 1500));
		const std::string client_name = getStringOption(options, "name", "WaveHome");
		const std::string name = base64Encode(client_name);
		const std::string token = getStringOption(options, "token");

		const auto connect_timeout = std::chrono::milliseconds(timeout_ms);
		TizenSession session;
		session.io = std::make_shared<asio::io_context>(1);
		session.socket = std::make_unique<asio::ip::tcp::socket>(*session.io);
		if (!asioConnect(
				*session.io,
				*session.socket,
				endpoint.host,
				endpoint.secure_port,
				connect_timeout,
				error))
			return std::nullopt;

		::SSL_load_error_strings();
		::OPENSSL_init_ssl(0, nullptr);
		session.ssl_ctx.reset(::SSL_CTX_new(TLS_client_method()));
		if (!session.ssl_ctx)
		{
			if (error)
				*error = "failed to initialize ssl context";
			return std::nullopt;
		}

		::SSL_CTX_set_verify(session.ssl_ctx.get(), SSL_VERIFY_NONE, nullptr);
		session.ssl.reset(::SSL_new(session.ssl_ctx.get()));
		if (!session.ssl)
		{
			if (error)
				*error = "failed to create ssl session";
			return std::nullopt;
		}

		::SSL_set_fd(session.ssl.get(), static_cast<int>(session.socket->native_handle()));
		::SSL_set_tlsext_host_name(session.ssl.get(), endpoint.host.c_str());
		if (::SSL_connect(session.ssl.get()) != 1)
		{
			if (error)
				*error = ::ERR_error_string(::ERR_get_error(), nullptr);
			return std::nullopt;
		}

		std::string request =
			"GET /api/v2/channels/samsung.remote.control?name=" + name;
		if (!token.empty())
			request += "&token=" + token;
		request += " HTTP/1.1\r\n";
		request += "Host: " + endpoint.host + ":" + std::to_string(endpoint.secure_port) + "\r\n";
		request += "Upgrade: websocket\r\n";
		request += "Connection: Upgrade\r\n";
		request += "Sec-WebSocket-Version: 13\r\n";
		request += "Sec-WebSocket-Key: " + randomWebSocketKey() + "\r\n\r\n";

		if (!sslWriteAll(session.ssl.get(), request, error))
			return std::nullopt;

		std::string response;
		if (!sslReadUntilHttpHeaders(session.ssl.get(), &response, error))
			return std::nullopt;
		if (response.find(" 101 ") == std::string::npos)
		{
			if (error)
				*error = "tizen websocket upgrade rejected";
			return std::nullopt;
		}

		if (learned_token)
		{
			if (auto frame = readWsTextFrame(session.ssl.get(), error); frame.has_value())
			{
				if (!frame->empty())
				{
					try
					{
						const auto json = nlohmann::json::parse(*frame);
						if (json.value("event", "") == "ms.channel.connect" &&
							json.contains("data") &&
							json.at("data").is_object() &&
							json.at("data").contains("token") &&
							json.at("data").at("token").is_string())
						{
							*learned_token = json.at("data").at("token").get<std::string>();
						}
					}
					catch (const std::exception&)
					{
					}
				}
			}
			else
			{
				if (error)
					error->clear();
			}
		}

		return session;
	}
	[[nodiscard]] bool postApplication(
		const TizenEndpoint& endpoint,
		const std::string& app_id,
		const int timeout_ms,
		std::string* error)
	{
		std::string request =
			"POST /api/v2/applications/" + app_id + " HTTP/1.1\r\n"
			"Host: " + endpoint.host + ":" + std::to_string(endpoint.api_port) + "\r\n"
			"Connection: close\r\n"
			"Content-Length: 0\r\n\r\n";
		std::string response;
		if (!httpRequest(endpoint.host, endpoint.api_port, request, &response, timeout_ms, error))
			return false;
		return response.find(" 200 ") != std::string::npos ||
			response.find(" 201 ") != std::string::npos ||
			response.find(" 202 ") != std::string::npos;
	}

	[[nodiscard]] bool isApiReachable(
		const TizenEndpoint& endpoint,
		const int timeout_ms)
	{
		const auto timeout = std::chrono::milliseconds(std::max(500, timeout_ms));
		asio::io_context io;
		asio::ip::tcp::socket socket(io);
		std::string error;
		return asioConnect(io, socket, endpoint.host, endpoint.api_port, timeout, &error);
	}

	[[nodiscard]] TizenPowerState queryPowerState(
		const TizenEndpoint& endpoint,
		const int timeout_ms,
		std::string* error)
	{
		std::string request =
			"GET /api/v2/ HTTP/1.1\r\n"
			"Host: " + endpoint.host + ":" + std::to_string(endpoint.api_port) + "\r\n"
			"Connection: close\r\n\r\n";
		std::string response;
		if (!httpRequestUntilClose(
				endpoint.host,
				endpoint.api_port,
				request,
				&response,
				timeout_ms,
				error))
			return TizenPowerState::Unknown;

		const auto header_end = response.find("\r\n\r\n");
		if (header_end == std::string::npos)
			return TizenPowerState::Unknown;

		const std::string body = response.substr(header_end + 4);
		if (body.empty())
			return TizenPowerState::Unknown;

		try
		{
			const auto json = nlohmann::json::parse(body);
			if (!json.contains("device") || !json.at("device").is_object())
				return TizenPowerState::Unknown;
			const auto& device = json.at("device");
			if (!device.contains("PowerState") || !device.at("PowerState").is_string())
				return TizenPowerState::Unknown;
			const std::string power_state = upperCopy(device.at("PowerState").get<std::string>());
			if (power_state == "ON")
				return TizenPowerState::On;
			if (power_state == "STANDBY" || power_state == "OFF")
				return TizenPowerState::Standby;
		}
		catch (const std::exception&)
		{
		}

		return TizenPowerState::Unknown;
	}

	[[nodiscard]] bool waitForPowerState(
		const TizenEndpoint& endpoint,
		const TizenPowerState desired_state,
		const int total_timeout_ms,
		const int probe_timeout_ms,
		const int retry_delay_ms,
		std::string* error)
	{
		std::string last_error;
		const auto deadline = std::chrono::steady_clock::now() +
			std::chrono::milliseconds(std::max(500, total_timeout_ms));
		while (std::chrono::steady_clock::now() < deadline)
		{
			if (queryPowerState(endpoint, probe_timeout_ms, &last_error) == desired_state)
			{
				if (error)
					error->clear();
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(std::max(100, retry_delay_ms)));
		}

		if (error)
		{
			*error = last_error.empty()
				? "timed out waiting for Tizen power state"
				: last_error;
		}
		return false;
	}

	[[nodiscard]] std::optional<std::array<unsigned char, 6>> parseMac(const std::string& mac)
	{
		std::array<unsigned char, 6> bytes {};
		std::stringstream stream(mac);
		std::string part;
		size_t index = 0;
		while (std::getline(stream, part, ':') && index < bytes.size())
		{
			try
			{
				bytes[index++] = static_cast<unsigned char>(std::stoul(part, nullptr, 16));
			}
			catch (const std::exception&)
			{
				return std::nullopt;
			}
		}
		if (index != bytes.size())
			return std::nullopt;
		return bytes;
	}

	[[nodiscard]] bool sendMagicPacket(const std::string& mac, std::string* error)
	{
		const auto mac_bytes = parseMac(mac);
		if (!mac_bytes)
		{
			if (error)
				*error = "invalid mac address";
			return false;
		}

		std::array<unsigned char, 102> packet {};
		packet.fill(0xff);
		for (size_t i = 0; i < 16; ++i)
		{
			std::copy(
				mac_bytes->begin(),
				mac_bytes->end(),
				packet.begin() + 6 + (i * mac_bytes->size()));
		}

		SocketHandle socket(::socket(AF_INET, SOCK_DGRAM, 0));
		if (socket.fd < 0)
		{
			if (error)
				*error = std::strerror(errno);
			return false;
		}

		const int broadcast = 1;
		::setsockopt(socket.fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

		sockaddr_in address {};
		address.sin_family = AF_INET;
		address.sin_port = htons(9);
		address.sin_addr.s_addr = INADDR_BROADCAST;

		const ssize_t sent = ::sendto(
			socket.fd,
			packet.data(),
			packet.size(),
			0,
			reinterpret_cast<sockaddr*>(&address),
			sizeof(address));
		if (sent != static_cast<ssize_t>(packet.size()))
		{
			if (error)
				*error = std::strerror(errno);
			return false;
		}
		return true;
	}
} // namespace

struct TizenCommandTransport::Session
{
	TizenSession transport;
};

TizenCommandTransport::TizenCommandTransport(std::string endpoint, nlohmann::json options) :
	m_endpoint(std::move(endpoint)),
	m_options(std::move(options))
{
}

TizenCommandTransport::~TizenCommandTransport()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	closeSessionLocked();
}

bool TizenCommandTransport::ensureSessionLocked(std::string* error)
{
	if (m_session && m_session->transport.ssl)
		return true;

	std::string learned_token;
	const TizenEndpoint endpoint = parseEndpoint(m_endpoint, m_options);
	auto session = openTizenSession(endpoint, m_options, &learned_token, error);
	if (!session)
		return false;

	if (!learned_token.empty())
		m_options["token"] = learned_token;

	auto persistent = std::make_unique<Session>();
	persistent->transport = std::move(*session);
	m_session = std::move(persistent);
	return true;
}

bool TizenCommandTransport::trySendRemotePayloadLocked(
	const std::string& payload,
	std::string* error)
{
	if (!ensureSessionLocked(error))
		return false;
	return sendWsTextFrame(m_session->transport.ssl.get(), payload, error);
}

bool TizenCommandTransport::sendRemotePayload(const std::string& payload, std::string* error)
{
	std::string last_error;
	const int retry_window_ms = std::max(1000, getIntOption(m_options, "commandRetryWindowMs", 5000));
	const int retry_delay_ms = std::max(100, getIntOption(m_options, "commandRetryDelayMs", 350));
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(retry_window_ms);

	while (true)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (trySendRemotePayloadLocked(payload, &last_error))
				return true;
			closeSessionLocked();
		}

		if (std::chrono::steady_clock::now() >= deadline)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
	}

	if (error)
		*error = last_error;
	return false;
}

bool TizenCommandTransport::waitForSessionReady(std::string* error)
{
	const int session_ready_wait_ms =
		std::max(1000, getIntOption(m_options, "sessionReadyWaitMs", 6000));
	const int power_retry_delay_ms = std::max(150, getIntOption(m_options, "powerRetryDelayMs", 400));
	std::string last_error;
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(session_ready_wait_ms);
	while (std::chrono::steady_clock::now() < deadline)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			closeSessionLocked();
			if (ensureSessionLocked(&last_error))
			{
				if (error)
					error->clear();
				return true;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(power_retry_delay_ms));
	}

	if (error)
		*error = last_error.empty() ? "timed out waiting for Tizen remote session" : last_error;
	return false;
}

void TizenCommandTransport::closeSessionLocked()
{
	m_session.reset();
}

bool TizenCommandTransport::publish(const std::string& channel, const std::string& payload)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_lastError.clear();
		m_state = TransportConnectionState::Connecting;
	}

	bool ok = false;
	if (channel == "remoteKey" || channel == "command")
		ok = sendRemoteKey(payload);
	else if (channel == "power")
		ok = sendPowerCommand(payload);
	else if (channel == "app")
		ok = sendAppCommand(payload);
	else if (channel == "source")
		ok = sendSourceCommand(payload);
	else if (channel == "art")
		ok = sendArtCommand(payload);
	else
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		setErrorLocked("unsupported tizen command channel: " + channel);
	}

	std::lock_guard<std::mutex> lock(m_mutex);
	if (ok)
	{
		m_state = TransportConnectionState::Connected;
		m_lastError.clear();
	}
	else if (m_state != TransportConnectionState::Error)
		m_state = TransportConnectionState::Disconnected;
	return ok;
}

void TizenCommandTransport::primeConnection()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_session && m_session->transport.ssl)
	{
		m_state = TransportConnectionState::Connected;
		return;
	}

	m_lastError.clear();
	m_state = TransportConnectionState::Connecting;
	std::string error;
	if (ensureSessionLocked(&error))
	{
		m_state = TransportConnectionState::Connected;
		return;
	}

	if (!error.empty())
		setErrorLocked(error);
	else
		m_state = TransportConnectionState::Disconnected;
}

TransportConnectionState TizenCommandTransport::connectionState() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_state;
}

std::string TizenCommandTransport::lastError() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_lastError;
}

nlohmann::json TizenCommandTransport::debugJson() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const TizenEndpoint endpoint = parseEndpoint(m_endpoint, m_options);
	return {
		{"connection", connectionStateName(m_state)},
		{"endpoint", m_endpoint},
		{"host", endpoint.host},
		{"securePort", endpoint.secure_port},
		{"apiPort", endpoint.api_port},
		{"name", getStringOption(m_options, "name")},
		{"sessionOpen", static_cast<bool>(m_session)},
		{"tokenPresent", m_options.contains("token") && m_options.at("token").is_string() &&
			!m_options.at("token").get<std::string>().empty()},
		{"lastError", m_lastError},
	};
}

bool TizenCommandTransport::sendRemoteKey(const std::string& key)
{
	const std::string trimmed = trimCopy(key);
	if (trimmed.empty())
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		setErrorLocked("missing remote key");
		return false;
	}

	nlohmann::json payload = {
		{"method", "ms.remote.control"},
		{"params", {
			{"Cmd", "Click"},
			{"DataOfCmd", trimmed},
			{"Option", false},
			{"TypeOfRemote", "SendRemoteKey"},
		}},
	};

	std::string error;
	if (!sendRemotePayload(payload.dump(), &error))
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		setErrorLocked(error);
		return false;
	}
	return true;
}

bool TizenCommandTransport::sendPowerCommand(const std::string& payload)
{
	const std::string command = upperCopy(trimCopy(payload));
	if (command.empty())
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		setErrorLocked("missing power command");
		return false;
	}

	const TizenEndpoint endpoint = parseEndpoint(m_endpoint, m_options);
	const int timeout_ms = std::max(500, getIntOption(m_options, "timeoutMs", 1500));
	const int power_retry_delay_ms = std::max(150, getIntOption(m_options, "powerRetryDelayMs", 400));
	const int power_on_wait_ms = std::max(2000, getIntOption(m_options, "powerOnWaitMs", 12000));
	std::string query_error;
	const TizenPowerState power_state = queryPowerState(endpoint, timeout_ms, &query_error);
	if (command == "ON")
	{
		if (power_state == TizenPowerState::On)
		{
			std::string session_error;
			(void)waitForSessionReady(&session_error);
			return true;
		}

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			closeSessionLocked();
		}
		bool wake_requested = false;
		if (sendRemoteKey("KEY_POWER"))
			wake_requested = true;
		else if (wakeOnLan())
			wake_requested = true;
		if (!wake_requested)
			return false;

		std::string wait_error;
		if (!waitForPowerState(
				endpoint,
				TizenPowerState::On,
				power_on_wait_ms,
				timeout_ms,
				power_retry_delay_ms,
				&wait_error))
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			setErrorLocked(wait_error);
			return false;
		}

		if (!waitForSessionReady(&wait_error))
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			setErrorLocked(wait_error);
			return false;
		}
		return true;
	}

	if (command == "OFF")
	{
		if (power_state == TizenPowerState::Standby)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			closeSessionLocked();
			return true;
		}

		std::string last_error;
		const int session_ready_wait_ms =
			std::max(1000, getIntOption(m_options, "sessionReadyWaitMs", 6000));
		const auto deadline = std::chrono::steady_clock::now() +
			std::chrono::milliseconds(session_ready_wait_ms);
		while (true)
		{
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				closeSessionLocked();
			}
			if (sendRemoteKey("KEY_POWER"))
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				closeSessionLocked();
				return true;
			}
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				last_error = m_lastError;
			}
			if (queryPowerState(endpoint, timeout_ms, &query_error) == TizenPowerState::Standby)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				closeSessionLocked();
				return true;
			}
			if (std::chrono::steady_clock::now() >= deadline)
				break;
			std::this_thread::sleep_for(std::chrono::milliseconds(power_retry_delay_ms));
		}

		if (!last_error.empty())
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			setErrorLocked(last_error);
		}
		return false;
	}

	return sendRemoteKey(command);
}

bool TizenCommandTransport::sendAppCommand(const std::string& app_id)
{
	const std::string trimmed = trimCopy(app_id);
	if (trimmed.empty())
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		setErrorLocked("missing application id");
		return false;
	}

	const TizenEndpoint endpoint = parseEndpoint(m_endpoint, m_options);
	std::string error;
	if (!postApplication(
			endpoint,
			trimmed,
			std::max(500, getIntOption(m_options, "timeoutMs", 1500)),
			&error))
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		setErrorLocked(error.empty() ? "failed to launch application" : error);
		return false;
	}
	return true;
}

bool TizenCommandTransport::sendSourceCommand(const std::string& source)
{
	std::string key = upperCopy(trimCopy(source));
	if (key.empty())
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		setErrorLocked("missing source value");
		return false;
	}
	if (key.rfind("KEY_", 0) != 0)
		key = "KEY_" + key;
	return sendRemoteKey(key);
}

bool TizenCommandTransport::sendArtCommand(const std::string& payload)
{
	const std::string command = upperCopy(trimCopy(payload));
	if (command == "ART")
		return sendAppCommand("com.samsung.tv.gallery");
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		setErrorLocked("unsupported art command");
	}
	return false;
}

bool TizenCommandTransport::wakeOnLan()
{
	std::string error;
	if (!sendMagicPacket(getStringOption(m_options, "mac"), &error))
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		setErrorLocked(error.empty() ? "failed to send wake-on-lan packet" : error);
		return false;
	}
	return true;
}

void TizenCommandTransport::setErrorLocked(const std::string& error)
{
	m_state = TransportConnectionState::Error;
	m_lastError = error.empty() ? "unknown tizen transport error" : error;
}

APPLIANCE_NAMESPACE_END
WAVE_NAMESPACE_END
