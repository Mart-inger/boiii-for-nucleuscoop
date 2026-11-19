#pragma once

#include <filesystem>
namespace path {

std::filesystem::path resolve(std::filesystem::path path, std::error_code &ec);
inline std::filesystem::path resolve(std::filesystem::path path) {
  std::error_code ec{};
  return resolve(std::move(path), ec);
}

struct ParentMatch {
  std::filesystem::path parent;
  std::filesystem::path replacement;
};

const std::filesystem::path &cwd();
// E.g. "C:\\"
const std::filesystem::path &root();

const std::filesystem::path &players_directory();
const std::filesystem::path &boiii_players_directory();

// The game installation. Differs from cwd() when running from a folder of
// symlinks to it (Nucleus Co-op instances).
const std::filesystem::path &real_game_directory();

// Next to the current folder. Used to redirect workshop paths when it's
// missing, so it deliberately doesn't follow symlinks.
const std::filesystem::path &steam_workshop_content_directory();
// The workshop folder of the real installation, also from a folder of
// symlinks. For listing workshop items, which are loaded by absolute path.
const std::filesystem::path &installed_workshop_content_directory();
bool have_steam_workshop_content_directory();
const std::filesystem::path &usermaps_directory();
const std::filesystem::path &mods_directory();

const std::filesystem::path &lpc_directory();

inline std::filesystem::path part(const std::filesystem::path &p,
                                  size_t partIdx) {
  return partIdx == 0 ? *p.begin() : *std::next(p.begin(), partIdx);
}

std::filesystem::path strip_parts(const std::filesystem::path &p, size_t count);

inline size_t part_count(const std::filesystem::path &path) {
  return std::distance(path.begin(), path.end());
}

// Assumes both paths have already been canonicalized prior to call
inline bool has_parent(const std::filesystem::path &child,
                       const std::filesystem::path &base) {
  return base.native().size() < child.native().size() &&
         std::equal(base.begin(), base.end(), child.begin());
}

bool reparent(std::filesystem::path &path, const std::filesystem::path &current,
              const std::filesystem::path &replacement);

inline bool reparent(std::filesystem::path &path,
                     const ParentMatch &replacement) {
  return reparent(path, replacement.parent, replacement.replacement);
}

std::filesystem::path normalize(const std::filesystem::path &path);

} // namespace path
