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

} // namespace openads::platform
