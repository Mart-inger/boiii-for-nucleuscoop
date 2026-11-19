#include <std_include.hpp>

#include <loader/component_loader.hpp>

#include <game/game.hpp>
#include <game/utils.hpp>

#include "network.hpp"
#include "party.hpp"
#include "scheduler.hpp"
#include "toast.hpp"

#include <utils/flags.hpp>
#include <utils/string.hpp>

/*
  F3: fast_restart, F4: map_restart, for the player hosting the match.
  Every instance reads the same physical keys. A local session marker lets
  the host recognize a focused client in its match, without forwarding any
  commands over the network or restarting unrelated matches on this PC.
  No window titles, instance numbers, port ranges or binds.cfg edits needed.
  Disable publishing and receiving with -nohosthotkeys.
*/
namespace host_hotkeys {
namespace {
HANDLE session_marker{};
std::wstring published_session;
bool marker_error_reported = false;

uint32_t normalize_address(const uint32_t address) {
  // All of 127/8 is local, not just the usual 127.0.0.1.
  return (ntohl(address) >> 24) == 127 ? htonl(INADDR_LOOPBACK) : address;
}

std::wstring session_name(const DWORD pid, const uint32_t address,
                          const uint16_t port) {
  // Local\\ is confined to this Windows login session, never another PC.
  return std::format(L"Local\\BOIII.RestartHotkeys.v1.{}.{}.{}", pid,
                     normalize_address(address), port);
}

bool in_match() {
  const auto map = game::get_mapname();
  return map && !map->empty() && *map != "core_frontend" &&
         !game::com::Com_IsRunningUILevel() &&
         *game::cl::clientConnections != nullptr &&
         game::cg::clientUIActives->actives[game::LOCAL_CLIENT_0]
                 .connectionState == game::connstate_t::ACTIVE;
}

bool hosting_match() { return game::server_running() && in_match(); }

void close_session_marker() {
  if (session_marker) {
    CloseHandle(session_marker);
    session_marker = nullptr;
  }
  published_session.clear();
}

void publish_session() {
  std::wstring name;
  if (in_match()) {
    if (game::server_running()) {
      name = session_name(GetCurrentProcessId(), htonl(INADDR_LOOPBACK),
                          party::get_local_port());
    } else {
      const auto server = party::get_connected_server();
      if (network::is_ip_address(server)) {
        name = session_name(GetCurrentProcessId(), server.addr, server.port);
      }
    }
  }

  if (name == published_session) {
    return;
  }
  close_session_marker();
  if (!name.empty()) {
    // The mutex is only a presence marker; never acquire it or wait on it.
    session_marker = CreateMutexW(nullptr, FALSE, name.c_str());
    if (session_marker) {
      published_session = std::move(name);
      marker_error_reported = false;
    } else if (!marker_error_reported) {
      printf("[Hotkeys] Could not publish the local match marker (error %lu)\n",
             GetLastError());
      marker_error_reported = true;
    }
  }
}

bool has_session_marker(const DWORD pid, const uint32_t address,
                        const uint16_t port) {
  const auto name = session_name(pid, address, port);
  const HANDLE marker = OpenMutexW(SYNCHRONIZE, FALSE, name.c_str());
  if (!marker) {
    return false;
  }
  CloseHandle(marker);
  return true;
}

bool focused_instance_is_in_our_match(const HWND foreground) {
  DWORD pid = 0;
  if (!foreground || !GetWindowThreadProcessId(foreground, &pid)) {
    return false;
  }
  if (pid == GetCurrentProcessId()) {
    return true;
  }

  const uint16_t port = party::get_local_port();
  if (has_session_marker(pid, htonl(INADDR_LOOPBACK), port)) {
    return true;
  }

  // A same-PC client can join through this PC's LAN/VPN address too. Match
  // both IP and port: a remote host can use the very same port as this one.
  ULONG size = 0;
  if (GetIpAddrTable(nullptr, &size, FALSE) != ERROR_INSUFFICIENT_BUFFER) {
    return false;
  }
  std::vector<uint8_t> buffer(size);
  auto *addresses = reinterpret_cast<MIB_IPADDRTABLE *>(buffer.data());
  if (GetIpAddrTable(addresses, &size, FALSE) != NO_ERROR) {
    return false;
  }
  for (DWORD i = 0; i < addresses->dwNumEntries; ++i) {
    const uint32_t address = addresses->table[i].dwAddr;
    if (address && has_session_marker(pid, address, port)) {
      return true;
    }
  }
  return false;
}

bool key_down(const int vk) {
  // Do not use the low bit: other processes consume it when reading the key.
  return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool modifier_down() {
  return key_down(VK_MENU) || key_down(VK_CONTROL) || key_down(VK_SHIFT) ||
         key_down(VK_LWIN) || key_down(VK_RWIN);
}

void run_restart(const char *command, const char *label) {
  if (!hosting_match()) {
    // Non-host instances do nothing, including when the real host is remote.
    return;
  }

  printf("[Hotkeys] %s\n", command);
  toast::info(label, "Restarting the match...");
  game::cbuf::Cbuf_AddText(game::LOCAL_CLIENT_0,
                           utils::string::va("%s\n", command));
}

void poll_keys() {
  publish_session();

  static HWND previous_foreground{};
  static bool armed = false;
  static std::chrono::steady_clock::time_point last_restart{};
  const HWND foreground = GetForegroundWindow();
  if (foreground != previous_foreground) {
    previous_foreground = foreground;
    armed = false;
  }

  if (!foreground || !hosting_match() || modifier_down()) {
    armed = false;
    return;
  }

  const bool fast_down = key_down(VK_F3);
  const bool full_down = key_down(VK_F4);
  if (!fast_down && !full_down) {
    armed = true;
    return;
  }
  if (!armed) {
    return;
  }
  // Release both keys before another request, even across focus changes or
  // Alt+F4. If both are pressed together, prefer one full restart.
  armed = false;
  const auto now = std::chrono::steady_clock::now();
  if (now - last_restart < 1s ||
      !focused_instance_is_in_our_match(foreground)) {
    return;
  }
  last_restart = now;
  run_restart(full_down ? "map_restart" : "fast_restart",
              full_down ? "Map restart" : "Fast restart");
}
} // namespace

struct component final : client_component {
  DEFINE_COMPONENT_NAME("host_hotkeys");

  void post_unpack() override {
    if (utils::flags::has_flag("nohosthotkeys")) {
      return;
    }

    scheduler::loop(poll_keys, scheduler::main, 16ms);
  }

  void pre_destroy() override { close_session_marker(); }
};
} // namespace host_hotkeys

REGISTER_COMPONENT(host_hotkeys::component)
