#pragma once
#include <utils/info_string.hpp>

#include <game/game.hpp>

namespace party {

extern game::EngineDependentDvarMut cl_connected_to_dedi;

using query_callback_func = void(bool success, const game::net::netadr_t &host,
                                 const utils::info_string &info, uint32_t ping);
using query_callback = std::function<query_callback_func>;

// Connect directly without passing through the console/browser route hook.
void connect(const game::net::netadr_t &target);

// The callback runs on a network/async thread. With attempts > 1 the query is
// resent every second until the server answers.
void query_server(const game::net::netadr_t &host, query_callback callback,
                  uint32_t attempts = 1);

// The server sent us a disconnect. Rejoins that host by itself when the
// reason is one a retry can fix (not a kick, ban, wrong password...).
void on_server_disconnect(game::LocalClientNum_t localClientNum,
                          const game::net::netadr_t &from, const char *reason);

// Stops a pending connect (server query, waiting for the host to start a
// match, or waiting for a game mode switch).
void cancel_connect();

// The max clients the match this instance hosts started with (0: not hosting
// a match).
void set_host_match_max_clients(int32_t max_clients);

// Before a restart of the hosted match: puts com_maxclients/party_maxplayers
// back to the value the match started with, so the restarted server scripts
// register the same clientfields as the connected clients.
void restore_host_match_max_clients();

game::net::netadr_t get_connected_server(
    game::LocalClientNum_t localClientNum = game::LOCAL_CLIENT_0);

bool is_host(const game::net::netadr_t &addr);

void join_session(const game::net::netadr_t &addr, const std::string &hostname,
                  uint64_t xuid, game::eModes mode);

uint16_t get_local_port();

std::string get_server_hostname();
int get_server_max_clients();
void clear_server_info();

game::net::netadr_t get_connect_host();
bool is_direct_connect();
bool is_dedi_server(const game::net::netadr_t &addr);
} // namespace party
