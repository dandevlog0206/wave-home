// Tuya LAN protocol 3.3 framing/crypto adapted from gordonb3/tuyapp (GPL-3.0+).
#include "appliance/tuya_protocol.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

#include <asio.hpp>
#include <openssl/evp.h>

#include "core/coredef.h"

WAVE_NAMESPACE_BEGIN
APPLIANCE_NAMESPACE_BEGIN

namespace
{
	constexpr uint32_t kMessagePrefix = 0x000055aa;
	constexpr uint32_t kMessageSuffix = 0x0000aa55;
	constexpr size_t kHeaderSize = 16;
	constexpr size_t kExtraHeaderSize = 15;
	constexpr size_t kTrailerSize = 8;

	constexpr uint8_t kCmdControl = 7;
	constexpr uint8_t kCmdDpQuery = 10;

	const uint32_t kCrcTable[256] = {
		0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535,
		0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd,
		0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d,
		0x6ddde4eb, 0xf4d4b551, 0x83d385c7, 0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
		0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4,
		0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
		0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59, 0x26d930ac,
		0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
		0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab,
		0xb6662d3d, 0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f,
		0x9fbfe4a5, 0xe8b8d433, 0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb,
		0x086d3d2d, 0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
		0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea,
		0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65, 0x4db26158, 0x3ab551ce,
		0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a,
		0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
		0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409,
		0xce61e49f, 0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
		0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739,
		0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
		0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1, 0xf00f9344, 0x8708a3d2, 0x1e01f268,
		0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0,
		0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8,
		0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
		0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef,
		0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703,
		0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7,
		0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d, 0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a,
		0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae,
		0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
		0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777, 0x88085ae6,
		0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
		0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d,
		0x3e6e77db, 0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5,
		0x47b2cf7f, 0x30b5ffe9, 0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605,
		0xcdd70693, 0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
		0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d};

	[[nodiscard]] uint32_t crc32(const uint8_t* data, size_t len)
	{
		uint32_t checksum = 0xffffffff;
		for (size_t i = 0; i < len; ++i)
			checksum = kCrcTable[(checksum ^ data[i]) & 0xff] ^ (checksum >> 8);
		return checksum ^ 0xffffffff;
	}

	void writeU32Be(std::vector<uint8_t>& out, const size_t offset, const uint32_t value)
	{
		out[offset] = static_cast<uint8_t>((value >> 24) & 0xff);
		out[offset + 1] = static_cast<uint8_t>((value >> 16) & 0xff);
		out[offset + 2] = static_cast<uint8_t>((value >> 8) & 0xff);
		out[offset + 3] = static_cast<uint8_t>(value & 0xff);
	}

	[[nodiscard]] uint32_t readU32Be(const uint8_t* data)
	{
		return (static_cast<uint32_t>(data[0]) << 24) |
			(static_cast<uint32_t>(data[1]) << 16) |
			(static_cast<uint32_t>(data[2]) << 8) |
			static_cast<uint32_t>(data[3]);
	}

	[[nodiscard]] bool aes128EcbEncrypt(
		const std::vector<uint8_t>& key,
		const uint8_t* input,
		const int input_size,
		std::vector<uint8_t>& output)
	{
		if (key.size() != 16)
			return false;

		EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
		if (!ctx)
			return false;

		bool ok = false;
		int len = 0;
		int total = 0;
		output.assign(static_cast<size_t>(input_size + 32), 0);

		if (EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, key.data(), nullptr) == 1 &&
			EVP_EncryptUpdate(ctx, output.data(), &len, input, input_size) == 1)
		{
			total = len;
			if (EVP_EncryptFinal_ex(ctx, output.data() + len, &len) == 1)
			{
				total += len;
				output.resize(static_cast<size_t>(total));
				ok = true;
			}
		}

