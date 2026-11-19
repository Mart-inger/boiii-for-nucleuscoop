#include <std_include.hpp>

#include "auth.hpp"
#include "local_instances.hpp"
#include "network.hpp"
#include "party.hpp"

#include <game/game.hpp>

#include <utils/cryptography.hpp>
#include <utils/string.hpp>

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <set>

#pragma comment(lib, "iphlpapi.lib")

namespace local_instances {
namespace {
// Instances are expected on consecutive ports starting at the default port
constexpr uint16_t first_port = 27017;
constexpr uint16_t port_count = 8;

// Entries not refreshed within this time are dropped
constexpr std::chrono::seconds entry_timeout = 7s;

struct entry {
  instance inst;
  bool local; // reachable via 127.0.0.1
  std::chrono::steady_clock::time_point last_seen;
};

std::mutex instances_mutex;
std::map<game::XUID, entry> instances; // keyed by xuid to merge duplicates
std::set<uint16_t> pending_ports;

// IPv4 addresses (network byte order) of this machine and the directed
// broadcast address of each network it is on
std::mutex network_mutex;
std::set<uint32_t> own_addresses;
std::set<uint32_t> broadcast_addresses;

const std::string &get_broadcast_challenge() {
  static const std::string challenge =
      "boiii-lan-" + utils::cryptography::random::get_challenge();
  return challenge;
}

void refresh_network_addresses() {
  std::set<uint32_t> own{};
  std::set<uint32_t> broadcast{};

  ULONG size = 16 * 1024;
  std::vector<uint8_t> buffer(size);
  constexpr ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                          GAA_FLAG_SKIP_DNS_SERVER;

  auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
  ULONG result = GetAdaptersAddresses(AF_INET, flags, nullptr, adapters, &size);
  if (result == ERROR_BUFFER_OVERFLOW) {
    buffer.resize(size);
    adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
    result = GetAdaptersAddresses(AF_INET, flags, nullptr, adapters, &size);
  }

  if (result == NO_ERROR) {
    for (auto *adapter = adapters; adapter; adapter = adapter->Next) {
      if (adapter->OperStatus != IfOperStatusUp ||
          adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
        continue;
      }

      for (auto *unicast = adapter->FirstUnicastAddress; unicast;
           unicast = unicast->Next) {
        const auto *addr =
            reinterpret_cast<const sockaddr_in *>(unicast->Address.lpSockaddr);
        if (addr->sin_family != AF_INET) {
          continue;
        }

        const uint32_t ip = addr->sin_addr.s_addr;
        own.insert(ip);

        // No broadcast on self-assigned (169.254.x.x) addresses: nothing to
        // find there
        const uint8_t prefix = unicast->OnLinkPrefixLength;
        const bool link_local = (ntohl(ip) >> 16) == 0xA9FE;
        if (!link_local && prefix > 0 && prefix < 31) {
          const uint32_t mask = htonl(~0u << (32 - prefix));
          broadcast.insert(ip | ~mask);
        }
      }
    }
  }

  // The global broadcast only as a fallback: on top of the per-network ones it
  // just makes every instance answer the same scan twice
  if (broadcast.empty()) {
    broadcast.insert(htonl(INADDR_BROADCAST));
  }

  std::lock_guard lock(network_mutex);
  own_addresses = std::move(own);
  broadcast_addresses = std::move(broadcast);
}

bool is_own_address(const uint32_t ip) {
  std::lock_guard lock(network_mutex);
  return own_addresses.contains(ip);
}

void update_instance(const game::net::netadr_t &address, const bool local,
                     const utils::info_string &info) {
  const game::XUID xuid = strtoull(info.get("xuid").data(), nullptr, 16);
  if (info.get("gamename") != "T7" || xuid == 0 || xuid == auth::get_guid()) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();

  std::lock_guard lock(instances_mutex);

  entry &e = instances[xuid];
  const bool keep_local_address =
      !local && e.local && now - e.last_seen < entry_timeout;

  std::string name = info.get("hostname");
  if (name.empty()) {
    name = network::address_to_string(address);
  }

  e.inst.name = std::move(name);
  // in_match from newer builds; sv_running is also 1 in the menus
  const std::string in_match = info.get("in_match");
  e.inst.hosting =
      in_match.empty() ? info.get("sv_running") == "1" : in_match == "1";

  const std::string playmode = info.get("playmode");
  e.inst.mode = playmode == "0"   ? "ZM"
                : playmode == "1" ? "MP"
                : playmode == "2" ? "CP"
                                  : "";

  e.inst.clients = atoi(info.get("clients").data());
  const std::string host_max = info.get("host_maxclients");
  e.inst.max_clients =
      atoi((host_max.empty() ? info.get("sv_maxclients") : host_max).data());
  e.last_seen = now;

  // Prefer 127.0.0.1 for instances on this machine
  if (!keep_local_address) {
    e.local = local;
    e.inst.address = local ? utils::string::va("127.0.0.1:%hu", address.port)
                           : network::address_to_string(address);
    e.inst.lan = !local && !is_own_address(address.addr);
  }
}

void query_localhost() {
  const uint16_t own_port = party::get_local_port();

  for (uint16_t port = first_port; port < first_port + port_count; ++port) {
    if (port == own_port) {
      continue;
    }

    {
      std::lock_guard lock(instances_mutex);
      if (!pending_ports.insert(port).second) {
        continue;
      }
    }

    party::query_server(network::address_from_ip(htonl(INADDR_LOOPBACK), port),
                        [port](const bool success,
                               const game::net::netadr_t &host,
                               const utils::info_string &info, uint32_t) {
                          {
                            std::lock_guard lock(instances_mutex);
                            pending_ports.erase(port);
                          }

                          if (success) {
                            update_instance(host, true, info);
                          }
                        });
  }
}

void query_lan() {
  std::set<uint32_t> targets;
  {
    std::lock_guard lock(network_mutex);
    targets = broadcast_addresses;
  }

  for (const uint32_t ip : targets) {
    for (uint16_t port = first_port; port < first_port + port_count; ++port) {
      network::send(network::address_from_ip(ip, port), "getInfo",
                    get_broadcast_challenge());
    }
  }
}

// Scans happen only while the connect menu is open (it calls refresh() every
// second). Scanning all the time made every instance in the menus query all
// the others every 2s for a list nobody was looking at.
constexpr std::chrono::milliseconds min_scan_interval = 900ms;
// A new menu session re-reads the network adapters (VPN connected, Wi-Fi
// changed, ...); within one it's enough every 15s
constexpr std::chrono::seconds network_refresh_interval = 15s;
} // namespace

void refresh() {
  // The connect menu lives in the menus
  if (!game::com::Com_IsRunningUILevel()) {
    return;
  }

  static std::chrono::steady_clock::time_point last_scan{};
  static std::chrono::steady_clock::time_point last_network_refresh{};

  const auto now = std::chrono::steady_clock::now();
  if (now - last_scan < min_scan_interval) {
    return;
  }

  // A long gap means the menu was closed in between
  if (now - last_scan > 5s ||
      now - last_network_refresh > network_refresh_interval) {
    refresh_network_addresses();
    last_network_refresh = now;
  }
  last_scan = now;

  query_localhost();
  query_lan();
}

std::vector<instance> get_instances() {
  const auto now = std::chrono::steady_clock::now();

  std::lock_guard lock(instances_mutex);

  std::vector<instance> result;
  for (auto i = instances.begin(); i != instances.end();) {
    if (now - i->second.last_seen >= entry_timeout) {
      i = instances.erase(i);
      continue;
    }

    result.push_back(i->second.inst);
    ++i;
  }

  // This machine's instances first, then by name
  std::ranges::sort(result, [](const instance &a, const instance &b) {
    if (a.lan != b.lan) {
      return !a.lan;
    }
    return a.name < b.name;
  });

  return result;
}

bool handle_broadcast_response(const game::net::netadr_t &target,
                               const utils::info_string &info) {
  if (info.get("challenge") != get_broadcast_challenge()) {
    return false;
  }

  update_instance(target, false, info);
  return true;
}
} // namespace local_instances
