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
    // OpenADS Plus — external SQL backend (sqlite:// …).
    SqliteConnection = 9,
    SqliteTable      = 10,
    SqliteIndex      = 11,
    // OpenADS Plus — PostgreSQL backend (postgresql:// …).
    PostgresConnection = 12,
    PostgresTable      = 13,
    PostgresIndex      = 14,
    // OpenADS Plus — MariaDB/MySQL backend (mariadb:// …).
    MariaConnection = 15,
    MariaTable      = 16,
    MariaIndex      = 17,
    // OpenADS Plus — generic ODBC backend (odbc:// …).
    OdbcConnection = 18,
    OdbcTable      = 19,
    OdbcIndex      = 20,
    // OpenADS Plus — native Firebird backend (firebird:// / fb:// …).
    FirebirdConnection = 21,
    FirebirdTable      = 22,
    FirebirdIndex      = 23,
    // Native MS SQL Server backend (mssql:// / tds://).
    MssqlConnection = 24,
    MssqlTable      = 25
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

    HandleKind kind_of(Handle h) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = slots_.find(h);
        return it == slots_.end() ? HandleKind::None : it->second.kind;
    }

private:
    struct Slot { HandleKind kind = HandleKind::None; void* ptr = nullptr; };

    mutable std::mutex                  mu_;
    std::unordered_map<Handle, Slot>    slots_;
    Handle                              next_ = 1;
};

} // namespace openads::session
