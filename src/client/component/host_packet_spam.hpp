#pragma once

#include <game/game.hpp>

namespace host_packet_spam {
// True if `command` is a redundant `luinotify` for this client while hosting
// a match: sent to it less than a second ago, or the same as the last
// command sent to it. Called from patches.cpp's SV_AddServerCommand hook,
// which already filters repeated configstrings. Records the command when it
// is not skipped.
bool is_redundant_luinotify(const game::sv::client_s *client,
                            const char *command);
} // namespace host_packet_spam
