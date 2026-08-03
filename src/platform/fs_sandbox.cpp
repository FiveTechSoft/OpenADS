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

// Separator-normalize, strip a drive-letter prefix and leading root
// slashes; the original casing is preserved (comparison happens on a
// lowercased copy at the call site). With strip_trailing, trailing
// slashes go too — used for roots so "C:/temp/" compares as "temp".
std::string norm_remainder(const std::string& p, bool strip_trailing) {
    std::string s = p;
    for (char& ch : s) if (ch == '\\') ch = '/';
    if (s.size() >= 2 &&
        ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) &&
        s[1] == ':') {
        s.erase(0, 2);                 // drop "C:"
    }
    const std::size_t b = s.find_first_not_of('/');
    s = (b == std::string::npos) ? std::string() : s.substr(b);
    if (strip_trailing) {
        while (!s.empty() && s.back() == '/') s.pop_back();
    }
    return s;
}

std::string lower_copy(std::string s) {
    for (char& ch : s) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return s;
}

} // namespace

bool is_client_absolute(const std::string& p) {
    if (p.size() >= 2 &&
        ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) &&
        p[1] == ':') {
        return true;                   // drive-letter path, any host OS
    }
    return !p.empty() && (p[0] == '/' || p[0] == '\\');
}

std::optional<std::string> resolve_client_path(
    const std::vector<std::string>& roots, const std::string& client_path) {
    if (roots.empty()) return std::nullopt;
    if (client_path.empty()) return resolve_under_root(roots.front(), ".");

    // Relative paths join under a root exactly as before — prefix
    // stripping must not rewrite a relative name that merely starts
    // with the root's folder name.
    if (!is_client_absolute(client_path)) {
        return resolve_under_any_root(roots, client_path);
    }

    const std::string pc = norm_remainder(client_path, false);
    if (pc.empty()) {
        // Drive root only ("E:/"): the data root itself.
        return resolve_under_root(roots.front(), ".");
    }
    const std::string want = lower_copy(pc);

    for (const auto& root : roots) {
        const std::string r = lower_copy(norm_remainder(root, true));
        if (r.empty()) continue;   // drive-root root: the fold below covers it
        if (want == r) return resolve_under_root(root, ".");
        if (want.size() > r.size() && want.compare(0, r.size(), r) == 0 &&
            want[r.size()] == '/') {
            if (auto res = resolve_under_root(root, pc.substr(r.size() + 1))) {
                return res;
            }
        }
    }
    // No root prefix matched: drop the drive/root and join the remainder.
    return resolve_under_any_root(roots, pc);
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
