#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

#include <drogon/drogon.h>

#include "app/api_controller.h"
#include "app/app_state.h"
#include "app/ws_controller.h"
#include "app/pipeline_service.h"
#include "device/sensor_pipeline.h"
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
		if (fs::exists(p / "dist" / "index.html"))
			return fs::weakly_canonical(p / "dist");
		return fs::weakly_canonical(p);
	}
#ifdef WAVE_SITE_ROOT
	return fs::path(WAVE_SITE_ROOT) / "dist";
#else
	const auto exe = fs::read_symlink("/proc/self/exe");
	const auto dist = fs::weakly_canonical(exe.parent_path() / ".." / "site" / "dist");
	if (fs::exists(dist / "index.html"))
		return dist;
	return fs::weakly_canonical(exe.parent_path() / ".." / "site");
#endif
}

static fs::path resolveGestureRoot(const std::string& cli_path)
{
	if (!cli_path.empty())
		return fs::weakly_canonical(cli_path);
#ifdef WAVE_GESTURE_SET_ROOT
	return fs::path(WAVE_GESTURE_SET_ROOT);
#else
	const auto exe = fs::read_symlink("/proc/self/exe");
	return fs::weakly_canonical(exe.parent_path() / ".." / "gesture_set");
#endif
}

static ArgParser makeArgParser()
{
	ArgParser parser("wave-server", "Wave Home dashboard server");
	parser.addArgument("--port", "-p")
		.help("HTTP listen port")
		.defaultValue(std::to_string(kDefaultPort));
	parser.addArgument("--site-root")
		.help("Dashboard static site root (dist/)")
		.defaultValue("");
	parser.addArgument("--set-root", "-s")
		.help("Gesture set directory")
		.defaultValue("");
	return parser;
}

int main(int argc, char* argv[])
{
	unsigned port = kDefaultPort;
	std::string site_root_arg;
	std::string gesture_root_arg;

	try
	{
		auto parser = makeArgParser();
		parser.parseArgs(argc, const_cast<const char**>(argv));
		port = parser.get<unsigned>("port");
		site_root_arg = parser.get<std::string>("site-root");
		gesture_root_arg = parser.get<std::string>("set-root");
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

	AppState::instance().setGestureRoot(gestureRoot.string());
	AppState::instance().setServerStartedAt(std::chrono::steady_clock::now());
	if (!AppState::instance().loadRepository())
	{
		LOG_WARN << "Gesture repository not loaded from " << gestureRoot;
	}

	drogon::app().setDocumentRoot(siteRoot.string());
	drogon::app().setFileTypes(
		{"html", "js", "css", "png", "jpg", "jpeg", "svg", "ico",
		 "json", "woff", "woff2", "map", "txt", "webm", "mp4"});

	drogon::app().registerPreRoutingAdvice(
		[](const drogon::HttpRequestPtr& req,
		   drogon::AdviceCallback&& acb,
		   drogon::AdviceChainCallback&& accb) {
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
			const std::filesystem::path file =
				std::filesystem::path(AppState::instance().gestureRoot()) / set_id / rel;
			if (!std::filesystem::exists(file))
			{
				acb(drogon::HttpResponse::newNotFoundResponse());
				return;
			}
			acb(drogon::HttpResponse::newFileResponse(file.string()));
		});

	const auto indexPath = siteRoot / "index.html";
	if (fs::exists(indexPath))
	{
		drogon::app().registerPreRoutingAdvice(
			[indexPath](const drogon::HttpRequestPtr& req,
						drogon::AdviceCallback&& acb,
						drogon::AdviceChainCallback&& accb) {
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
				acb(drogon::HttpResponse::newFileResponse(indexPath.string()));
			});
	}

	drogon::app().setThreadNum(kMaxThreads);
	drogon::app().addListener("0.0.0.0", port);

	LOG_INFO << "wave-server: http://0.0.0.0:" << port
			 << "  site=" << siteRoot << "  gesture_set=" << gestureRoot;

	if (fs::exists(gestureRoot))
		AppState::instance().startSensorPipeline(gestureRoot.string());
	else
		LOG_WARN << "Gesture set root not found: " << gestureRoot;

	drogon::app().run();
	AppState::instance().stopSensorPipeline();

	return 0;
}
