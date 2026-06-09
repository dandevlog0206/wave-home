#include "locale.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

WAVE_NAMESPACE_BEGIN
CORE_NAMESPACE_BEGIN
LOCALE_NAMESPACE_BEGIN

namespace
{
	using Dictionary = std::unordered_map<std::string, std::string>;

	const Dictionary& dictionaryFor(const std::string_view locale_tag)
	{
		static const Dictionary kKoKr = {
			{std::string(key::kCommonUnknown), "알 수 없음"},
			{std::string(key::kCommonLoading), "로딩 중..."},

			{std::string(key::kRadarStatusOk), "정상"},
			{std::string(key::kRadarStatusConnecting), "연결 중"},
			{std::string(key::kRadarStatusOffline), "오프라인"},
			{std::string(key::kRadarDetailLive), "실시간 감지 중"},
			{std::string(key::kRadarDetailConnecting), "연결 시도 중"},
			{std::string(key::kRadarDetailConnectingCountdown), "연결 시도 중... {seconds}s"},
			{std::string(key::kRadarDetailDisconnected), "센서 미연결"},
			{std::string(key::kRadarDetailScanning), "센서 검색 중"},

			{std::string(key::kErrorBadRequest), "잘못된 요청입니다."},
			{std::string(key::kErrorSetIdRequired), "setId가 필요합니다."},
			{std::string(key::kErrorGestureSetNotFound), "제스처 세트를 찾을 수 없습니다."},
			{std::string(key::kErrorActivationFailed), "제스처 세트 모델을 로드하지 못했습니다."},
			{std::string(key::kErrorDeviceNotFound), "장치를 찾을 수 없습니다."},
			{std::string(key::kErrorControlFailed), "MQTT 제어에 실패했습니다."},
			{std::string(key::kErrorDeviceIdRequired), "deviceId가 필요합니다."},
			{std::string(key::kErrorDeviceIdControlIdRequired), "deviceId와 controlId가 필요합니다."},
			{std::string(key::kErrorGestureAlreadyAssigned), "이미 다른 장치에 할당된 제스처입니다."},

			{std::string(key::kHistoryGestureRecognition), "제스처 인식"},
			{std::string(key::kHistoryControlTriggered), "{control}"},
			{std::string(key::kHistoryControlTriggeredFailed), "{control} (MQTT 실패)"},

			{std::string(key::kDeviceTypeTelevision), "TV"},
			{std::string(key::kDeviceStateUnknown), "—"},
			{std::string(key::kDeviceConnectionOnline), "online"},
			{std::string(key::kDeviceConnectionOffline), "offline"},
			{std::string(key::kDeviceInputPower), "전원"},
			{std::string(key::kDeviceInputVolumeUp), "볼륨 올리기"},
			{std::string(key::kDeviceInputVolumeDown), "볼륨 내리기"},
			{std::string(key::kDeviceInputChannelUp), "다음 채널"},
			{std::string(key::kDeviceInputChannelDown), "이전 채널"},
			{std::string(key::kDeviceInputHome), "홈 메뉴"},

			{std::string(key::kLogServerStarted), "wave-server 시작 · 포트 {port}"},
			{std::string(key::kLogGestureSetActivationFailed), "제스처 세트 활성화 실패: {setId}"},
			{std::string(key::kLogGestureSetActivated), "활성 제스처 세트: {setId}"},
			{std::string(key::kLogControlTestSucceeded), "테스트 제어 · {deviceId} / {controlId}"},
			{std::string(key::kLogControlTestFailed), "테스트 제어 실패 · {deviceId} / {controlId}"},
			{std::string(key::kLogSensorPipelineStarted), "센서 파이프라인 시작 · {setId}"},
			{std::string(key::kLogSensorPipelineStopped), "센서 파이프라인 중지"},
			{std::string(key::kLogGestureSetLoadFailed), "제스처 세트 로드 실패 · {setId}"},
			{std::string(key::kLogModelReloaded), "모델 리로드 완료 · {setId}"},
			{std::string(key::kLogModelReloadFailed), "모델 리로드 실패 · {setId}: {reason}"},
			{std::string(key::kLogRadarDisconnectedRetry), "레이더 연결 끊김 · 15초 후 재시도"},
			{std::string(key::kLogIotControlSucceeded), "IoT 제어 · {deviceName} / {control}"},
			{std::string(key::kLogIotControlFailed), "IoT 제어 실패 · {deviceName} / {control}"},
		};

		static const Dictionary kEnUs = {
			{std::string(key::kCommonUnknown), "Unknown"},
			{std::string(key::kCommonLoading), "Loading..."},

			{std::string(key::kRadarStatusOk), "OK"},
			{std::string(key::kRadarStatusConnecting), "Connecting"},
			{std::string(key::kRadarStatusOffline), "Offline"},
			{std::string(key::kRadarDetailLive), "Live detection"},
			{std::string(key::kRadarDetailConnecting), "Connecting"},
			{std::string(key::kRadarDetailConnectingCountdown), "Retrying connection... {seconds}s"},
			{std::string(key::kRadarDetailDisconnected), "Sensor disconnected"},
			{std::string(key::kRadarDetailScanning), "Scanning for sensor"},

			{std::string(key::kErrorBadRequest), "Bad request."},
			{std::string(key::kErrorSetIdRequired), "setId is required."},
			{std::string(key::kErrorGestureSetNotFound), "Gesture set not found."},
			{std::string(key::kErrorActivationFailed), "Failed to load the gesture set model."},
			{std::string(key::kErrorDeviceNotFound), "Device not found."},
			{std::string(key::kErrorControlFailed), "MQTT control failed."},
			{std::string(key::kErrorDeviceIdRequired), "deviceId is required."},
			{std::string(key::kErrorDeviceIdControlIdRequired), "deviceId and controlId are required."},
			{std::string(key::kErrorGestureAlreadyAssigned), "This gesture is already assigned to another device."},

			{std::string(key::kHistoryGestureRecognition), "Gesture recognized"},
			{std::string(key::kHistoryControlTriggered), "{control}"},
			{std::string(key::kHistoryControlTriggeredFailed), "{control} (MQTT failed)"},

			{std::string(key::kDeviceTypeTelevision), "TV"},
			{std::string(key::kDeviceStateUnknown), "-"},
			{std::string(key::kDeviceConnectionOnline), "online"},
			{std::string(key::kDeviceConnectionOffline), "offline"},
			{std::string(key::kDeviceInputPower), "Power"},
			{std::string(key::kDeviceInputVolumeUp), "Volume up"},
			{std::string(key::kDeviceInputVolumeDown), "Volume down"},
			{std::string(key::kDeviceInputChannelUp), "Channel up"},
			{std::string(key::kDeviceInputChannelDown), "Channel down"},
			{std::string(key::kDeviceInputHome), "Home"},

			{std::string(key::kLogServerStarted), "wave-server started · port {port}"},
			{std::string(key::kLogGestureSetActivationFailed), "Failed to activate gesture set: {setId}"},
			{std::string(key::kLogGestureSetActivated), "Active gesture set: {setId}"},
			{std::string(key::kLogControlTestSucceeded), "Test control · {deviceId} / {controlId}"},
			{std::string(key::kLogControlTestFailed), "Test control failed · {deviceId} / {controlId}"},
			{std::string(key::kLogSensorPipelineStarted), "Sensor pipeline started · {setId}"},
			{std::string(key::kLogSensorPipelineStopped), "Sensor pipeline stopped"},
			{std::string(key::kLogGestureSetLoadFailed), "Failed to load gesture set · {setId}"},
			{std::string(key::kLogModelReloaded), "Model reloaded · {setId}"},
			{std::string(key::kLogModelReloadFailed), "Model reload failed · {setId}: {reason}"},
			{std::string(key::kLogRadarDisconnectedRetry), "Radar disconnected · retrying in 15s"},
			{std::string(key::kLogIotControlSucceeded), "IoT control · {deviceName} / {control}"},
			{std::string(key::kLogIotControlFailed), "IoT control failed · {deviceName} / {control}"},
		};

		return resolveTag(locale_tag) == "ko-KR" ? kKoKr : kEnUs;
	}

