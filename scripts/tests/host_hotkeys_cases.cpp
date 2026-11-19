#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#undef GetAsyncKeyState

void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
  std::cout << "PASS: " << message << std::endl;
}
struct peer {
  HANDLE process{}, ready{}, stop{};
  DWORD pid{};
  peer(uint32_t ip, uint16_t port) {
    static int sequence = 0;
    const auto prefix = std::format(L"Local\\BOIII.Hotkeys.Test.{}.{}", GetCurrentProcessId(), ++sequence);
    ready = CreateEventW(nullptr, TRUE, FALSE, (prefix + L".ready").c_str());
    stop = CreateEventW(nullptr, TRUE, FALSE, (prefix + L".stop").c_str());
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    auto command = std::format(L"\"{}\" --peer {} {} {}", executable, ip, port, prefix);
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info))
      throw std::runtime_error("Could not start the peer publisher");
    CloseHandle(info.hThread); process = info.hProcess; pid = info.dwProcessId;
    if (WaitForSingleObject(ready, 5000) != WAIT_OBJECT_0)
      throw std::runtime_error("Peer did not publish its local session");
  }
  ~peer() { finish(); CloseHandle(ready); CloseHandle(stop); }
  void finish() {
    if (process) { SetEvent(stop); WaitForSingleObject(process, 5000); CloseHandle(process); process = nullptr; }
  }
  peer(const peer &) = delete;
};
void release_keys() { fixture::keys.fill(false); host_hotkeys::poll_keys(); }
void press(int key) { fixture::keys[key] = true; host_hotkeys::poll_keys(); }
void cooldown() { std::this_thread::sleep_for(1050ms); }

int main(int argc, char **argv) {
  try {
    if (argc == 5 && std::string_view(argv[1]) == "--peer") {
      fixture::server = false;
      party::server = {static_cast<uint32_t>(std::stoul(argv[2])), static_cast<uint16_t>(std::stoul(argv[3]))};
      host_hotkeys::publish_session();
      const std::string prefix = argv[4];
      const HANDLE ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, (prefix + ".ready").c_str());
      const HANDLE stop = OpenEventA(SYNCHRONIZE, FALSE, (prefix + ".stop").c_str());
      SetEvent(ready); WaitForSingleObject(stop, 20000);
      host_hotkeys::close_session_marker(); CloseHandle(ready); CloseHandle(stop); return 0;
    }
    const auto own_pid = GetCurrentProcessId();
    peer local(htonl(0x7f000002), party::port);
    fixture::foreground_pid = local.pid;
    check(host_hotkeys::focused_instance_is_in_our_match(fixture::foreground), "other process in this local match is recognized, including 127/8 and non-default ports");
    release_keys(); press(VK_F3);
    check(fixture::commands == std::vector<std::string>{"fast_restart\n"}, "F3 from a focused client executes once on the background host");
    cooldown(); host_hotkeys::poll_keys(); press(VK_F4);
    check(fixture::commands.size() == 1, "held keys and overlapping F3/F4 do not repeat the restart");
    release_keys(); fixture::keys[VK_F3] = fixture::keys[VK_F4] = true; host_hotkeys::poll_keys();
    check(fixture::commands.size() == 2 && fixture::commands.back() == "map_restart\n", "simultaneous F3/F4 prefers exactly one map restart");
    release_keys(); press(VK_F3);
    check(fixture::commands.size() == 2, "rapid repeated presses are consumed rather than queued");
    cooldown(); release_keys(); fixture::keys[VK_MENU] = true; press(VK_F4);
    fixture::keys[VK_MENU] = false; host_hotkeys::poll_keys();
    check(fixture::commands.size() == 2, "Alt+F4 does not restart; releasing Alt while F4 is held does not restart either");
    release_keys(); fixture::foreground_pid = own_pid; fixture::foreground = reinterpret_cast<HWND>(2); press(VK_F3);
    check(fixture::commands.size() == 2, "changing focused windows while holding a restart key does not trigger it");
    release_keys(); press(VK_F4);
    check(fixture::commands.size() == 3 && fixture::commands.back() == "map_restart\n", "a standalone or focused host can still restart normally");
    cooldown(); release_keys(); fixture::ui = true; fixture::map = "core_frontend"; press(VK_F3);
    fixture::ui = false; fixture::map = "zm_test"; host_hotkeys::poll_keys();
    check(fixture::commands.size() == 3, "menu keys are ignored and not replayed on entering a match");
    release_keys(); game::cg::state.actives[0].connectionState = game::connstate_t::CONNECTING; press(VK_F3);
    game::cg::state.actives[0].connectionState = game::connstate_t::ACTIVE; host_hotkeys::poll_keys();
    check(fixture::commands.size() == 3, "loading/joining instances cannot restart or replay held keys after loading");
    release_keys(); fixture::server = false; press(VK_F4);
    check(fixture::commands.size() == 3, "a client with a remote host cannot execute a restart");
    fixture::server = true;
    {
      peer remote(htonl(0xcb007109), party::port);
      fixture::foreground_pid = remote.pid;
      check(!host_hotkeys::focused_instance_is_in_our_match(fixture::foreground), "remote match on the same port cannot control an unrelated local host");
      release_keys(); press(VK_F3);
      check(fixture::commands.size() == 3, "F3 on a remote-match client leaves the independent local host untouched");
    }
    {
      peer other_match(htonl(INADDR_LOOPBACK), party::port + 1);
      fixture::foreground_pid = other_match.pid;
      check(!host_hotkeys::focused_instance_is_in_our_match(fixture::foreground), "a separate local match on another port stays isolated");
    }
    ULONG size = 0; GetIpAddrTable(nullptr, &size, FALSE);
    std::vector<uint8_t> buffer(size);
    auto *addresses = reinterpret_cast<MIB_IPADDRTABLE *>(buffer.data());
    if (GetIpAddrTable(addresses, &size, FALSE) == NO_ERROR) {
      for (DWORD i = 0; i < addresses->dwNumEntries; ++i) {
        const auto ip = addresses->table[i].dwAddr;
        if (!ip || (ntohl(ip) >> 24) == 127) continue;
        peer lan(ip, party::port); fixture::foreground_pid = lan.pid;
        check(host_hotkeys::focused_instance_is_in_our_match(fixture::foreground), "same-PC connection via an actual LAN/VPN adapter is recognized");
        break;
      }
    }
    fixture::foreground_pid = 0x7ffffffe;
    check(!host_hotkeys::focused_instance_is_in_our_match(fixture::foreground), "unregistered foreground applications do not control any match");
    fixture::foreground_pid = local.pid; local.finish();
    check(!host_hotkeys::focused_instance_is_in_our_match(fixture::foreground), "exiting a peer removes its session marker without stale routing");
    host_hotkeys::component component;
    fixture::disabled = true; component.post_unpack();
    check(fixture::schedules == 0, "-nohosthotkeys disables the component");
    component.pre_destroy();
    check(fixture::toasts == static_cast<int>(fixture::commands.size()), "only the executing host displays restart notifications");
    std::cout << "All host hotkey regression checks passed. Game state/input were simulated; IPC used real Windows processes." << std::endl;
    return 0;
  } catch (const std::exception &error) { std::cerr << "FAIL: " << error.what() << std::endl; return 1; }
}
