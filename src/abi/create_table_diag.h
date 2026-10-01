#pragma once

// Opt-in CreateTable stages, shared by the client ABI and server ABI twin.
// Never log schema, row values, usernames, or raw paths.
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>

namespace openads::abi::create_diag {
inline thread_local std::string target;
inline thread_local std::string correlation;

inline std::string leaf(std::string_view path) {
    auto end = path.find_last_not_of("/\\");
    if (end == std::string_view::npos) return {};
    auto start = path.find_last_of("/\\", end);
    std::string out(path.substr(start == std::string_view::npos ? 0 : start + 1,
                                end - (start == std::string_view::npos ? 0 : start + 1) + 1));
    for (auto& ch : out) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.'))
            ch = '_';
    }
    return out;
}
inline bool enabled(std::string_view path) {
    const char* dest = std::getenv("OPENADS_CREATE_TABLE_DIAG_FILE");
    if (!dest || !*dest) return false;
    const char* filter = std::getenv("OPENADS_CREATE_TABLE_DIAG_FILTER");
    if (!filter || !*filter) return true;
    auto a = leaf(path), b = leaf(filter);
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}
inline std::string new_id() {
    static std::mutex mu;
    static unsigned long long serial = 0;
    std::lock_guard<std::mutex> lock(mu);
    return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
           "." + std::to_string(++serial);
}
struct Scope {
    std::string old_target, old_correlation;
    Scope(std::string_view name, std::string id) : old_target(target), old_correlation(correlation) {
        if (enabled(name)) {
            target = leaf(name);
            correlation = id.empty() ? new_id() : std::move(id);
        } else {
            target.clear();
            correlation.clear();
        }
    }
    ~Scope() {
        target = std::move(old_target);
        correlation = std::move(old_correlation);
    }
};
inline void log(std::string_view stage, std::int64_t code = 0,
                std::string_view detail = {}, int os_error = 0) {
    if (target.empty()) return;
    const char* dest = std::getenv("OPENADS_CREATE_TABLE_DIAG_FILE");
    if (!dest || !*dest) return;
    static std::mutex mu;
    std::lock_guard<std::mutex> lock(mu);
    auto* fp = std::fopen(dest, "a");
    if (!fp) return;
    // Bound each process log even when the filter is omitted for a fresh-org run.
    if (std::fseek(fp, 0, SEEK_END) != 0 ||
        std::ftell(fp) >= 2 * 1024 * 1024) {
        std::fclose(fp);
        return;
    }
    // Detail is restricted to a stage label, never arbitrary error text or paths.
    const auto epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::fprintf(fp, "create-table epoch_ms=%lld id=%s table=%s stage=%.*s code=%lld os_code=%d detail=%.*s\n",
                 static_cast<long long>(epoch_ms),
                 correlation.c_str(), target.c_str(), static_cast<int>(stage.size()), stage.data(),
                 static_cast<long long>(code), os_error,
                 static_cast<int>(detail.size()), detail.data());
    std::fclose(fp);
}
} // namespace openads::abi::create_diag
