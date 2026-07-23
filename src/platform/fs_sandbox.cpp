#include "platform/fs_sandbox.h"
#include "platform/path.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace openads::platform {

std::string fold_absolute_to_relative(const std::string& client_path) {
    // Must be host-independent (#133): a Windows client can send
    // "C:\dir\f" to a POSIX server, where std::filesystem does NOT
    // recognize the drive letter — is_absolute()/has_root_name() are
    // both false and the old code returned the path verbatim, leaking
    // "C:" past the jail. Normalize separators and strip a drive-letter
    // prefix + leading root slashes ourselves before handing the
    // remainder to std::filesystem.
    std::string s = client_path;
    for (char& ch : s) if (ch == '\\') ch = '/';
    if (s.size() >= 2 &&
        ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) &&
        s[1] == ':') {
        s.erase(0, 2);                 // drop "C:"
    }
    std::size_t b = s.find_first_not_of('/');
    if (b == std::string::npos) {
        // Path was only a drive/root with no leaf — nothing to fold.
        return fs::path(client_path).filename().string();
    }
    s.erase(0, b);                     // drop leading root slashes
    fs::path p(s);
    if (p.is_absolute() || p.has_root_name()) p = p.relative_path();
    if (p.empty() || p == ".") return p.filename().string();
    return p.generic_string();
}

std::optional<std::string> resolve_fs_path(const std::string& root,
                                           const std::string& client_path) {
    if (client_path.empty()) return std::nullopt;
    const std::string folded = fold_absolute_to_relative(client_path);
    if (folded.empty()) return std::nullopt;
    return resolve_under_root(root, folded);
}

std::optional<std::string> resolve_fs_path(
    const std::vector<std::string>& roots, const std::string& client_path) {
    if (client_path.empty() || roots.empty()) return std::nullopt;
    const std::string folded = fold_absolute_to_relative(client_path);
    if (folded.empty()) return std::nullopt;
    return resolve_under_any_root(roots, folded);
}

namespace {

bool match_rec(const char* n, const char* p) {
    for (;; ++n, ++p) {
        if (*p == '\0') return *n == '\0';
        if (*p == '*') {
            while (p[1] == '*') ++p;
            if (p[1] == '\0') return true;
            for (const char* t = n; *t; ++t) {
                if (match_rec(t, p + 1)) return true;
            }
            return match_rec(n, p + 1);
        }
        if (*p == '?') {
            if (*n == '\0') return false;
            continue;
        }
        if (*n != *p) return false;
        if (*n == '\0') return true;
    }
}

} // namespace

bool match_wildcard(const std::string& name, const std::string& pattern) {
    if (pattern.empty()) return name.empty();
    return match_rec(name.c_str(), pattern.c_str());
}

} // namespace openads::platform
