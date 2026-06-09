#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <drogon/drogon.h>

#include "app/app_state.h"
#include "util/arg_parser.h"

namespace fs = std::filesystem;

static constexpr unsigned kDefaultPort = 8500;
static constexpr unsigned kMaxThreads = 4;

static fs::path resolveSiteRoot(const std::string& cli_path)
{
	if (!cli_path.empty())
	{
		const auto p = fs::path(cli_path);
		if (fs::exists(p / "index.html"))
			return fs::weakly_canonical(p);
		return fs::weakly_canonical(p);
	}

	const auto exe = fs::read_symlink("/proc/self/exe");
	return fs::weakly_canonical(exe.parent_path() / ".." / "site");
}

static fs::path resolveGestureRoot(const std::string& cli_path)
{
	if (!cli_path.empty())
		return fs::weakly_canonical(cli_path);

	const auto exe = fs::read_symlink("/proc/self/exe");
	return fs::weakly_canonical(exe.parent_path() / ".." / "gesture_set");
}

static fs::path resolveConfigRoot(const std::string& cli_path)
{
	if (!cli_path.empty())
		return fs::weakly_canonical(cli_path);

	const auto exe = fs::read_symlink("/proc/self/exe");
	return fs::weakly_canonical(exe.parent_path() / ".." / "config");
}

static ArgParser makeArgParser()
{
	ArgParser parser("wave-server", "Wave Home dashboard server");
	parser.addArgument("--port", "-p")
		.help("HTTP listen port")
		.defaultValue(std::to_string(kDefaultPort));
	parser.addArgument("--site-root")
		.help("Dashboard static site root")
		.defaultValue("");
	parser.addArgument("--set-root", "-s")
		.help("Gesture set directory")
		.defaultValue("");
	parser.addArgument("--config-root")
		.help("Runtime config directory containing appliances.json and server_state.json")
		.defaultValue("");
	parser.addArgument("--ncnn-profile")
		.help("Enable NCNN inference timing (PointNet per-frame + temporal aggregator)")
		.actionFlag();
	return parser;
}

int main(int argc, char* argv[])
{
	unsigned port = kDefaultPort;
	std::string site_root_arg;
	std::string gesture_root_arg;
	std::string config_root_arg;
	bool ncnn_profile = false;

	try
	{
		auto parser = makeArgParser();
		parser.parseArgs(argc, const_cast<const char**>(argv));
		port = parser.get<unsigned>("port");
		site_root_arg = parser.get<std::string>("site-root");
		gesture_root_arg = parser.get<std::string>("set-root");
		config_root_arg = parser.get<std::string>("config-root");
		ncnn_profile = parser.has("ncnn-profile");
	}
	catch (const std::exception& ex)
	{
		std::cerr << ex.what() << '\n';
		return 1;
	}

	const auto siteRoot = resolveSiteRoot(site_root_arg);
	if (!fs::exists(siteRoot))
	{
		LOG_ERROR << "Site root not found: " << siteRoot;
		return 1;
	}

	const auto gestureRoot = resolveGestureRoot(gesture_root_arg);
	const auto configRoot = resolveConfigRoot(config_root_arg);

	auto& app_state = wave::AppState::instance();
	auto& app = drogon::app();

	app_state.setConfigRoot(configRoot.string());
	app_state.setGestureRoot(gestureRoot.string());
	app_state.setServerStartedAt(std::chrono::steady_clock::now());
	if (ncnn_profile)
	{
		app_state.setNcnnProfilingEnabled(true);
		LOG_INFO << "wave-server: NCNN inference profiling enabled";
	}
	LOG_INFO << "wave-server: loading gesture repository from " << gestureRoot;
	if (!app_state.loadRepository())
		LOG_WARN << "Gesture repository not loaded from " << gestureRoot;
	else
		LOG_INFO << "wave-server: gesture repository ready";

	LOG_INFO << "wave-server: loading appliances from " << configRoot;
	std::string appliance_config_error;
	if (!app_state.loadAppliancesConfig(&appliance_config_error))
		LOG_WARN << "Appliance config not loaded: " << appliance_config_error;
	else
	{
		LOG_INFO << "wave-server: appliance config ready";
		app_state.startIoTWorker();
		std::thread([] {
			LOG_INFO << "wave-server: priming appliance connections (background)";
			wave::AppState::instance().applianceManager().primeConnections();
			LOG_INFO << "wave-server: appliance prime finished";
		}).detach();
	}

	LOG_INFO << "wave-server: loading server state from " << configRoot;
	std::string server_state_error;
	if (!app_state.loadServerState(&server_state_error))
		LOG_WARN << "Server state not loaded: " << server_state_error;
	else
		LOG_INFO << "wave-server: server state ready";

	app.setDocumentRoot(siteRoot.string());
	app.setFileTypes(
		{"html", "js", "css", "png", "jpg", "jpeg", "svg", "ico",
		 "json", "woff", "woff2", "map", "txt", "webm", "mp4"});

	app.registerPreRoutingAdvice(
		[](const drogon::HttpRequestPtr& req, drogon::AdviceCallback&& acb, drogon::AdviceChainCallback&& accb)
		{
			static constexpr std::string_view kPrefix = "/api/v1/gesture-media/";
			const auto& path = req->path();
			if (path.rfind(kPrefix, 0) != 0)
			{
				accb();
				return;
			}
			if (req->method() != drogon::Get && req->method() != drogon::Head)
			{
				accb();
				return;
			}

			const std::string rest = path.substr(kPrefix.size());
			const auto slash = rest.find('/');
			if (slash == std::string::npos || slash + 1 >= rest.size())
			{
				acb(drogon::HttpResponse::newNotFoundResponse());
				return;
			}

			const std::string set_id = rest.substr(0, slash);
			const std::string rel = rest.substr(slash + 1);
			const fs::path file = fs::path(wave::AppState::instance().gestureRoot()) / set_id / rel;
			
			if (!fs::exists(file))
			{
				acb(drogon::HttpResponse::newNotFoundResponse());
				return;
			}
			acb(drogon::HttpResponse::newFileResponse(file.string()));
		});

	const auto index_path = siteRoot / "index.html";
	if (fs::exists(index_path))
	{
		app.registerPreRoutingAdvice(
			[index_path](const drogon::HttpRequestPtr& req, drogon::AdviceCallback&& acb, drogon::AdviceChainCallback&& accb)
			{
				if (req->method() != drogon::Get && req->method() != drogon::Head)
				{
					accb();
					return;
				}

				const auto& path = req->path();
				if (path.find('.') != std::string::npos || path.rfind("/api/", 0) == 0)
				{
					accb();
					return;
				}
				acb(drogon::HttpResponse::newFileResponse(index_path.string()));
			});
	}

	app.setThreadNum(kMaxThreads);
	app.addListener("0.0.0.0", port);

	LOG_INFO << "wave-server: http://0.0.0.0:" << port
			 << "  site=" << siteRoot
			 << "  gesture_set=" << gestureRoot
			 << "  config=" << configRoot;
	app_state.appendDevLog(
		"info",
		"wave-server started, port=" + std::to_string(port));

	if (fs::exists(gestureRoot))
		app_state.startSensorPipeline(gestureRoot.string());
	else
		LOG_WARN << "Gesture set root not found: " << gestureRoot;

	app.run();
	app_state.stopSensorPipeline();
	app_state.stopIoTWorker();

	return 0;
}
