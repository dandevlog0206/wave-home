#include "sensor_pipeline.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <drogon/drogon.h>
#include <asio.hpp>
#include "app/app_state.h"
#include "gesture_probability_gate.h"
#include "gesture_set_catalog.h"
#include "inference_engine.h"
#include "retina.h"
#include "../device/network.h"

WAVE_NAMESPACE_BEGIN

namespace
{
	constexpr std::chrono::seconds kReconnectDelay {15};

	void devLog(const std::string& level, const std::string& message)
	{
		AppState::instance().appendDevLog(level, message);
	}

	struct QueuedRadarFrame
	{
		retina::Frame frame;
	};

	void publishRadarConnecting(const std::string& detail_key, uint32_t countdown_sec = 0)
	{
		RadarState radar {};
		radar.connected = false;
		radar.status = "connecting";
		radar.detail = detail_key;
		radar.reconnectCountdownSec = countdown_sec;
		AppState::instance().updateRadar(radar);
	}

	void sleepReconnectDelay(std::atomic<bool>& stop_requested)
	{
		constexpr auto delay = kReconnectDelay;
		const auto deadline = std::chrono::steady_clock::now() + delay;

		while (!stop_requested.load())
		{
			const auto now = std::chrono::steady_clock::now();
			if (now >= deadline)
				break;

			const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(deadline - now).count();
			publishRadarConnecting(
				"radar.detail.connecting",
				static_cast<uint32_t>(std::max<int64_t>(1, remaining)));
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	}
}

struct SensorPipeline::Impl
{
	std::ostream& log = std::cerr;

	GestureSetCatalog catalog;
	net::InferenceEngine inference;
	wave::GestureProbabilityGate probabilityGate;
	std::mutex inference_mutex;

	std::mutex queue_mutex;
	std::condition_variable queue_cv;
	std::deque<QueuedRadarFrame> frame_queue;
	std::atomic<bool> stop_requested {false};

	std::thread connection_thread;
	std::thread inference_thread;

	std::unique_ptr<asio::io_context> io_context;
	std::unique_ptr<retina::RetinaClient> client;
	std::string gesture_set_root;

	void connectionLoop();
	void inferenceLoop();
	void publishRadarDisconnected();
	void publishRadarConnected(const retina::DeviceInfo& info);
	void publishInferenceResult();

