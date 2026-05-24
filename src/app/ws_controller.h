#pragma once

#include <drogon/WebSocketController.h>

class DevWsController : public drogon::WebSocketController<DevWsController>
{
public:
	void handleNewMessage(
		const drogon::WebSocketConnectionPtr&,
		std::string&&,
		const drogon::WebSocketMessageType&) override
	{
	}

	void handleNewConnection(
		const drogon::HttpRequestPtr&,
		const drogon::WebSocketConnectionPtr& conn) override;

	void handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn) override;

	WS_PATH_LIST_BEGIN
	WS_PATH_ADD("/api/v1/dev/stream", drogon::Get);
	WS_PATH_LIST_END
};
