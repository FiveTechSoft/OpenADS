#pragma once

#include <optional>
#include <string>
#include <vector>

namespace openads::platform {

// On Windows the filesystem is already case-insensitive; on POSIX this
// scans the parent directory once to find a case-folded match. Returns
// the input unchanged if no match exists.
std::string resolve_case_insensitive(const std::string& path);

// Resolve `client_path` under `root` (relative paths are joined first),
// canonicalize, and return the result only when it stays inside `root`.
// Absolute client paths are accepted when they already lie under `root`.
// Returns nullopt when the path escapes the jail (e.g. `..` segments).
std::optional<std::string> resolve_under_root(const std::string& root,
                                              const std::string& client_path);

// Split a semicolon-separated list of data roots (the openads_serverd
// `--data` / `data=` value) into trimmed, non-empty entries. A single root
// with no semicolon yields a one-element vector, so callers can treat the
// single- and multi-root cases uniformly.
std::vector<std::string> split_data_roots(const std::string& roots);

// Like resolve_under_root, but accepts any one of several jail roots —
// openads_serverd can be configured with more than one `--data` directory
// (semicolon-separated) so a single server can serve DDs that live under
// different drives/shares. Tries each root in order and returns the first
// successful resolution. Returns nullopt if `client_path` escapes every
// root in the list (or the list is empty).
std::optional<std::string> resolve_under_any_root(
    const std::vector<std::string>& roots, const std::string& client_path);

// RCB 2026-07-10 — directory of the binary module that contains the
// OpenADS engine code: openace64.dll / openace32.dll when the engine is
// loaded as the ACE-compatible DLL, or the executable itself for
// statically linked binaries (openads_serverd, the unit-test runner).
// Used to locate adslocal.cfg SAP-style — the Advantage Local Server
// reads its configuration file from the directory of the local-server
// library (adsloc32.dll), so OpenADS looks next to its own DLL. Returns
// nullopt if the platform lookup fails.
std::optional<std::string> module_directory();

} // namespace openads::platform
