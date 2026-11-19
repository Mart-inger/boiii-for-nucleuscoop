#include <std_include.hpp>

#include <loader/component_loader.hpp>

#include <steam/steam.hpp>

#include "auth.hpp"
#include "nat.hpp"
#include "network.hpp"
#include "network_password.hpp"
#include "party.hpp"
#include "scheduler.hpp"
#include "workshop/workshop.hpp"

#include <utils/info_string.hpp>
#include <utils/string.hpp>

// In case of clangd compilation
#if __has_include("version.hpp")
#include "version.hpp"
#else
#ifndef VERSION
#define VERSION "0"
#endif
#ifndef SHORTVERSION
#define SHORTVERSION "0"
#endif
#endif

#include <game/utils.hpp>

namespace getinfo {
template <typename T> int get_client_count(T *client_states) {
  if (client_states == nullptr) {
    return 0;
  }

  int32_t count = 0;
  for (int32_t i = 0; i < game::get_com_maxclients(); ++i) {
    if (client_states[i].client_state > 0) {
      ++count;
    }
  }

  return count;
}

bool is_host() { return game::server_running(); }

// Max client count the running match actually uses, captured on the match's
// first server frame. Joining clients mirror it (see party.cpp); the dvars
// alone are unreliable while loading (com_maxclients still holds the lobby
// default and sv_maxclients whatever the config says).
std::atomic_int32_t match_max_clients{0};

void capture_match_max_clients() {
  // Only real matches: the frontend also runs a local server, and while the
  // game starts up or reloads the frontend (mode switch, mod load) the UI
  // level isn't running either. Locking the max clients then broke loading.
  const std::string_view mapname = game::get_mapname().value_or("");
  const bool in_match = game::server_running() &&
                        !game::com::Com_IsRunningUILevel() &&
                        !mapname.empty() && mapname != "core_frontend";
  if (!in_match) {
    if (match_max_clients.exchange(0) != 0 && !game::is_server()) {
      party::set_host_match_max_clients(0);
    }
    return;
  }

  if (match_max_clients.load() == 0) {
    match_max_clients = game::get_dvar_int("com_maxclients").value_or(0);
    if (!game::is_server()) {
      // Keep it for the whole match, restarts included
      party::set_host_match_max_clients(match_max_clients.load());
    }
  }
}

int32_t get_host_max_clients() {
  if (const int32_t captured = match_max_clients.load(); captured > 0) {
    return captured;
  }

  // Match not started yet: best guess from the dvars
  int32_t result = 0;
  for (const char *dvar : {"com_maxclients", "sv_maxclients"}) {
    const int32_t value = game::get_dvar_int(dvar).value_or(0);
    if (value > 0 && (result == 0 || value < result)) {
      result = value;
    }
  }
  return result;
}

struct component final : generic_component {
  DEFINE_COMPONENT_NAME("getinfo");

  void post_unpack() override {
    scheduler::loop(capture_match_max_clients, scheduler::pipeline::server);

    game::sv_wwwDownload = game::register_dvar_bool("sv_wwwDownload", false,
                                                    game::dvarFlags_e::NONE,
                                                    "Enable http downloads");
    game::sv_wwwDlDisconnected = game::register_dvar_bool(
        "sv_wwwDlDisconnected", true, game::dvarFlags_e::NONE,
        "Should clients stay connected while downloading?");
    scheduler::once(
        []() {
          const std::string configured =
              std::string(game::get_dvar_string("sv_wwwBaseURL").value_or(""));
          game::sv_wwwBaseURL = game::register_dvar_string(
              "sv_wwwBaseURL", configured.c_str(), game::dvarFlags_e::NONE,
              "The base url for files downloaded via http");
        },
        scheduler::pipeline::main);

    network::on("getInfo", [](const game::net::netadr_t &target,
                              const network::data_view &data,
                              game::LocalClientNum_t clientNum) {
      utils::info_string info{};
      info.set("challenge", std::string{data.begin(), data.end()});
      info.set("gamename", "T7");
      info.set("hostname", game::hostname().value_or(""));
      info.set("gametype", game::gametype().value_or(""));
      /*
         Unsure why this is commented out, but important notes in case of
        future restoration:
          - The `sv_motd` dvar does not exist in the engine.
          - The engine _does_ have:
              - g_motd - most closely matching the usage of `sv_motd`.
                Used to set SV config string index 11 on `G_InitGame`.
                Description is "The message of the day".
                Never seems to be set in engine.
              - `cl_motdstring` - unused after initial registration
              - `motd_enabled` - used for the `LiveStats` MOTD
              - `live_motdEnabled` - used to check if LiveStorage should fetch
                latest pub online data MOTD.

                This may be the same MOTD used later,
                conditionally upon the potentially duplicitous
                `motd_enabled` dvar.

                Unsure currently.
              - `motddelay` - used for the `LiveStats` MOTD

        TL;DR: this should either be set with the value of `g_motd` or
        we should register a new `sv_motd` dvar.
      */
      // info.set("sv_motd", get_dvar_string("sv_motd"));
      info.set("description",
               game::is_server()
                   ? game::get_live_steam_server_description().value_or("")
                   : "");
      info.set("xuid", utils::string::va("%llX", auth::get_guid()));
      info.set("mapname", game::get_mapname().value_or(""));
      info.set("isPrivate", game::password().value_or("").empty() ? "0" : "1");
      info.set("clients", std::to_string(game::connected_client_count()));
      info.set("bots", std::to_string(game::test_client_count()));
      info.set("sv_maxclients", std::to_string(+game::get_max_client_count()));
      info.set("protocol", std::to_string(PROTOCOL));
      info.set("sub_protocol", std::to_string(SUB_PROTOCOL));
      info.set("playmode", std::to_string(static_cast<int32_t>(
                               game::com::Com_SessionMode_GetMode())));
      info.set("gamemode", std::to_string(static_cast<int32_t>(
                               game::com::Com_SessionMode_GetGameMode())));
      info.set("sv_running", game::server_running() ? "1" : "0");
      info.set("xpscale", std::format("{}", game::xp_scale()));
      info.set("gunxpscale", std::format("{}", game::gun_xp_scale()));
      // The menu also runs a local server, so sv_running alone doesn't mean
      // there is a match to join
      info.set("in_match",
               game::server_running() && !game::com::Com_IsRunningUILevel()
                   ? "1"
                   : "0");
      // Joining clients mirror this so their client scripts register the
      // same per-player clientfields as the host (see party.cpp)
      info.set("host_maxclients", std::to_string(get_host_max_clients()));
      info.set("dedicated", game::is_server() ? "1" : "0");
      info.set("lobby", nat::joinable_lobby());
      info.set("hc",
               std::to_string(game::com::gts::Com_GametypeSettings_GetUInt(
                   "hardcoremode", false)));
      info.set("modName", workshop::get_mod_resized_name());
      info.set("modId", workshop::get_mod_publisher_id());
      info.set("rounds_played",
               std::to_string(*game::level::level_rounds_played));
      info.set("shortversion", SHORTVERSION);

      info.set("sv_wwwBaseURL", game::fastdl_uri().value_or(""));
      info.set("sv_wwwBaseUrl", game::fastdl_uri().value_or(""));
      const std::string_view usermapId = game::ugc::active_usermap->publisherId;
      info.set("workshop_id", usermapId);
      info.set("usermapId", usermapId);

      if (network_password::is_password_set()) {
        info.set("net_password_hash",
                 network_password::get_password_hash_string());
      }

      network::send(target, "infoResponse", info.build(), '\n');
    });
  }
};
} // namespace getinfo

REGISTER_COMPONENT(getinfo::component)
