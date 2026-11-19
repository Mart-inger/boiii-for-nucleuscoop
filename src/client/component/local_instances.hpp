#pragma once

#include <game/game.hpp>

#include <utils/info_string.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Discovers other BOIII instances on this machine (e.g. split-screen setups
// launched by Nucleus Co-op) and on the local network by sending getInfo to
// the default port range on 127.0.0.1 and as a LAN broadcast. Used by the
// built-in "Connect" menu.
namespace local_instances {
struct instance {
  std::string name;
  std::string address; // "<ip>:<port>"
  bool hosting;        // in a match that can be joined
  bool lan;            // found on another machine on the network
  std::string mode;    // "ZM", "MP", "CP" or empty if unknown
  int32_t clients;     // players in the match
  int32_t max_clients; // 0 if unknown
};

std::vector<instance> get_instances();

// Scans for instances (at most about once a second). There is no background
// scan: the connect menu calls this while it is open.
void refresh();

// Called for infoResponse packets that don't belong to a regular server query.
// Returns true if the response was an answer to our LAN broadcast.
bool handle_broadcast_response(const game::net::netadr_t &target,
                               const utils::info_string &info);
} // namespace local_instances
