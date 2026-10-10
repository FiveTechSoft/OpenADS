#pragma once

#include <cstdint>
#include <string>

namespace openads::sql_backend {

// Runtime limits for enterprise deployments (100+ concurrent users).
// Applies to ALL connection modes — not only ODBC.
//
// AdsConnect60 URI dispatch (first match wins):
//   tls:// / tcp://     -> native OpenADS wire client (RemoteConnection)
//   <local path>        -> native ADT/CDX/NTX engine (Connection)
//   sqlite://           -> native SQLite driver (SqliteConnection, not ODBC)
//   odbc://             -> optional ODBC bridge (OdbcConnection; compile-time)
//   oledb://            -> optional OLE DB bridge (Windows COM; WIP)
//
// ODBC is optional transport for SQL sources (MSSQL, Firebird, …). Apps that
// prefer native local tables or the OpenADS server keep using path / tcp URIs.
// Tunable via OPENADS_* environment variables; see enterprise_config.cpp.
struct EnterpriseConfig {
    bool pool_enabled = true;
    bool pool_odbc_enabled   = true;
    bool pool_sqlite_enabled = true;
    bool pool_oledb_enabled  = true;

    std::uint32_t odbc_pool_max_per_dsn   = 64;
    std::uint32_t sqlite_pool_max_per_db  = 32;
    std::uint32_t oledb_pool_max_per_dsn  = 64;
    std::uint32_t odbc_acquire_timeout_ms = 30'000;
    std::uint32_t odbc_idle_timeout_sec   = 300;
    std::uint32_t sqlite_idle_timeout_sec = 300;
    std::uint32_t oledb_idle_timeout_sec  = 300;

    std::uint32_t odbc_login_timeout_sec      = 15;
    std::uint32_t odbc_connection_timeout_sec   = 30;
    std::uint32_t sqlite_busy_timeout_ms        = 30'000;
    bool          sqlite_wal_mode                 = true;

    std::uint32_t server_max_sessions   = 500;
    std::string server_max_sessions_source = "default";
    std::uint32_t server_listen_backlog = 256;

    // Enterprise step 3 — wire-server connection pool (sharded reactor).
    // When enabled, the server multiplexes connections over a fixed pool of
    // worker threads instead of one thread per connection. Default OFF (the
    // thread-per-connection path is unchanged) until proven by stress.
    bool          server_pool_enabled = false;   // OPENADS_SERVER_POOL
    std::uint32_t server_pool_workers = 0;        // 0 = hardware_concurrency

    std::uint32_t lock_retry_count = 50;
    std::uint32_t lock_retry_cycle_ms = 100;

    static const EnterpriseConfig& instance();
};

inline const EnterpriseConfig& enterprise_config() {
    return EnterpriseConfig::instance();
}

// Pool toggles re-read env on each call (unit tests can flip without reload).
// OPENADS_POOL_ENABLED=0 disables all pooling. Per-backend overrides:
//   OPENADS_POOL_ODBC_ENABLED   (optional ODBC path only)
//   OPENADS_POOL_SQLITE_ENABLED (native sqlite:// path only)
//   OPENADS_POOL_OLEDB_ENABLED  (optional oledb:// path only; Windows)
bool enterprise_pool_enabled() noexcept;
bool enterprise_pool_odbc_enabled() noexcept;
bool enterprise_pool_sqlite_enabled() noexcept;
bool enterprise_pool_oledb_enabled() noexcept;

// Wire-server pool toggles — also re-read env per call so tests can flip
// them without rebuilding the cached singleton.
//   OPENADS_SERVER_POOL          (default 0/off)
//   OPENADS_SERVER_POOL_WORKERS  (default 0 = hardware_concurrency)
bool          enterprise_server_pool_enabled() noexcept;
std::uint32_t enterprise_server_pool_workers() noexcept;

} // namespace openads::sql_backend