		EVP_CIPHER_CTX_free(ctx);
		return ok;
	}

	[[nodiscard]] bool aes128EcbDecrypt(
		const std::vector<uint8_t>& key,
		const uint8_t* input,
		const int input_size,
		std::vector<uint8_t>& output)
	{
		if (key.size() != 16 || input_size <= 0)
			return false;

		EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
		if (!ctx)
			return false;

		bool ok = false;
		int len = 0;
		int total = 0;
		output.assign(static_cast<size_t>(input_size + 16), 0);

		if (EVP_DecryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, key.data(), nullptr) == 1 &&
			EVP_CIPHER_CTX_set_padding(ctx, 1) == 1 &&
			EVP_DecryptUpdate(ctx, output.data(), &len, input, input_size) == 1)
		{
			total = len;
			if (EVP_DecryptFinal_ex(ctx, output.data() + len, &len) == 1)
			{
				total += len;
				output.resize(static_cast<size_t>(total));
				ok = true;
			}
		}

		EVP_CIPHER_CTX_free(ctx);
		return ok;
	}

	[[nodiscard]] std::vector<uint8_t> deriveEncryptionKey(const std::string& local_key)
	{
		// Match tuyapp: use the local key bytes directly (16-char keys are common).
		std::vector<uint8_t> key(16, 0);
		const size_t copy_len = std::min(local_key.size(), key.size());
		if (copy_len > 0)
			memcpy(key.data(), local_key.data(), copy_len);
		return key;
	}

	[[nodiscard]] bool connectWithTimeout(
		asio::io_context& io,
		asio::ip::tcp::socket& socket,
		const std::string& host,
		const uint16_t port,
		const std::chrono::milliseconds timeout,
		std::string* error)
	{
		asio::error_code address_ec;
		const auto address = asio::ip::make_address(host, address_ec);
		if (address_ec)
		{
			if (error)
				*error = "invalid tuya host address: " + address_ec.message();
			return false;
		}

		asio::ip::tcp::endpoint endpoint(address, port);
		asio::steady_timer timer(io);
		bool connected = false;
		bool completed = false;

		timer.expires_after(timeout);
		timer.async_wait([&](const std::error_code& timer_ec) {
			if (!timer_ec && !completed)
				socket.cancel();
		});

		socket.async_connect(endpoint, [&](const std::error_code& connect_ec) {
			completed = true;
			connected = !connect_ec;
			timer.cancel();
		});

		io.restart();
		io.run();

		if (!connected && error)
			*error = "tuya tcp connect timeout";
		return connected;
	}

	[[nodiscard]] bool sendAll(
		asio::ip::tcp::socket& socket,
		const std::vector<uint8_t>& data,
		std::string* error)
	{
		asio::error_code ec;
		asio::write(socket, asio::buffer(data), ec);
		if (ec)
		{
			if (error)
				*error = "tuya tcp write failed: " + ec.message();
			return false;
		}
		return true;
	}

	[[nodiscard]] bool receiveAtLeast(
		asio::io_context& io,
		asio::ip::tcp::socket& socket,
		std::vector<uint8_t>& buffer,
		const int min_bytes,
		const std::chrono::milliseconds timeout,
		std::string* error)
	{
		buffer.clear();
		std::array<uint8_t, 1024> chunk {};
		const auto deadline = std::chrono::steady_clock::now() + timeout;

		while (static_cast<int>(buffer.size()) < min_bytes)
		{
			if (std::chrono::steady_clock::now() >= deadline)
			{
				if (error)
					*error = "tuya tcp read timeout";
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
					*error = "tuya tcp read failed: " + read_ec.message();
				return false;
			}
			if (received == 0)
				continue;

			buffer.insert(buffer.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(received));
		}

		return true;
	}
} // namespace

namespace tuya_protocol
{
	std::string generateDpQueryPayload(const std::string& device_id)
	{
		const auto now = static_cast<long>(std::time(nullptr));
		return "{\"gwId\":\"" + device_id + "\",\"devId\":\"" + device_id +
			"\",\"uid\":\"" + device_id + "\",\"t\":\"" + std::to_string(now) + "\"}";
	}

