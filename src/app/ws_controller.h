#pragma once

#include <drogon/WebSocketController.h>
#include "../core/coredef.h"

WAVE_NAMESPACE_BEGIN

class WebSocketController : public drogon::WebSocketController<WebSocketController>
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

WAVE_NAMESPACE_END
