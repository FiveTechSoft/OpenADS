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

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#else
#include <unistd.h>
#endif

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

// Key canonical form: lowercase, and '-' mapped to '_' so the serverd
// dash style (`http-port`, `legacy-paths`) and the underscore style
// (`remote_only_access`) name the same key in a shared openads.ini.
std::string norm_key(std::string s) {
    s = lower(std::move(s));
    std::replace(s.begin(), s.end(), '-', '_');
    return s;
}

// Directory of the host executable (the app loading the DLL) -- the
// natural place for a client-side openads.ini when ace64.dll itself
// lives on a shared path.
std::string exe_directory() {
#if defined(_WIN32)
    char buf[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return fs::path(std::string(buf, n)).parent_path().string();
#elif defined(__APPLE__)
    char buf[PATH_MAX] = {0};
    std::uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) != 0) return {};
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::path(buf), ec);
    if (ec) p = fs::path(buf);
    return p.parent_path().string();
#else
    std::error_code ec;
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if (ec) return {};
    return p.parent_path().string();
#endif
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
        std::string key = norm_key(trim(t.substr(0, eq)));
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
    const std::string exe_dir = exe_directory();
    if (!exe_dir.empty()) {
        std::error_code ec;
        const std::string cand = (fs::path(exe_dir) / "openads.ini").string();
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
    const auto it = c.values.find(norm_key(ini_key));
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