	std::string generateControlPayload(
		const std::string& device_id,
		const nlohmann::json& dps)
	{
		const auto now = static_cast<long>(std::time(nullptr));
		return "{\"devId\":\"" + device_id + "\",\"uid\":\"" + device_id + "\",\"dps\":" +
			dps.dump() + ",\"t\":\"" + std::to_string(now) + "\"}";
	}

	std::vector<uint8_t> buildMessage(
		const uint8_t command,
		const std::string& payload_json,
		const std::vector<uint8_t>& encryption_key,
		uint32_t& sequence)
	{
		std::vector<uint8_t> message(kHeaderSize, 0);
		writeU32Be(message, 0, kMessagePrefix);

		++sequence;
		writeU32Be(message, 4, sequence);
		message[11] = command;

		size_t buffer_pos = kHeaderSize;
		if (command != kCmdDpQuery)
		{
			message.resize(buffer_pos + kExtraHeaderSize, 0);
			memcpy(message.data() + buffer_pos, "3.3", 3);
			buffer_pos += kExtraHeaderSize;
		}

		std::vector<uint8_t> encrypted;
		if (!aes128EcbEncrypt(
				encryption_key,
				reinterpret_cast<const uint8_t*>(payload_json.data()),
				static_cast<int>(payload_json.size()),
				encrypted))
			return {};

		message.insert(message.end(), encrypted.begin(), encrypted.end());
		buffer_pos = message.size();

		const size_t total_size = buffer_pos + kTrailerSize;
		message.resize(total_size, 0);

		const uint16_t payload_len = static_cast<uint16_t>(total_size - kHeaderSize);
		message[14] = static_cast<uint8_t>((payload_len >> 8) & 0xff);
		message[15] = static_cast<uint8_t>(payload_len & 0xff);

		const uint32_t crc = crc32(message.data(), buffer_pos);
		writeU32Be(message, buffer_pos, crc);
		writeU32Be(message, buffer_pos + 4, kMessageSuffix);
		return message;
	}

	std::string decodeMessage(
		const std::vector<uint8_t>& buffer,
		const std::vector<uint8_t>& encryption_key)
	{
		std::string result;
		size_t offset = 0;
		while (offset + kHeaderSize <= buffer.size())
		{
			const uint8_t* frame = buffer.data() + offset;
			const uint16_t payload_len =
				(static_cast<uint16_t>(frame[14]) << 8) | frame[15];
			const size_t message_size = static_cast<size_t>(payload_len) + kHeaderSize;
			if (offset + message_size > buffer.size())
				break;

			const uint16_t retcode =
				(static_cast<uint16_t>(frame[18]) << 8) | frame[19];
			if (retcode != 0)
			{
				char error_message[64];
				snprintf(error_message, sizeof(error_message), "{\"msg\":\"device returned error %u\"}", retcode);
				result.append(error_message);
				offset += message_size;
				continue;
			}

			const uint32_t crc_sent = readU32Be(frame + message_size - kTrailerSize);
			const uint32_t crc_calc = crc32(frame, message_size - kTrailerSize);
			if (crc_sent != crc_calc)
			{
				result.append("{\"msg\":\"crc error\"}");
				offset += message_size;
				continue;
			}

			// tuyapp skips a 4-byte return code field after the 16-byte header.
			constexpr size_t kReturnCodeSize = 4;
			const uint8_t* encrypted = frame + kHeaderSize + kReturnCodeSize;
			size_t encrypted_len =
				message_size - kHeaderSize - kReturnCodeSize - kTrailerSize;
			if ((frame[15] & 0x1) && encrypted_len >= 3 && encrypted[0] == '3' &&
				encrypted[1] == '.' && encrypted[2] == '3')
			{
				encrypted += kExtraHeaderSize;
				encrypted_len -= kExtraHeaderSize;
			}

			std::vector<uint8_t> decrypted;
			if (aes128EcbDecrypt(
					encryption_key,
					encrypted,
					static_cast<int>(encrypted_len),
					decrypted))
			{
				result.append(reinterpret_cast<const char*>(decrypted.data()), decrypted.size());
			}
			else
			{
				result.append("{\"msg\":\"error decrypting payload\"}");
			}

			offset += message_size;
		}
		return result;
	}

