#include "network.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>
#include <sstream>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/wait.h>
#endif

namespace network
{
	bool getLocalNetworkInfo(LocalNetworkInfo& out_info)
	{
		out_info.address.clear();
		out_info.subnetMask.clear();

#ifdef _WIN32
		ULONG buffer_size = 0;
		if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, nullptr, &buffer_size) != ERROR_BUFFER_OVERFLOW)
			return false;

		std::vector<unsigned char> buffer(buffer_size);
		IP_ADAPTER_ADDRESSES* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
		if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, adapters, &buffer_size) != NO_ERROR)
			return false;

		for (IP_ADAPTER_ADDRESSES* adapter = adapters; adapter != nullptr; adapter = adapter->Next)
		{
			if (adapter->OperStatus != IfOperStatusUp)
				continue;
			for (IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress; unicast != nullptr; unicast = unicast->Next)
			{
				if (unicast->Address.lpSockaddr == nullptr || unicast->Address.lpSockaddr->sa_family != AF_INET)
					continue;

				const sockaddr_in* address = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
				const unsigned long address_host = ntohl(address->sin_addr.s_addr);
				if ((address_host >> 24) == 127)
					continue;

				char ip_buffer[INET_ADDRSTRLEN] = {};
				if (inet_ntop(AF_INET, &address->sin_addr, ip_buffer, sizeof(ip_buffer)) == nullptr)
					continue;

				unsigned long prefix_mask = unicast->OnLinkPrefixLength == 0 ? 0 : (0xFFFFFFFFul << (32 - unicast->OnLinkPrefixLength));
				prefix_mask = htonl(prefix_mask);
				in_addr mask_addr {};
				mask_addr.s_addr = prefix_mask;
				char mask_buffer[INET_ADDRSTRLEN] = {};
				if (inet_ntop(AF_INET, &mask_addr, mask_buffer, sizeof(mask_buffer)) == nullptr)
					continue;

				out_info.address = ip_buffer;
				out_info.subnetMask = mask_buffer;
				return true;
			}
		}
#else
		ifaddrs* interfaces = nullptr;
		if (getifaddrs(&interfaces) != 0)
			return false;

		for (ifaddrs* iface = interfaces; iface != nullptr; iface = iface->ifa_next)
		{
			if (iface->ifa_addr == nullptr || iface->ifa_netmask == nullptr)
				continue;
			if (iface->ifa_addr->sa_family != AF_INET)
				continue;
			if ((iface->ifa_flags & IFF_UP) == 0 || (iface->ifa_flags & IFF_LOOPBACK) != 0)
				continue;

			char ip_buffer[INET_ADDRSTRLEN] = {};
			char mask_buffer[INET_ADDRSTRLEN] = {};
			const sockaddr_in* address = reinterpret_cast<const sockaddr_in*>(iface->ifa_addr);
			const sockaddr_in* mask = reinterpret_cast<const sockaddr_in*>(iface->ifa_netmask);
			if (inet_ntop(AF_INET, &address->sin_addr, ip_buffer, sizeof(ip_buffer)) == nullptr)
				continue;
			if (inet_ntop(AF_INET, &mask->sin_addr, mask_buffer, sizeof(mask_buffer)) == nullptr)
				continue;

			out_info.address = ip_buffer;
			out_info.subnetMask = mask_buffer;
			freeifaddrs(interfaces);
			return true;
		}

		freeifaddrs(interfaces);
