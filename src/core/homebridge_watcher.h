#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "appliance/appliance_manager.h"
#include "core/coredef.h"

WAVE_NAMESPACE_BEGIN
CORE_NAMESPACE_BEGIN

class HomebridgeWatcher
{
public:
	using ReloadCallback = std::function<void(const std::string& message)>;

	HomebridgeWatcher(appliance::ApplianceManager& manager, std::string config_path);
	~HomebridgeWatcher();

	HomebridgeWatcher(const HomebridgeWatcher&) = delete;
	HomebridgeWatcher& operator=(const HomebridgeWatcher&) = delete;

	bool start(ReloadCallback on_reload = {});
	void stop();

	const std::string& configPath() const { return m_config_path; }

private:
	void watchLoop();

	appliance::ApplianceManager& m_manager;
	std::string m_config_path;
	ReloadCallback m_on_reload;
	std::thread m_thread;
	std::atomic<bool> m_running {false};
	int m_inotify_fd = -1;
};

CORE_NAMESPACE_END
WAVE_NAMESPACE_END
