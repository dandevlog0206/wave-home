#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <ostream>
#include <vector>
#include <asio.hpp>

namespace retina
{
	using asio::ip::tcp;

	enum TargetStatus : uint32_t
	{
		Standing = 0,
		Sitting = 1,
		Lying = 2,
		Walking = 4,
	};

	struct Point
	{
		float x;
		float y;
		float z;
		float doppler;
		float power;
		int32_t targetId;
	};
	
	struct Target
	{
		float x;
		float y;
		TargetStatus status;
		uint32_t targetId;
		float minx;
		float maxx;
		float miny;
		float maxy;
		float minz;
		float maxz;
	};
	
	struct Frame
	{
		uint32_t packetSize;
		uint32_t frameCount;
		uint64_t deltaUs;
		std::vector<Point> points;
		std::vector<Target> targets;
	};

	struct SensorSpec
	{
		float hfovDeg;
		float vfovDeg;
		float sensorWidth;
		float sensorHeight;
		float posX;
		float posY;
		float posZ;
		float yawDeg;
		float pitchDeg;
		float rangeMinX;
		float rangeMinY;
		float rangeMinZ;
		float rangeMaxX;
		float rangeMaxY;
		float rangeMaxZ;
		float range;
	};

	struct DeviceInfo
	{
		std::string ip;
		std::string mac;
		std::string model;
		SensorSpec sensorSpec;
	};

	static const char* to_string(retina::TargetStatus status)
	{
		switch (status)
		{
			case retina::TargetStatus::Standing: return "Standing";
			case retina::TargetStatus::Sitting: return "Sitting";
			case retina::TargetStatus::Lying: return "Lying";
			case retina::TargetStatus::Walking: return "Walking";
			default: return "Unknown";
		}
	}

	class DeviceFinder
	{
	public:
		enum class Result
		{
			Success,
			Canceled,
			Failed
		};

		DeviceFinder(std::ostream& log);

		// synchronous function which finds the first device in the local network and returns its IP address
		Result find(const std::string& local_ip, const std::string& subnet_mask, std::string& out_host);

		// async function which cancels the ongoing find operation
		void cancel();

	private:
		bool buildScanRange(const std::string& local_ip, const std::string& subnet_mask, uint32_t& out_network, uint32_t& out_first, uint32_t& out_last);
		bool probeHost(asio::io_context& io, const std::string& ip);
		bool probeTcpPort(asio::io_context& io, const std::string& ip, uint16_t port, std::chrono::milliseconds timeout);
		bool probeHttpPort(asio::io_context& io, const std::string& ip, std::chrono::milliseconds timeout);
		std::string formatAddress(uint32_t address) const;

	private:
		std::vector<asio::io_context*> m_io_contexts;
		std::mutex m_mutex;
		std::atomic_bool m_canceled;
		std::ostream& m_log;
	};

	class RetinaClient
	{
	public:
		static constexpr uint32_t frame_count_limit_default = 60;

		RetinaClient(asio::io_context& io, std::ostream& log_stream);
		~RetinaClient();

		void connect(const std::string& host);

		void setOnConnected(std::function<void()> callback);
		void setOnDisconnected(std::function<void()> callback);
		void setOnFrame(std::function<void(const Frame&)> callback);

		void setFrameCountLimit(uint32_t limit);

		void getFrames(std::function<void(const std::deque<Frame>&)> callback) const;
		double getFrameRate() const;
		double getBandwidthMbps(uint64_t interval_ms = 1000) const;

		const DeviceInfo& getDeviceInfo() const;

		void shutdown();
	
	private:
		void connectAsync(tcp::resolver::results_type endpoints);
		void doRead();
		bool tryExtractPacket(std::vector<uint8_t>& stream_buf, std::vector<uint8_t>& out_packet_buf);
		bool parseSinglePacket(const std::vector<uint8_t>& packet_buf, Frame& out_frame);
		bool findPacketMagic(const std::vector<uint8_t>& buffer, size_t& out_offset);

	private:
		asio::io_context& m_io;
		std::ostream& m_log;

		tcp::resolver m_resolver;
		tcp::socket m_socket;
		uint32_t m_frame_count_limit;


		std::vector<uint8_t> m_read_buf {};
		std::vector<uint8_t> m_stream_buf;
		std::vector<uint8_t> m_packet_buf;
		mutable std::deque<Frame> m_frames;
		mutable std::mutex m_mutex;

		std::function<void()> m_on_connected;
		std::function<void()> m_on_disconnected;
		std::function<void(const Frame&)> m_on_frame;

		// frame rate tracking
		uint64_t m_last_frame_timepoint = 0;
		uint64_t m_total_duration_us = 0;
		size_t m_total_frames = 0;

		// bandwidth tracking
		mutable std::deque<std::pair<uint64_t, size_t>> m_bandwidth_samples;
		mutable size_t m_bandwidth_total_bytes = 0;

		DeviceInfo m_device_info;
	};
}