	std::optional<bool> parseSwitchDp(
		const std::string& decoded_json,
		const std::string& switch_dp)
	{
		try
		{
			const auto json = nlohmann::json::parse(decoded_json);
			if (json.contains("dps") && json.at("dps").is_object())
			{
				const auto& dps = json.at("dps");
				if (dps.contains(switch_dp))
					return dps.at(switch_dp).get<bool>();
			}
			const auto needle = "\"" + switch_dp + "\":";
			const size_t pos = decoded_json.find(needle);
			if (pos == std::string::npos)
				return std::nullopt;
			const size_t value_pos = pos + needle.size();
			if (value_pos < decoded_json.size() && decoded_json[value_pos] == 't')
				return true;
			if (value_pos < decoded_json.size() && decoded_json[value_pos] == 'f')
				return false;
		}
		catch (const std::exception&)
		{
		}
		return std::nullopt;
	}
} // namespace tuya_protocol

TuyaLanClient::TuyaLanClient(TuyaEndpointConfig config) :
	m_config(std::move(config))
{
}

void TuyaLanClient::disconnect()
{
	m_connected = false;
}

std::vector<uint8_t> TuyaLanClient::encryptionKey() const
{
	return deriveEncryptionKey(m_config.localKey);
}

bool TuyaLanClient::transact(
	const std::vector<uint8_t>& request,
	std::vector<uint8_t>& response,
	std::string* error)
{
	if (m_config.host.empty() || m_config.deviceId.empty() || m_config.localKey.empty())
	{
		if (error)
			*error = "missing tuya host, deviceId, or localKey";
		return false;
	}

	const auto timeout = std::chrono::milliseconds(std::max(500, m_config.timeoutMs));
	asio::io_context io;
	asio::ip::tcp::socket socket(io);

	if (!connectWithTimeout(io, socket, m_config.host, m_config.port, timeout, error))
		return false;

	if (!sendAll(socket, request, error))
		return false;

	if (!receiveAtLeast(io, socket, response, m_config.receiveMinBytes, timeout, error))
		return false;

	m_connected = true;
	asio::error_code ec;
	socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
	socket.close(ec);
	return true;
}

bool TuyaLanClient::queryStatus(std::string* decoded_json, std::string* error)
{
	const auto key = encryptionKey();
	const auto payload = tuya_protocol::generateDpQueryPayload(m_config.deviceId);
	const auto request =
		tuya_protocol::buildMessage(kCmdDpQuery, payload, key, m_sequence);
	if (request.empty())
	{
		if (error)
			*error = "failed to build tuya query frame";
		return false;
	}

	std::vector<uint8_t> response;
	if (!transact(request, response, error))
		return false;

	if (decoded_json)
		*decoded_json = tuya_protocol::decodeMessage(response, key);
	return true;
}

bool TuyaLanClient::setDps(
	const nlohmann::json& dps,
	std::string* decoded_json,
	std::string* error)
{
	const auto key = encryptionKey();
	const auto payload = tuya_protocol::generateControlPayload(m_config.deviceId, dps);
	const auto request =
		tuya_protocol::buildMessage(kCmdControl, payload, key, m_sequence);
	if (request.empty())
	{
		if (error)
			*error = "failed to build tuya control frame";
		return false;
	}

	std::vector<uint8_t> response;
	if (!transact(request, response, error))
		return false;

	if (decoded_json)
		*decoded_json = tuya_protocol::decodeMessage(response, key);
	return true;
}

bool TuyaLanClient::setSwitch(const bool on, std::string* decoded_json, std::string* error)
{
	nlohmann::json dps = nlohmann::json::object();
	dps[m_config.switchDp] = on;
	return setDps(dps, decoded_json, error);
}

APPLIANCE_NAMESPACE_END
WAVE_NAMESPACE_END
