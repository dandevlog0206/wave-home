#pragma once

#include <string>

namespace network
{
	struct LocalNetworkInfo
	{
		std::string address;
		std::string subnetMask;
	};

	bool getLocalNetworkInfo(LocalNetworkInfo& out_info);
	bool getMacAddress(const std::string& ip, std::string& out_mac);
}
