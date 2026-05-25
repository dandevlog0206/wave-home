#include "core/homebridge_watcher.h"

#include <cerrno>
#include <chrono>
#include <filesystem>

#include <drogon/drogon.h>

#include <sys/inotify.h>
#include <unistd.h>

WAVE_NAMESPACE_BEGIN
CORE_NAMESPACE_BEGIN

namespace
{
	namespace fs = std::filesystem;
}

HomebridgeWatcher::HomebridgeWatcher(
	appliance::ApplianceManager& manager,
	std::string config_path) :
	m_manager(manager),
	m_config_path(std::move(config_path))
{
}

HomebridgeWatcher::~HomebridgeWatcher()
{
	stop();
}

bool HomebridgeWatcher::start(ReloadCallback on_reload)
{
	if (m_running.load())
		return true;

	m_on_reload = std::move(on_reload);

	try
	{
		m_manager.loadFromHomebridgeConfig(m_config_path);
		if (m_on_reload)
			m_on_reload("Initial Homebridge config loaded");
	}
	catch (const std::exception& ex)
	{
		LOG_WARN << "homebridge: initial load failed: " << ex.what();
		if (m_on_reload)
			m_on_reload(std::string("Homebridge load failed: ") + ex.what());
	}

	m_running.store(true);
	m_thread = std::thread([this] { watchLoop(); });
	return true;
}

void HomebridgeWatcher::stop()
{
	if (!m_running.exchange(false))
		return;

	if (m_inotify_fd >= 0)
	{
		::close(m_inotify_fd);
		m_inotify_fd = -1;
	}
	if (m_thread.joinable())
		m_thread.join();
}

void HomebridgeWatcher::watchLoop()
{
	m_inotify_fd = inotify_init1(IN_NONBLOCK);
	if (m_inotify_fd < 0)
	{
		LOG_ERROR << "homebridge: inotify_init failed";
		m_running.store(false);
		return;
	}

	const fs::path config_path = m_config_path;
	const fs::path watch_dir =
		config_path.parent_path().empty() ? "." : config_path.parent_path();
	const int watch_descriptor = inotify_add_watch(
		m_inotify_fd,
		watch_dir.string().c_str(),
		IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
	if (watch_descriptor < 0)
	{
		LOG_ERROR << "homebridge: inotify_add_watch failed for " << watch_dir;
		::close(m_inotify_fd);
		m_inotify_fd = -1;
		m_running.store(false);
		return;
	}

	const std::string target_name = config_path.filename().string();
	auto last_reload = std::chrono::steady_clock::now() - std::chrono::seconds(10);

	while (m_running.load())
	{
		char buffer[4096];
		const ssize_t bytes = ::read(m_inotify_fd, buffer, sizeof(buffer));
		if (bytes < 0)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
				continue;
			}
			break;
		}

		bool relevant = false;
		for (char* ptr = buffer; ptr < buffer + bytes;)
		{
			const auto* event = reinterpret_cast<const inotify_event*>(ptr);
			if (event->len != 0 && target_name == event->name)
				relevant = true;
			ptr += sizeof(inotify_event) + event->len;
		}

		if (!relevant)
			continue;

		const auto now = std::chrono::steady_clock::now();
		if (now - last_reload < std::chrono::milliseconds(400))
			continue;
		last_reload = now;
		std::this_thread::sleep_for(std::chrono::milliseconds(150));

		try
		{
			m_manager.loadFromHomebridgeConfig(m_config_path);
			LOG_INFO << "homebridge: reloaded devices from " << m_config_path;
			if (m_on_reload)
				m_on_reload("Homebridge config changed · devices reloaded");
		}
		catch (const std::exception& ex)
		{
			LOG_WARN << "homebridge: reload failed: " << ex.what();
			if (m_on_reload)
				m_on_reload(std::string("Homebridge reload failed: ") + ex.what());
		}
	}

	inotify_rm_watch(m_inotify_fd, watch_descriptor);
	::close(m_inotify_fd);
	m_inotify_fd = -1;
}

CORE_NAMESPACE_END
WAVE_NAMESPACE_END
