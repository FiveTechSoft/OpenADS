#pragma once

#include <string>

namespace openads::util {

// Client-side setting lookup shared by the ACE DLL and the tools.
//
// Order:
//   1. environment variable `env_name` (exact, e.g. "OPENADS_REMOTE_ONLY_ACCESS")
//   2. key `ini_key` (e.g. "remote_only_access") in openads.ini
// The environment wins. openads.ini is located via, in order:
//   $OPENADS_INI (full path to the file), <dll dir>/openads.ini,
//   <app exe dir>/openads.ini, ./openads.ini (host process cwd).
// Keys are case-insensitive and '-' is treated as '_' (so the serverd
// dash style and the underscore style name the same key).
// The file is re-read only when its path or mtime changes, so lookups
// stay cheap on hot paths. Returns "" when the setting is not present
// anywhere (an env var set to an empty string also yields "", matching
// getenv semantics closely enough for flags).
std::string client_setting(const char* env_name, const char* ini_key);

// Truthy interpretation of client_setting(): "", "0", "false", "off"
// and "no" (any case) are false; everything else is true.
bool client_setting_truthy(const char* env_name, const char* ini_key);

} // namespace openads::util
