#include "ws_controller.h"

#include "app_state.h"

WAVE_NAMESPACE_BEGIN

void WebSocketController::handleNewConnection(
	const drogon::HttpRequestPtr&,
	const drogon::WebSocketConnectionPtr& conn)
{
	AppState::instance().registerDevSocket(conn);
	if (conn && conn->connected())
		conn->send(AppState::instance().buildDevJson());
}

void WebSocketController::handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn)
{
	AppState::instance().unregisterDevSocket(conn);
}

WAVE_NAMESPACE_END