	void pushFrame(retina::Frame frame);
	bool popFrame(QueuedRadarFrame& out);
};

SensorPipeline::SensorPipeline() :
	m_impl(std::make_unique<Impl>())
{
}

SensorPipeline::~SensorPipeline()
{
	stop();
}

void SensorPipeline::start(const std::string& gesture_set_root)
{
	if (m_running.exchange(true))
		return;

	m_impl->gesture_set_root = gesture_set_root;
	AppState::instance().setGestureRoot(gesture_set_root);

	m_impl->catalog.setRoot(gesture_set_root);
	const auto& active_id = AppState::instance().gestures().activeSetId();
	if (!m_impl->catalog.loadSet(active_id))
	{
		LOG_ERROR << "sensor_pipeline: failed to load active set " << active_id;
		m_running.store(false);
		return;
	}

	try
	{
		std::lock_guard<std::mutex> lock(m_impl->inference_mutex);
		m_impl->inference.load(m_impl->catalog.activeSet().modelJsonPath.c_str());
		m_impl->probabilityGate.configure(m_impl->catalog.activeSet());
	}
	catch (const std::exception& ex)
	{
		LOG_WARN << "sensor_pipeline: inference load failed (UI still available): " << ex.what();
	}

	m_impl->stop_requested.store(false);
	m_impl->connection_thread = std::thread([this] { m_impl->connectionLoop(); });
	m_impl->inference_thread = std::thread([this] { m_impl->inferenceLoop(); });

	LOG_INFO << "sensor_pipeline: started (active set " << m_impl->catalog.activeSetId() << ')';
	devLog("info", "sensor_pipeline: started " + m_impl->catalog.activeSetId());
}

void SensorPipeline::stop()
{
	if (!m_running.exchange(false))
		return;

	{
		std::lock_guard<std::mutex> lock(m_impl->queue_mutex);
		m_impl->stop_requested.store(true);
	}
	m_impl->queue_cv.notify_all();

	if (m_impl->client)
		m_impl->client->shutdown();
	if (m_impl->io_context)
		m_impl->io_context->stop();

	if (m_impl->connection_thread.joinable())
		m_impl->connection_thread.join();
	if (m_impl->inference_thread.joinable())
		m_impl->inference_thread.join();

	{
		std::lock_guard<std::mutex> lock(m_impl->queue_mutex);
		m_impl->frame_queue.clear();
	}
	m_impl->client.reset();
	m_impl->io_context.reset();

	LOG_INFO << "sensor_pipeline: stopped";
	devLog("info", "sensor_pipeline: stopped");
}

bool SensorPipeline::reloadActiveSet(const std::string& set_id)
{
	if (!m_running.load())
		return false;

	m_impl->catalog.setRoot(m_impl->gesture_set_root);
	if (!m_impl->catalog.loadSet(set_id))
	{
		devLog("error", "sensor_pipeline: failed to load gesture set · " + set_id);
		return false;
	}

	try
	{
		{
			std::lock_guard<std::mutex> queue_lock(m_impl->queue_mutex);
			m_impl->frame_queue.clear();
		}
		{
			std::lock_guard<std::mutex> inference_lock(m_impl->inference_mutex);
			m_impl->inference.load(m_impl->catalog.activeSet().modelJsonPath.c_str());
			m_impl->probabilityGate.configure(m_impl->catalog.activeSet());
			m_impl->probabilityGate.reset();
		}
		LOG_INFO << "sensor_pipeline: reloaded model for " << set_id;
		devLog("info", "sensor_pipeline: reloaded model for " + set_id);
		return true;
	}
	catch (const std::exception& ex)
	{
		LOG_WARN << "sensor_pipeline: reload failed for " << set_id << ": " << ex.what();
		devLog("error", std::string("sensor_pipeline: model reload failed · ") + set_id + ": " + ex.what());
		std::lock_guard<std::mutex> inference_lock(m_impl->inference_mutex);
		m_impl->probabilityGate.configure(m_impl->catalog.activeSet());
		m_impl->probabilityGate.reset();
		return false;
	}
}

void SensorPipeline::reloadTriggerBindings(
	const std::unordered_map<uint32_t, GestureTriggerConfig>& overrides)
{
	if (!m_running.load())
		return;

	std::lock_guard<std::mutex> lock(m_impl->inference_mutex);
	m_impl->probabilityGate.configure(m_impl->catalog.activeSet(), overrides);
	m_impl->probabilityGate.reset();
	devLog("info", "sensor_pipeline: trigger bindings updated");
}

void SensorPipeline::Impl::publishRadarDisconnected()
{
	RadarState radar {};
	radar.connected = false;
	radar.status = "offline";
	radar.detail = "radar.detail.disconnected";
	radar.reconnectCountdownSec = 0;
	AppState::instance().updateRadar(radar);
}

void SensorPipeline::Impl::publishRadarConnected(const retina::DeviceInfo& info)
{
	RadarState radar {};
	radar.connected = true;
	radar.status = "ok";
	radar.detail = "radar.detail.live";
	radar.ip = info.ip;
	radar.mac = info.mac;
	radar.model = info.model;
	radar.reconnectCountdownSec = 0;
	AppState::instance().updateRadar(radar);
}

void SensorPipeline::Impl::publishInferenceResult()
{
	InferenceSnapshot snap {};
	std::vector<wave::GestureGateDebug> gates;
	{
		std::lock_guard<std::mutex> lock(inference_mutex);
		if (!inference.hasSequence(net::SEQUENCE_IDX_BACK))
			return;

		const auto probs = inference.getSequenceProbabilities(net::SEQUENCE_IDX_BACK);
		const auto embed_map = inference.getSequenceEmbeddingMap(net::SEQUENCE_IDX_BACK);
		const auto model = inference.getModelInfo();

		snap.probabilities = probs;
		snap.embeddingMap = embed_map;
		snap.embedDim = model.frameEncoderInfo.outputSize;
		snap.sequenceLength = model.sequenceLength;
		snap.sequenceReady = true;

		gates = probabilityGate.debugSnapshot();
	}

	for (auto& gate : gates)
	{
		const auto name = AppState::instance().gestures().gestureName(
			AppState::instance().gestures().activeSetId(),
			gate.gestureClassId);
		if (!name.empty())
			(void)name;
	}

	AppState::instance().updateInference(snap, gates);
}

void SensorPipeline::Impl::pushFrame(retina::Frame frame)
{
	{
		std::lock_guard<std::mutex> lock(queue_mutex);
		if (stop_requested)
			return;
		frame_queue.push_back(QueuedRadarFrame {std::move(frame)});
	}
	queue_cv.notify_one();
}

bool SensorPipeline::Impl::popFrame(QueuedRadarFrame& out)
{
	std::unique_lock<std::mutex> lock(queue_mutex);
	queue_cv.wait(lock, [this] {
		return stop_requested.load() || !frame_queue.empty();
	});
	if (stop_requested.load() && frame_queue.empty())
		return false;
	out = std::move(frame_queue.front());
	frame_queue.pop_front();
	return true;
}

void SensorPipeline::Impl::connectionLoop()
{
	publishRadarDisconnected();

	while (!stop_requested.load())
	{
		network::LocalNetworkInfo net_info {};
		if (!network::getLocalNetworkInfo(net_info))
		{
			LOG_WARN << "sensor_pipeline: failed to read local network info, retry in 15s";
			sleepReconnectDelay(stop_requested);
			continue;
		}

		publishRadarConnecting("radar.detail.scanning");

		retina::DeviceFinder finder(log);
		std::string host;
		const auto result = finder.find(net_info.address, net_info.subnetMask, host);
		if (result != retina::DeviceFinder::Result::Success)
		{
			LOG_WARN << "sensor_pipeline: device scan failed, retry in 15s";
			sleepReconnectDelay(stop_requested);
			continue;
		}

		publishRadarConnecting("radar.detail.connecting");

		try
		{
			io_context = std::make_unique<asio::io_context>(1);
			client = std::make_unique<retina::RetinaClient>(*io_context, log);

			client->setOnConnected([this]() {
				if (client)
					publishRadarConnected(client->getDeviceInfo());
			});

			client->setOnFrame([this](const retina::Frame& frame) {
				RadarState radar = AppState::instance().radarSnapshot();
				radar.connected = true;
				radar.status = "ok";
				radar.detail = "radar.detail.live";
				radar.targetCount = static_cast<uint32_t>(frame.targets.size());
				radar.frameRateHz = client ? client->getFrameRate() : 0.0;
				if (client)
				{
					radar.ip = client->getDeviceInfo().ip;
					radar.mac = client->getDeviceInfo().mac;
					radar.model = client->getDeviceInfo().model;
				}
				radar.reconnectCountdownSec = 0;
				AppState::instance().updateRadar(radar);
				pushFrame(frame);
			});

			client->setOnDisconnected([this]() {
				publishRadarDisconnected();
				if (io_context)
					io_context->stop();
			});

			client->connect(host);

			asio::executor_work_guard<asio::io_context::executor_type> work(
				io_context->get_executor());

			std::thread io_thread([this] {
				io_context->run();
			});

			io_thread.join();

			client.reset();
			io_context.reset();
		}
		catch (const std::exception& ex)
		{
			LOG_ERROR << "sensor_pipeline: connection error: " << ex.what();
		}

		if (stop_requested.load())
			break;

		LOG_WARN << "sensor_pipeline: disconnected, retry in 15s";
		devLog("warn", "sensor_pipeline: disconnected, retry in 15s");
		sleepReconnectDelay(stop_requested);
	}
}

void SensorPipeline::Impl::inferenceLoop()
{
	std::vector<net::Point> points;

	while (!stop_requested.load())
	{
		QueuedRadarFrame item;
		if (!popFrame(item))
			break;

		try
		{
			points.clear();
			points.reserve(item.frame.points.size());
			for (const auto& point : item.frame.points)
			{
				points.push_back(net::Point {
					point.x,
					point.y,
					point.z,
					point.doppler,
					point.power,
				});
			}

			std::vector<wave::GestureTriggerEvent> events;
			{
				std::lock_guard<std::mutex> lock(inference_mutex);
				inference.enqueueFrame(std::move(points), net::FRAME_IDX_BACK);

				if (!inference.hasSequence(net::SEQUENCE_IDX_BACK))
					continue;

				const auto probs = inference.getSequenceProbabilities(net::SEQUENCE_IDX_BACK);
				events = probabilityGate.update(probs);
			}
			publishInferenceResult();

			for (const auto& ev : events)
			{
				if (!ev.fired)
					continue;
				AppState::instance().recordGestureTrigger(ev.gestureClassId, ev.score);
				LOG_INFO << "gesture class=" << ev.gestureClassId
				         << " score=" << ev.score
				         << " fired=" << ev.fired;
			}
		}
		catch (const std::exception& ex)
		{
			LOG_WARN << "sensor_pipeline: inference error: " << ex.what();
		}
	}
}

WAVE_NAMESPACE_END
