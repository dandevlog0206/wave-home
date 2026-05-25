#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "coredef.h"

WAVE_NAMESPACE_BEGIN
CORE_NAMESPACE_BEGIN
LOCALE_NAMESPACE_BEGIN

struct Param
{
	std::string name;
	std::string value;
};

using Params = std::vector<Param>;

namespace key
{
	inline constexpr std::string_view kCommonUnknown = "common.unknown";
	inline constexpr std::string_view kCommonLoading = "common.loading";

	inline constexpr std::string_view kRadarStatusOk = "radar.status.ok";
	inline constexpr std::string_view kRadarStatusConnecting = "radar.status.connecting";
	inline constexpr std::string_view kRadarStatusOffline = "radar.status.offline";
	inline constexpr std::string_view kRadarDetailLive = "radar.detail.live";
	inline constexpr std::string_view kRadarDetailConnecting = "radar.detail.connecting";
	inline constexpr std::string_view kRadarDetailConnectingCountdown = "radar.detail.connectingCountdown";
	inline constexpr std::string_view kRadarDetailDisconnected = "radar.detail.disconnected";
	inline constexpr std::string_view kRadarDetailScanning = "radar.detail.scanning";

	inline constexpr std::string_view kErrorBadRequest = "api.error.badRequest";
	inline constexpr std::string_view kErrorSetIdRequired = "api.error.setIdRequired";
	inline constexpr std::string_view kErrorGestureSetNotFound = "api.error.gestureSetNotFound";
	inline constexpr std::string_view kErrorActivationFailed = "api.error.activationFailed";
	inline constexpr std::string_view kErrorDeviceNotFound = "api.error.deviceNotFound";
	inline constexpr std::string_view kErrorControlFailed = "api.error.controlFailed";
	inline constexpr std::string_view kErrorDeviceIdRequired = "api.error.deviceIdRequired";
	inline constexpr std::string_view kErrorDeviceIdControlIdRequired = "api.error.deviceIdControlIdRequired";
	inline constexpr std::string_view kErrorGestureAlreadyAssigned = "api.error.gestureAlreadyAssigned";

	inline constexpr std::string_view kHistoryGestureRecognition = "history.action.gestureRecognition";
	inline constexpr std::string_view kHistoryControlTriggered = "history.action.controlTriggered";
	inline constexpr std::string_view kHistoryControlTriggeredFailed = "history.action.controlTriggeredFailed";

	inline constexpr std::string_view kDeviceTypeTelevision = "device.type.television";
	inline constexpr std::string_view kDeviceStateUnknown = "device.state.unknown";
	inline constexpr std::string_view kDeviceConnectionOnline = "device.connection.online";
	inline constexpr std::string_view kDeviceConnectionOffline = "device.connection.offline";
	inline constexpr std::string_view kDeviceInputPower = "device.input.power";
	inline constexpr std::string_view kDeviceInputVolumeUp = "device.input.volumeUp";
	inline constexpr std::string_view kDeviceInputVolumeDown = "device.input.volumeDown";
	inline constexpr std::string_view kDeviceInputChannelUp = "device.input.channelUp";
	inline constexpr std::string_view kDeviceInputChannelDown = "device.input.channelDown";
	inline constexpr std::string_view kDeviceInputHome = "device.input.home";

	inline constexpr std::string_view kLogServerStarted = "log.server.started";
	inline constexpr std::string_view kLogGestureSetActivationFailed = "log.gestureSet.activationFailed";
	inline constexpr std::string_view kLogGestureSetActivated = "log.gestureSet.activated";
	inline constexpr std::string_view kLogControlTestSucceeded = "log.control.testSucceeded";
	inline constexpr std::string_view kLogControlTestFailed = "log.control.testFailed";
	inline constexpr std::string_view kLogSensorPipelineStarted = "log.sensor.started";
	inline constexpr std::string_view kLogSensorPipelineStopped = "log.sensor.stopped";
	inline constexpr std::string_view kLogGestureSetLoadFailed = "log.gestureSet.loadFailed";
	inline constexpr std::string_view kLogModelReloaded = "log.model.reloaded";
	inline constexpr std::string_view kLogModelReloadFailed = "log.model.reloadFailed";
	inline constexpr std::string_view kLogRadarDisconnectedRetry = "log.radar.disconnectedRetry";
	inline constexpr std::string_view kLogIotControlSucceeded = "log.iot.controlSucceeded";
	inline constexpr std::string_view kLogIotControlFailed = "log.iot.controlFailed";
	inline constexpr std::string_view kLogHomebridgeInitialLoaded = "log.homebridge.initialLoaded";
	inline constexpr std::string_view kLogHomebridgeLoadFailed = "log.homebridge.loadFailed";
	inline constexpr std::string_view kLogHomebridgeReloaded = "log.homebridge.reloaded";
	inline constexpr std::string_view kLogHomebridgeReloadFailed = "log.homebridge.reloadFailed";
} // namespace key

std::string normalizeTag(std::string_view requested_tag);
std::string resolveTag(std::string_view requested_tag);
bool isSupported(std::string_view requested_tag);

std::string text(std::string_view locale_tag, std::string_view text_key);
std::string format(
	std::string_view locale_tag,
	std::string_view text_key,
	const Params& params = {});

LOCALE_NAMESPACE_END
CORE_NAMESPACE_END
WAVE_NAMESPACE_END