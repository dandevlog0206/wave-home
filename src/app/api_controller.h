#pragma once

#include <drogon/HttpController.h>
#include "../core/coredef.h"

WAVE_NAMESPACE_BEGIN

class ApiController : public drogon::HttpController<ApiController>
{
public:
	METHOD_LIST_BEGIN
	ADD_METHOD_TO(ApiController::dashboardSummary, "/api/v1/dashboard/summary", drogon::Get);
	ADD_METHOD_TO(ApiController::gestureHistory, "/api/v1/gestures/history", drogon::Get);
	ADD_METHOD_TO(ApiController::gestureSets, "/api/v1/gesture-sets", drogon::Get);
	ADD_METHOD_TO(ApiController::gestureSetDetail, "/api/v1/gesture-sets/{setId}", drogon::Get);
	ADD_METHOD_TO(ApiController::setActiveGestureSet, "/api/v1/gesture-sets/active", drogon::Put);
	ADD_METHOD_TO(ApiController::devices, "/api/v1/devices", drogon::Get);
	ADD_METHOD_TO(
		ApiController::testDeviceControl,
		"/api/v1/devices/{deviceId}/controls/{controlId}/test",
		drogon::Post);
	ADD_METHOD_TO(ApiController::bindings, "/api/v1/bindings", drogon::Get);
	ADD_METHOD_TO(ApiController::putBinding, "/api/v1/bindings", drogon::Put);
	ADD_METHOD_TO(ApiController::deleteBinding, "/api/v1/bindings", drogon::Delete);
	ADD_METHOD_TO(ApiController::deleteAllBindings, "/api/v1/bindings/all", drogon::Delete);
	METHOD_LIST_END

	using HTTPCallback = std::function<void(const drogon::HttpResponsePtr&)>;
	
	void dashboardSummary(const drogon::HttpRequestPtr& req, HTTPCallback&& callback);
	void gestureHistory(const drogon::HttpRequestPtr& req, HTTPCallback&& callback);
	void gestureSets(const drogon::HttpRequestPtr& req, HTTPCallback&& callback);
	void gestureSetDetail(const drogon::HttpRequestPtr& req, HTTPCallback&& callback, const std::string& setId);
	void setActiveGestureSet(const drogon::HttpRequestPtr& req, HTTPCallback&& callback);
	void devices(const drogon::HttpRequestPtr& req, HTTPCallback&& callback);
	void testDeviceControl(
		const drogon::HttpRequestPtr& req,
		HTTPCallback&& callback,
		const std::string& deviceId,
		const std::string& controlId);
	void bindings(const drogon::HttpRequestPtr& req, HTTPCallback&& callback);
	void putBinding(const drogon::HttpRequestPtr& req, HTTPCallback&& callback);
	void deleteBinding(const drogon::HttpRequestPtr& req, HTTPCallback&& callback);
	void deleteAllBindings(const drogon::HttpRequestPtr& req, HTTPCallback&& callback);
};

WAVE_NAMESPACE_END