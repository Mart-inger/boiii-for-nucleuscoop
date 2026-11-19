#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <format>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
namespace fixture {
std::array<bool, 256> keys{};
HWND foreground = reinterpret_cast<HWND>(1);
DWORD foreground_pid = GetCurrentProcessId();
SHORT get_key(int vk) { return keys[vk] ? static_cast<SHORT>(0x8000) : 0; }
HWND get_foreground() { return foreground; }
DWORD window_pid(HWND hwnd, DWORD *pid) {
  *pid = foreground_pid;
  return hwnd && foreground_pid ? 1 : 0;
}
bool server = true, ui = false, disabled = false;
std::string map = "zm_test";
std::vector<std::string> commands;
int schedules = 0, toasts = 0;
}
namespace game {
enum { LOCAL_CLIENT_0 };
enum class connstate_t { CONNECTING, ACTIVE };
namespace net { struct netadr_t { uint32_t addr; uint16_t port; bool ip = true; }; }
namespace cl { inline int connection; inline void *slot = &connection; inline void **clientConnections = &slot; }
namespace cg {
struct active { connstate_t connectionState = connstate_t::ACTIVE; };
struct states { active actives[1]; };
inline states state;
inline states *clientUIActives = &state;
}
std::optional<std::string_view> get_mapname() { return fixture::map; }
bool server_running() { return fixture::server; }
namespace com { bool Com_IsRunningUILevel() { return fixture::ui; } }
namespace cbuf { void Cbuf_AddText(int, const char *text) { fixture::commands.emplace_back(text); } }
}
namespace party {
inline uint16_t port = 31071;
inline game::net::netadr_t server{htonl(INADDR_LOOPBACK), 31071};
uint16_t get_local_port() { return port; }
game::net::netadr_t get_connected_server() { return server; }
}
namespace network { bool is_ip_address(const game::net::netadr_t &address) { return address.ip; } }
namespace toast { void info(const char *, const char *) { ++fixture::toasts; } }
namespace scheduler {
enum { main };
void loop(const std::function<void()> &, int, std::chrono::milliseconds) { ++fixture::schedules; }
}
namespace utils {
namespace flags { bool has_flag(const char *) { return fixture::disabled; } }
namespace string {
const char *va(const char *format, ...) {
  static char buffer[512]; va_list args; va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args); va_end(args); return buffer;
}
}
}
struct client_component { virtual void post_unpack() {} virtual void pre_destroy() {} virtual ~client_component() = default; };
#define DEFINE_COMPONENT_NAME(...)
#define REGISTER_COMPONENT(...)
#define GetForegroundWindow fixture::get_foreground
#define GetWindowThreadProcessId fixture::window_pid
#define GetAsyncKeyState fixture::get_key
