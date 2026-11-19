# Ezz BOIII for Nucleus Co-op

Source code of Ezz BOIII adapted for Nucleus Co-op.

## Changes

- Separate player identities for each Nucleus instance.
- Localhost networking and discovery of instances on the same PC or LAN.
- Built-in CONNECT menu with live host and player information.
- Improved joining, reconnection handling, and synchronization of host settings.
- F3 (`fast_restart`) and F4 (`map_restart`) work from another local instance in the same match. Only the host executes the command; remote PCs cannot trigger it.
- Restart fixes that preserve player limits and reduce clientfield mismatches.
- Improved Workshop, custom map, and mod detection in Nucleus instance folders.
- Mod loading compatibility fixes and timeout handling.
- Filtering of redundant reliable HUD commands in player-hosted matches.
- Title-screen skipping, menu/toast fixes, and per-instance console logs.
