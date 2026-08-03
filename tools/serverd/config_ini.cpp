// openads_serverd — minimal INI config parser (see config_ini.h).

#include "tools/serverd/config_ini.h"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

namespace openads::serverd {

namespace {

// Trim ASCII whitespace from both ends.
std::string trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(
        std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Parse an unsigned integer in [0, max]. Rejects empty strings, non-digit
// characters and out-of-range values so a typo (`port = 99999`) is a clear
// error rather than a silent wraparound.
bool parse_uint(const std::string& v, unsigned long max, unsigned long& out) {
    if (v.empty()) return false;
    unsigned long acc = 0;
    for (char c : v) {
        if (c < '0' || c > '9') return false;
        acc = acc * 10 + static_cast<unsigned long>(c - '0');
        if (acc > max) return false;
    }
    out = acc;
    return true;
}

}  // namespace

bool parse_ini(const std::string& text, IniConfig& out, std::string& error) {
    std::istringstream in(text);
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        // Tolerate CRLF input on POSIX builds.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = trim(line);
        if (t.empty()) continue;
        if (t[0] == '#' || t[0] == ';') continue;          // comment
        if (t.front() == '[' && t.back() == ']') continue;  // section header

        auto eq = t.find('=');
        if (eq == std::string::npos) {
            error = "line " + std::to_string(lineno) +
                    ": expected key = value";
            return false;
        }
        std::string key = to_lower(trim(t.substr(0, eq)));
        std::string val = trim(t.substr(eq + 1));

        if (key == "host") {
            out.host = val;
            out.has_host = true;
        } else if (key == "port") {
            unsigned long n = 0;
            if (!parse_uint(val, 65535, n)) {
                error = "line " + std::to_string(lineno) +
                        ": port must be 0..65535";
                return false;
            }
            out.port = static_cast<std::uint16_t>(n);
            out.has_port = true;
        } else if (key == "backlog") {
            unsigned long n = 0;
            if (!parse_uint(val, 65535, n)) {
                error = "line " + std::to_string(lineno) +
                        ": backlog must be a non-negative integer";
                return false;
            }
            out.backlog = static_cast<int>(n);
            out.has_backlog = true;
        } else if (key == "http_port" || key == "http-port") {
            unsigned long n = 0;
            if (!parse_uint(val, 65535, n)) {
                error = "line " + std::to_string(lineno) +
                        ": http_port must be 0..65535";
                return false;
            }
            out.http_port = static_cast<std::uint16_t>(n);
            out.has_http_port = true;
        } else if (key == "data" || key == "data_dir" || key == "datadir") {
            out.data_dir = val;
            out.has_data = true;
        } else if (key == "enablefilefunc" || key == "enable_file_func" ||
                   key == "enable-file-func") {
            out.has_enable_file_func = true;
            std::string v = val;
            for (char& c : v) {
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            }
            out.enable_file_func =
                (v == "1" || v == "true" || v == "yes" || v == "on");
        } else if (key == "legacypaths" || key == "legacy_paths" ||
                   key == "legacy-paths") {
            out.has_legacy_paths = true;
            std::string v = val;
            for (char& c : v) {
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            }
            out.legacy_paths =
                (v == "1" || v == "true" || v == "yes" || v == "on");
        } else if (key == "error_log_path" || key == "error-log-path" ||
                   key == "error_assert_logs") {
            out.error_log_path = val;
            out.has_error_log_path = true;
        } else if (key == "error_log_max" || key == "error-log-max") {
            unsigned long n = 0;
            if (!parse_uint(val, 0xFFFFFFFFul, n) || n == 0) {
                error = "line " + std::to_string(lineno) +
                        ": error_log_max must be a positive number of "
                        "kilobytes";
                return false;
            }
            out.error_log_max_kb = static_cast<std::uint32_t>(n);
            out.has_error_log_max = true;
        } else if (key == "http_user" || key == "http-user") {
            auto colon = val.find(':');
            if (colon == std::string::npos) {
                error = "line " + std::to_string(lineno) +
                        ": http_user must be user:password";
                return false;
            }
            out.http_users.emplace_back(val.substr(0, colon),
                                        val.substr(colon + 1));
        } else if (key == "auth_user" || key == "auth-user") {
            auto colon = val.find(':');
            if (colon == std::string::npos || colon == 0) {
                error = "line " + std::to_string(lineno) +
                        ": auth_user must be user:password";
                return false;
            }
            out.auth_users.emplace_back(val.substr(0, colon),
                                        val.substr(colon + 1));
        } else {
            error = "line " + std::to_string(lineno) +
                    ": unknown key '" + key + "'";
            return false;
        }
    }
    return true;
}

bool load_ini_file(const std::string& path, IniConfig& out,
                   std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "cannot open config file: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse_ini(ss.str(), out, error);
}

}  // namespace openads::serverd
