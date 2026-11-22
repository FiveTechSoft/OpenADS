#pragma once
// Diagnostic provenance only. Static API/reason labels, never caller arguments,
// source paths, return addresses or stack unwinding. Synchronous same-thread
// dispatch inherits this scope; unrelated/background threads report unknown.
#include <atomic>
#include <cstdint>
#include <cstdio>

namespace openads::abi::wire_caller {
class Entry;
struct State {
    Entry* top = nullptr;
    const char* reason = nullptr;
    std::uint64_t thread = 0;
};
inline State& state() { static thread_local State value; return value; }
inline std::uint64_t next_id() {
    static std::atomic<std::uint64_t> value{0};
    return value.fetch_add(1, std::memory_order_relaxed) + 1;
}
class Entry {
public:
    Entry(const char* api, bool enabled) noexcept : api_(api), enabled_(enabled) {
        if (!enabled_) return;
        auto& s = state();
        previous_ = s.top;
        previous_reason_ = s.reason;
        if (previous_) { root_ = previous_->root_; call_ = previous_->call_; }
        else {
            root_ = this;
            call_ = next_id();
            if (!s.thread) s.thread = next_id();
            s.reason = nullptr;
        }
        s.top = this;
    }
    ~Entry() {
        if (enabled_) { state().top = previous_; state().reason = previous_reason_; }
    }
    Entry(const Entry&) = delete;
    Entry& operator=(const Entry&) = delete;
    const char* api() const noexcept { return api_; }
    const char* root_api() const noexcept { return root_->api_; }
    std::uint64_t call() const noexcept { return call_; }
private:
    const char* api_;
    bool enabled_;
    Entry* previous_ = nullptr;
    Entry* root_ = nullptr;
    const char* previous_reason_ = nullptr;
    std::uint64_t call_ = 0;
};
class Reason {
public:
    explicit Reason(const char* reason) noexcept : active_(state().top != nullptr) {
        if (active_) { previous_ = state().reason; state().reason = reason; }
    }
    ~Reason() { if (active_) state().reason = previous_; }
    Reason(const Reason&) = delete;
    Reason& operator=(const Reason&) = delete;
private:
    bool active_;
    const char* previous_ = nullptr;
};
struct Snapshot {
    const char* root;
    const char* current;
    const char* reason;
    std::uint64_t call;
    std::uint64_t thread;
};
inline Snapshot snapshot() noexcept {
    const auto& s = state();
    if (!s.top) return {"unknown", "unknown", "unknown", 0, 0};
    return {s.top->root_api(), s.top->api(), s.reason ? s.reason : "request",
            s.top->call(), s.thread};
}
inline void label(char* out, std::size_t bytes) noexcept {
    const auto s = snapshot();
    if (!s.call) std::snprintf(out, bytes, "unknown");
    else std::snprintf(out, bytes, "%s/%s", s.root, s.reason);
}
} // namespace openads::abi::wire_caller
