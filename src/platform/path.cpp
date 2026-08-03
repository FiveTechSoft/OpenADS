#include "platform/path.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

namespace openads::platform {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return s;
}

} // namespace

std::string resolve_case_insensitive(const std::string& path) {
    fs::path p(path);
    fs::path parent = p.parent_path();
    if (parent.empty()) parent = ".";

    std::error_code ec;
    if (!fs::is_directory(parent, ec)) {
        // Parent directory does not exist: nothing to resolve.
        return path;
    }

    const std::string leaf      = p.filename().string();
    const std::string leaf_low  = to_lower(leaf);

    // First pass: prefer an exact-case match if one exists. This keeps
    // the input verbatim when the case is already correct.
    for (const auto& entry : fs::directory_iterator(parent, ec)) {
        if (ec) break;
        if (entry.path().filename().string() == leaf) {
            return entry.path().string();
        }
    }

    // Second pass: case-insensitive match returns the on-disk casing.
    for (const auto& entry : fs::directory_iterator(parent, ec)) {
        if (ec) break;
        if (to_lower(entry.path().filename().string()) == leaf_low) {
            return entry.path().string();
        }
    }

    return path;
}

namespace {

bool path_has_prefix(const std::string& path, const std::string& root) {
    if (root.empty()) return true;
    if (path.size() < root.size()) return false;
    if (path.compare(0, root.size(), root) != 0) return false;
    if (path.size() == root.size()) return true;
    const char next = path[root.size()];
    return next == '/' || next == '\\';
}

} // namespace

std::optional<std::string> resolve_under_root(const std::string& root,
                                              const std::string& client_path) {
    namespace fs = std::filesystem;
    if (client_path.empty()) return std::nullopt;

    std::error_code ec;
    fs::path root_p = root.empty()
        ? fs::current_path(ec)
        : fs::absolute(fs::path(root), ec);
    if (ec) return std::nullopt;

    fs::path client_p(client_path);
    fs::path combined = client_p.is_absolute() ? client_p : (root_p / client_p);

    fs::path canon = fs::weakly_canonical(combined, ec);
    if (ec) return std::nullopt;

    fs::path root_canon = fs::weakly_canonical(root_p, ec);
    if (ec) root_canon = root_p.lexically_normal();

    const std::string canon_s = canon.generic_string();
    std::string root_s  = root_canon.generic_string();
    // A drive-root ("C:\") canonicalizes with a trailing slash ("C:/"),
    // which breaks the boundary check below for everything under it —
    // "--data E:\" rejected every path. Strip it ("C:"), so the next
    // char of a jailed child is the expected '/'. A POSIX "/" root
    // strips to empty, which path_has_prefix treats as accept-all —
    // exactly what "--data /" means.
    while (!root_s.empty() && root_s.back() == '/') root_s.pop_back();
    if (!path_has_prefix(canon_s, root_s)) return std::nullopt;
    return canon_s;
}

std::vector<std::string> split_data_roots(const std::string& roots) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= roots.size()) {
        std::size_t sep = roots.find(';', start);
        std::string piece = roots.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
        // Trim surrounding whitespace so "a ; b" behaves the same as "a;b".
        std::size_t first = piece.find_first_not_of(" \t\r\n");
        if (first != std::string::npos) {
            std::size_t last = piece.find_last_not_of(" \t\r\n");
            out.push_back(piece.substr(first, last - first + 1));
        }
        if (sep == std::string::npos) break;
        start = sep + 1;
    }
    return out;
}

std::optional<std::string> resolve_under_any_root(
    const std::vector<std::string>& roots, const std::string& client_path) {
    for (const auto& root : roots) {
        if (auto resolved = resolve_under_root(root, client_path)) {
            return resolved;
        }
    }
    return std::nullopt;
}

// RCB 2026-07-10 — see path.h. The address of this function itself is
// used as the anchor, so the answer is the module that actually holds
// the engine code (the DLL under openace64.dll, the exe when linked
// statically) rather than the host process's exe.
std::optional<std::string> module_directory() {
#if defined(_WIN32)
    HMODULE mod = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&module_directory), &mod) ||
        mod == nullptr) {
        return std::nullopt;
    }
    char buf[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameA(mod, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::nullopt;
    return fs::path(std::string(buf, n)).parent_path().string();
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&module_directory), &info) == 0 ||
        info.dli_fname == nullptr || info.dli_fname[0] == '\0') {
        return std::nullopt;
    }
    std::error_code ec;
    fs::path p = fs::absolute(fs::path(info.dli_fname), ec);
    if (ec) p = fs::path(info.dli_fname);
    return p.parent_path().string();
#endif
}

} // namespace openads::platform
