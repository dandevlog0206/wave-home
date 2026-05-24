#include "ws_controller.h"

#include "app_state.h"

void DevWsController::handleNewConnection(
	const drogon::HttpRequestPtr&,
	const drogon::WebSocketConnectionPtr& conn)
{
	AppState::instance().registerDevSocket(conn);
	if (conn && conn->connected())
		conn->send(AppState::instance().buildDevJson());
}

void DevWsController::handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn)
{
	AppState::instance().unregisterDevSocket(conn);
}
