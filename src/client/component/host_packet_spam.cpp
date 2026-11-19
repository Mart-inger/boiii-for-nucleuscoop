#include <std_include.hpp>

#include <loader/component_loader.hpp>

#include <game/game.hpp>
#include <game/utils.hpp>

#include "game_event.hpp"
#include "host_packet_spam.hpp"

#include <utils/flags.hpp>
#include <utils/string.hpp>

#include <mutex>
#include <string>
#include <unordered_map>

/*
  Player-hosted matches get the same `luinotify` flood that
  dedicated/networking.cpp filters on dedicated servers (see the comment
  there): custom maps and big mods update their HUD from server scripts every
  ~50ms, each update is a reliable command to every client, and a client still
  on the loading blackscreen can't process them. After 128 it is dropped with
  EXE_ERR_RELIABLE_CYCLED_OUT. The dedicated server only filters them with
  -mitigatepacketspam; a player hosting a match did not filter them at all.

  Same filter, on by default for the hosting player: skip a `luinotify` that
  was already sent to that client less than a second ago, or that repeats the
  last command sent to it. Disable with -nomitigatepacketspam. The hook itself
  is patches.cpp's SV_AddServerCommand hook (a function can only be hooked
  once), which also skips repeated configstrings.
*/
namespace host_packet_spam {
namespace {
bool enabled = false;

std::mutex history_mutex;
// Reliable command string -> xuid -> svs->time it was last sent
std::unordered_map<std::string, std::unordered_map<game::XUID, uint32_t>>
    luinotify_last_sent;
// xuid -> last reliable command string sent
std::unordered_map<game::XUID, std::string> client_last_cmd;

void clear_history() {
  std::scoped_lock lock(history_mutex);
  luinotify_last_sent.clear();
  client_last_cmd.clear();
}

inline constexpr const str<2> LUI_NOTIFY_RELIABLE_CMD_PREFIX = {
    static_cast<char>(game::sv::ReliableCommand::LUI_NOTIFY), ' '};
} // namespace

bool is_redundant_luinotify(const game::sv::client_s *client,
                            const char *command) {
  // Only while hosting a real match: the frontend's local server (also while
  // switching mode or loading a mod) is left alone
  if (!enabled || !client || !command || game::com::Com_IsRunningUILevel()) {
    return false;
  }

  const std::string cmd_str = command;
  const uint32_t now = game::sv::svs->time;

  std::scoped_lock lock(history_mutex);

  // `luinotify` commands have the format "D %d %d %d %d" or "D %d %d %d"
  if (utils::string::starts_with(cmd_str, LUI_NOTIFY_RELIABLE_CMD_PREFIX)) {
    const auto sent = luinotify_last_sent.find(cmd_str);
    if (sent != luinotify_last_sent.end()) {
      const auto client_sent = sent->second.find(client->xuid);
      if (client_sent != sent->second.end() &&
          now - client_sent->second < 1000) {
        return true;
      }
    }

    // No menu state changed in between, so repeating the last command is
    // redundant even if it was sent more than a second ago
    const auto last = client_last_cmd.find(client->xuid);
    if (last != client_last_cmd.end() && last->second == cmd_str) {
      return true;
    }
  }

  luinotify_last_sent[cmd_str][client->xuid] = now;
  client_last_cmd[client->xuid] = cmd_str;
  return false;
}

struct component final : client_component {
  DEFINE_COMPONENT_NAME("host_packet_spam");

  void post_unpack() override {
    if (utils::flags::has_flag("nomitigatepacketspam")) {
      return;
    }

    enabled = true;
    game_event::on_any(clear_history);
  }
};
} // namespace host_packet_spam

REGISTER_COMPONENT(host_packet_spam::component)
