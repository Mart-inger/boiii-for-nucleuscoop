# Local restart hotkeys

F3 runs `fast_restart`; F4 runs `map_restart`. The focused BOIII instance
can be the host or another local client in the same match. Only the instance
hosting that match executes the command, on the game's main thread.

| Situation | Result |
| --- | --- |
| Host window focused | Restarts its own match. |
| Another same-PC client in that match focused | The background host restarts the match. |
| Same-PC client joined through the PC's LAN/VPN IP | Works as well as a loopback connection. |
| Native split-screen guests | One command executes for the hosting instance. |
| Match hosted on another PC | No restart command is sent to that PC. Its host must press the shortcut locally. |
| Independent matches on the same PC | Only the focused instance's match is eligible. IP and port must both match. |
| Menus, loading, disconnected instance, or another application focused | No restart. Held keys are not replayed when focus/loading changes. |
| Dedicated server | These client hotkeys do not restart it; use its server console. |

Both the host and the focused local client must use this version. Relaunch all
instances after replacing the handler's `boiii.exe`. No runtime-data reset or
binds changes are needed for this feature. Keep the legacy F3/F4 AutoIt scripts
disabled so they do not submit additional console commands.

Shortcuts require an unmodified F3/F4 press. Alt+F4 and shortcuts involving
Ctrl, Shift or the Windows key are ignored. Holding a key does not repeat.
Both keys must be released before another request; pressing both together
chooses one `map_restart`. A one-second cooldown drops rapid extra requests.

`-nohosthotkeys` disables both participation and execution in that instance.

Each active instance publishes its match's IP and port in a named Windows
mutex under the session-local `Local\` namespace. The host checks whether the
foreground process has a matching marker when a shortcut is pressed. Markers
are removed on a session change or process exit. There are no network restart
messages, extra sockets, instance-number assumptions or window-title lookups.

## Validation

Run `powershell -ExecutionPolicy Bypass -File scripts/tests/test-host-hotkeys.ps1`
from the repository. It compiles the component implementation with simulated
game state and keyboard input, then checks routing with separate Windows
processes and real local mutexes. It covers host/client authority, remote hosts
sharing the same port, independent local matches, LAN addresses, focus changes,
held keys, modifiers, loading, cooldowns and cleanup. A live game test is still
needed to verify behavior with the installed Nucleus input hooks.
