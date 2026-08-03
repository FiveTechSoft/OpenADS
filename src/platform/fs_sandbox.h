#pragma once

#include <optional>
#include <string>
#include <vector>

namespace openads::platform {

// Resolve a client-supplied path under a data root for server filesystem
// ops (oads_*/AdsF*). Absolute / drive-rooted paths are folded: root is
// stripped and the relative remainder is joined under `root` (same idea
// as Connection::resolve_table_file for create). Returns nullopt on
// empty input or jail escape (.. outside root).
std::optional<std::string> resolve_fs_path(const std::string& root,
                                           const std::string& client_path);

std::optional<std::string> resolve_fs_path(
    const std::vector<std::string>& roots, const std::string& client_path);

// Basename wildcard: * and ? (case-sensitive).
bool match_wildcard(const std::string& name, const std::string& pattern);

// Fold an absolute path to a relative remainder (drop root_name +
// root_directory). Relative paths returned unchanged.
std::string fold_absolute_to_relative(const std::string& client_path);

// Host-independent absolute-path detection: true for drive-letter paths
// ("C:\x", "e:/x") and root-slash paths ("/x", "\\share\x") no matter
// which OS the server runs on. std::filesystem misses drive-letter
// paths on POSIX (#133).
bool is_client_absolute(const std::string& client_path);

// Legacy ERP path resolution (openads_serverd --legacy-paths). Resolves
// a client-supplied path of any file type under one of the data roots:
//   1. Relative paths join under a root (unchanged classic behavior).
//   2. Client-absolute paths whose remainder starts with a root's own
//      remainder — compared case-insensitively and ignoring the drive
//      letter — are prefix-stripped and re-joined under that root, so
//      "C:/TEMP/Sub/t.dbf" with root "c:\temp" maps to "<root>/Sub/t.dbf"
//      (original casing of the remainder is preserved).
//   3. Otherwise the drive/root is dropped and the remainder joined
//      (fold), so "E:\CLIENT\F.DBF" with root "/srv/data" maps to
//      "/srv/data/CLIENT/F.DBF".
// An empty or drive-root-only path ("E:/") resolves to the first root
// itself. Returns nullopt only on jail escape (".." outside the root).
std::optional<std::string> resolve_client_path(
    const std::vector<std::string>& roots, const std::string& client_path);

} // namespace openads::platform
