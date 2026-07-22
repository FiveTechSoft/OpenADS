// openads_serverd — minimal INI config parser.
//
// Lets operators keep the daemon's settings in an `openads.ini` file
// instead of a long command line baked into a service binPath. This is
// the shape ex-Advantage users expect (ADS shipped `ads.cfg`): a setup
// step writes the file, and the service just points at it with
// `--config <path>`.
//
// Deliberately dependency-free (no toml/json/yaml): a handful of
// `key = value` lines, `#`/`;` comments, and an optional `[server]`
// section header that is accepted and ignored. Keys map 1:1 to the
// daemon's command-line options so there is exactly one mental model.

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace openads::serverd {

// Values read from an openads.ini. Each scalar carries a `has_*` flag so
// the caller can apply only the keys that were actually present — the file
// overrides the built-in defaults, and the command line in turn overrides
// the file. `http_users` is additive (every `http_user` line appends).
struct IniConfig {
    bool          has_host      = false;
    std::string   host;
    bool          has_port      = false;
    std::uint16_t port          = 0;
    bool          has_backlog   = false;
    int           backlog       = 0;
    bool          has_http_port = false;
    std::uint16_t http_port     = 0;
    bool          has_data      = false;
    std::string   data_dir;
    // Enable remote client filesystem ops under data= (oads_*/AdsF*).
    // Keys: EnableFileFunc / enable_file_func (0/1/true/false).
    bool          has_enable_file_func = false;
    bool          enable_file_func     = false;
    // SAP-style error log settings (ads_err.dbf): directory and max size
    // in kilobytes. Accepted keys: error_log_path (alias:
    // error_assert_logs, SAP's registry/ini name) and error_log_max.
    bool          has_error_log_path = false;
    std::string   error_log_path;
    bool          has_error_log_max  = false;
    std::uint32_t error_log_max_kb   = 0;
    std::vector<std::pair<std::string, std::string>> http_users;
    std::vector<std::pair<std::string, std::string>> auth_users;
};

// Parse INI *text* (already loaded into memory). Returns true on success.
// On failure returns false and sets `error` to a one-line, line-numbered
// message (e.g. "line 4: unknown key 'foo'"). Recognised keys:
//   host, port, backlog, http_port, data (alias: data_dir; may list
//   several roots separated by ';', e.g. "C:\data;D:\more-data" — see
//   Server::set_data_dir / platform::split_data_roots),
//   http_user (value is user:password, repeatable),
//   auth_user (value is user:password, repeatable; required by TCP clients).
bool parse_ini(const std::string& text, IniConfig& out, std::string& error);

// Convenience wrapper: read the file at `path` then parse_ini() it.
// Returns false (with `error` set) if the file cannot be opened.
bool load_ini_file(const std::string& path, IniConfig& out,
                   std::string& error);

}  // namespace openads::serverd
