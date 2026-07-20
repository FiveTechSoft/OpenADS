#include "platform/fs_sandbox.h"
#include "platform/path.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace openads::platform {

std::string fold_absolute_to_relative(const std::string& client_path) {
    fs::path p(client_path);
    if (!p.is_absolute() && !p.has_root_name()) return client_path;
    // Drop drive/root; keep relative path under it.
    fs::path rel = p.relative_path();
    if (rel.empty() || rel == ".") {
        // bare "C:\" style — no useful leaf
        return p.filename().string();
    }
    return rel.generic_string();
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