	std::string replaceAll(std::string source, const std::string& from, const std::string& to)
	{
		size_t pos = 0;
		while ((pos = source.find(from, pos)) != std::string::npos)
		{
			source.replace(pos, from.size(), to);
			pos += to.size();
		}
		return source;
	}
} // namespace

std::string normalizeTag(const std::string_view requested_tag)
{
	std::string out;
	out.reserve(requested_tag.size());
	for (char ch : requested_tag)
	{
		if (ch == '_')
			out.push_back('-');
		else
			out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
	}

	if (out.rfind("ko", 0) == 0)
		return "ko-KR";
	if (out.rfind("en", 0) == 0)
		return "en-US";
	return out;
}

bool isSupported(const std::string_view requested_tag)
{
	const std::string normalized = normalizeTag(requested_tag);
	return normalized == "ko-KR" || normalized == "en-US";
}

std::string resolveTag(const std::string_view requested_tag)
{
	return isSupported(requested_tag) ? normalizeTag(requested_tag) : "en-US";
}

std::string text(const std::string_view locale_tag, const std::string_view text_key)
{
	const auto& dict = dictionaryFor(locale_tag);
	const auto it = dict.find(std::string(text_key));
	if (it != dict.end())
		return it->second;

	const auto& fallback = dictionaryFor("en-US");
	const auto fallback_it = fallback.find(std::string(text_key));
	if (fallback_it != fallback.end())
		return fallback_it->second;
	return std::string(text_key);
}

std::string format(
	const std::string_view locale_tag,
	const std::string_view text_key,
	const Params& params)
{
	std::string out = text(locale_tag, text_key);
	for (const auto& param : params)
		out = replaceAll(out, "{" + param.name + "}", param.value);
	return out;
}

LOCALE_NAMESPACE_END
CORE_NAMESPACE_END
WAVE_NAMESPACE_END