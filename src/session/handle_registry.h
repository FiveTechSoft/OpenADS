#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace openads::session {

enum class HandleKind {
    None             = 0,
    Connection       = 1,
    Table            = 2,
    Cursor           = 3,
    Statement        = 4,
    Find             = 5,
    // M12.5 — remote (TCP) variants. Same Ads* surface routes to
    // a wire client instead of a local engine::Connection.
    RemoteConnection = 6,
    RemoteTable      = 7,
    // M12.16 — remote (TCP) index handle. Wraps a server-side
    // index id; ABI calls (AdsSeek, AdsCloseIndex, …) route the
    // hIndex through the wire instead of a local IIndex.
    RemoteIndex      = 8,
    // Optional SQL backend (odbc:// …).
    OdbcConnection = 9,
    OdbcTable      = 10,
    OdbcIndex      = 11,
    // Native MS SQL Server backend (mssql:// / tds://).
    MssqlConnection = 12,
    MssqlTable      = 13
};

using Handle = std::uint64_t;

class HandleRegistry {
public:
    Handle register_object(HandleKind kind, void* ptr);
    void   release(Handle h);

    template <class T>
    T* lookup(Handle h, HandleKind kind) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = slots_.find(h);
        if (it == slots_.end()) return nullptr;
        if (it->second.kind != kind) return nullptr;
        return static_cast<T*>(it->second.ptr);
    }

    template <class F>
    void for_each_handle(F&& f) const {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& [h, slot] : slots_) {
            f(h, slot.kind, slot.ptr);
        }
    }

private:
    struct Slot { HandleKind kind = HandleKind::None; void* ptr = nullptr; };

    mutable std::mutex                  mu_;
    std::unordered_map<Handle, Slot>    slots_;
    Handle                              next_ = 1;
};

} // namespace openads::session