#endif
		return false;
	}

	bool getMacAddress(const std::string& ip, std::string& out_mac)
	{
		out_mac.clear();

#ifdef _WIN32
		unsigned long net_table_size = 0;
		if (GetIpNetTable(nullptr, &net_table_size, FALSE) != ERROR_INSUFFICIENT_BUFFER)
			return false;

		std::vector<unsigned char> buffer(net_table_size);
		PMIB_IPNETTABLE ip_net_table = reinterpret_cast<PMIB_IPNETTABLE>(buffer.data());
		if (GetIpNetTable(ip_net_table, &net_table_size, FALSE) != NO_ERROR)
			return false;

		for (unsigned long index = 0; index < ip_net_table->dwNumEntries; ++index)
		{
			const MIB_IPNETROW& row = ip_net_table->table[index];
			char ip_buffer[INET_ADDRSTRLEN] = {};
			in_addr addr {};
			addr.S_un.S_addr = row.dwAddr;
			if (inet_ntop(AF_INET, &addr, ip_buffer, sizeof(ip_buffer)) == nullptr)
				continue;

			if (ip != ip_buffer)
				continue;

			if (row.dwPhysAddrLen < 6)
				continue;

			char mac_buffer[18] = {};
			snprintf(mac_buffer, sizeof(mac_buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
				row.bPhysAddr[0], row.bPhysAddr[1], row.bPhysAddr[2],
				row.bPhysAddr[3], row.bPhysAddr[4], row.bPhysAddr[5]);
			out_mac = mac_buffer;
			return true;
		}

		return false;

#elif defined(__APPLE__)
		FILE* pipe = popen("arp -a", "r");
		if (pipe == nullptr)
			return false;

		char line_buffer[256] = {};
		while (fgets(line_buffer, sizeof(line_buffer), pipe) != nullptr)
		{
			char host_ip[INET_ADDRSTRLEN] = {};
			char mac_str[18] = {};
			if (sscanf(line_buffer, "%*s (%[^)])", host_ip) != 1)
				continue;

			if (ip != host_ip)
				continue;

			if (sscanf(line_buffer, "%*s at %17s", mac_str) != 1)
				continue;

			std::string mac_result;
			for (size_t pos = 0; mac_str[pos] != '\0'; ++pos)
			{
				const char ch = mac_str[pos];
				if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F') || ch == ':')
				{
					mac_result.push_back(ch);
				}
			}

			if (mac_result.empty() || std::count(mac_result.begin(), mac_result.end(), ':') != 5)
				continue;

			pclose(pipe);
			out_mac = mac_result;
			return true;
		}

		pclose(pipe);
		return false;

#else
		char line_buffer[256] = {};
		FILE* proc_file = fopen("/proc/net/arp", "r");
		if (proc_file == nullptr)
		{
			FILE* arp_pipe = popen("arp -a", "r");
			if (arp_pipe == nullptr)
				return false;

			while (fgets(line_buffer, sizeof(line_buffer), arp_pipe) != nullptr)
			{
				char host_ip[INET_ADDRSTRLEN] = {};
				char mac_str[18] = {};
				if (sscanf(line_buffer, "%*s (%[^)])", host_ip) != 1)
					continue;

				if (ip != host_ip)
					continue;

				if (sscanf(line_buffer, "%*s at %17s", mac_str) != 1)
					continue;

				std::string mac_result;
				for (size_t pos = 0; mac_str[pos] != '\0'; ++pos)
				{
					const char ch = mac_str[pos];
					if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F') || ch == ':')
					{
						mac_result.push_back(ch);
					}
				}

				if (mac_result.empty() || std::count(mac_result.begin(), mac_result.end(), ':') != 5)
					continue;

				pclose(arp_pipe);
				out_mac = mac_result;
				return true;
			}

			pclose(arp_pipe);
			return false;
		}

		fgets(line_buffer, sizeof(line_buffer), proc_file);

		while (fgets(line_buffer, sizeof(line_buffer), proc_file) != nullptr)
		{
			char ip_buffer[INET_ADDRSTRLEN] = {};
			char hw_addr[18] = {};
			unsigned int hw_type = 0;
			unsigned int flags = 0;

			if (sscanf(line_buffer, "%15s 0x%x 0x%x %17s", ip_buffer, &hw_type, &flags, hw_addr) < 4)
				continue;

			if (ip != ip_buffer)
				continue;

			if (hw_type != 1)
				continue;

			std::string mac_result;
			for (size_t pos = 0; hw_addr[pos] != '\0'; ++pos)
			{
				const char ch = hw_addr[pos];
				if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F') || ch == ':')
				{
					mac_result.push_back(ch);
				}
			}

			if (mac_result.empty() || std::count(mac_result.begin(), mac_result.end(), ':') != 5)
				continue;

			fclose(proc_file);
			out_mac = mac_result;
			return true;
		}

		fclose(proc_file);
		return false;
#endif
	}
}
