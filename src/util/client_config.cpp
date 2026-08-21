#include "util/client_config.h"

#include "platform/path.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace openads::util {

namespace {

namespace fs = std::filesystem;

std::string trim(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(),
            std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(),
            s.end());
    return s;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

// Minimal openads.ini reader: `key = value` lines, `#`/`;` comments,
// `[section]` headers skipped (keys are matched anywhere -- the client
// keys live at the top level of the server file or alone in a
// client-side copy). Keys are case-insensitive; first occurrence wins.
std::unordered_map<std::string, std::string> parse_ini(
        const std::string& path) {
    std::unordered_map<std::string, std::string> out;
    std::ifstream in(path);
    if (!in) return out;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';' || t[0] == '[')
            continue;
        const auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = lower(trim(t.substr(0, eq)));
        if (key.empty()) continue;
        out.emplace(std::move(key), trim(t.substr(eq + 1)));
    }
    return out;
}

// First existing candidate wins; "" when no openads.ini is found.
std::string locate_ini() {
    if (const char* p = std::getenv("OPENADS_INI")) {
        if (p[0] != '\0') return p;  // explicit override: used even if absent
    }
    if (auto dir = openads::platform::module_directory()) {
        std::error_code ec;
        const std::string cand = (fs::path(*dir) / "openads.ini").string();
        if (fs::exists(cand, ec)) return cand;
    }
    std::error_code ec;
    if (fs::exists("openads.ini", ec)) return "openads.ini";
    return {};
}

struct IniCache {
    std::mutex                                    mu;
    std::string                                   path;
    fs::file_time_type                            mtime{};
    std::unordered_map<std::string, std::string>  values;
};

IniCache& cache() {
    static IniCache c;
    return c;
}

std::string ini_value(const char* ini_key) {
    if (ini_key == nullptr || ini_key[0] == '\0') return {};
    const std::string path = locate_ini();
    if (path.empty()) return {};
    std::error_code ec;
    const auto mtime = fs::last_write_time(path, ec);
    if (ec) return {};
    auto& c = cache();
    std::lock_guard<std::mutex> lk(c.mu);
    if (c.path != path || c.mtime != mtime) {
        c.values = parse_ini(path);
        c.path   = path;
        c.mtime  = mtime;
    }
    const auto it = c.values.find(lower(ini_key));
    return it == c.values.end() ? std::string() : it->second;
}

} // namespace

std::string client_setting(const char* env_name, const char* ini_key) {
    if (env_name != nullptr) {
        if (const char* e = std::getenv(env_name)) return e;
    }
    return ini_value(ini_key);
}

bool client_setting_truthy(const char* env_name, const char* ini_key) {
    const std::string v = lower(client_setting(env_name, ini_key));
    return !v.empty() && v != "0" && v != "false" && v != "off" &&
           v != "no";
}

} // namespace openads::util
