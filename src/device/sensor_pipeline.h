#pragma once

#include <atomic>
#include <memory>
#include <string>

class SensorPipeline
{
public:
	SensorPipeline();
	~SensorPipeline();

	SensorPipeline(const SensorPipeline&) = delete;
	SensorPipeline& operator=(const SensorPipeline&) = delete;

	void start(const std::string& gesture_set_root);
	void stop();

	bool reloadActiveSet(const std::string& set_id);

	bool isRunning() const { return m_running.load(); }

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
	std::atomic<bool> m_running {false};
};
