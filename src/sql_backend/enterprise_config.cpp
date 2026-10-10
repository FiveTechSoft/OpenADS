#include "sql_backend/enterprise_config.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace openads::sql_backend {

namespace {

bool env_bool(const char* name, bool default_val) {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') return default_val;
    return (std::strcmp(v, "0") != 0 && std::strcmp(v, "false") != 0 &&
            std::strcmp(v, "FALSE") != 0 && std::strcmp(v, "no") != 0 &&
            std::strcmp(v, "NO") != 0);
}

std::uint32_t env_u32(const char* name, std::uint32_t default_val) {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') return default_val;
    errno = 0;
    char* end = nullptr;
    const long long n = std::strtoll(v, &end, 10);
    // Reject non-numeric, trailing junk, out-of-range, and negative values —
    // a malformed env var falls back to the safe default rather than 0 or a
    // truncated/overflowed cap.
    if (end == v || *end != '\0') return default_val;
    if (errno == ERANGE || n < 0 ||
        n > static_cast<long long>(UINT32_MAX)) {
        return default_val;
    }
    return static_cast<std::uint32_t>(n);
}

EnterpriseConfig load_from_env() {
    EnterpriseConfig c;
    c.pool_enabled = env_bool("OPENADS_POOL_ENABLED", true);
    c.pool_odbc_enabled = env_bool("OPENADS_POOL_ODBC_ENABLED", c.pool_enabled);
    c.pool_sqlite_enabled =
        env_bool("OPENADS_POOL_SQLITE_ENABLED", c.pool_enabled);
    c.pool_oledb_enabled =
        env_bool("OPENADS_POOL_OLEDB_ENABLED", c.pool_enabled);

    c.odbc_pool_max_per_dsn  = env_u32("OPENADS_POOL_ODBC_MAX", 64);
    c.sqlite_pool_max_per_db = env_u32("OPENADS_POOL_SQLITE_MAX", 32);
    c.oledb_pool_max_per_dsn = env_u32("OPENADS_POOL_OLEDB_MAX", 64);
    c.odbc_acquire_timeout_ms =
        env_u32("OPENADS_POOL_ACQUIRE_TIMEOUT_MS", 30'000);
    c.odbc_idle_timeout_sec  = env_u32("OPENADS_POOL_ODBC_IDLE_SEC", 300);
    c.sqlite_idle_timeout_sec = env_u32("OPENADS_POOL_SQLITE_IDLE_SEC", 300);
    c.oledb_idle_timeout_sec  = env_u32("OPENADS_POOL_OLEDB_IDLE_SEC", 300);

    c.odbc_login_timeout_sec =
        env_u32("OPENADS_ODBC_LOGIN_TIMEOUT_SEC", 15);
    c.odbc_connection_timeout_sec =
        env_u32("OPENADS_ODBC_CONN_TIMEOUT_SEC", 30);
    c.sqlite_busy_timeout_ms = env_u32("OPENADS_SQLITE_BUSY_MS", 30'000);
    c.sqlite_wal_mode = env_bool("OPENADS_SQLITE_WAL", true);

    c.server_max_sessions   = env_u32("OPENADS_SERVER_MAX_SESSIONS", 500);
    const char* raw_sessions = std::getenv("OPENADS_SERVER_MAX_SESSIONS");
    if (raw_sessions && *raw_sessions) {
        errno = 0;
        char* end = nullptr;
        const long long parsed = std::strtoll(raw_sessions, &end, 10);
        if (end != raw_sessions && *end == '\0' && errno != ERANGE &&
            parsed >= 0 && parsed <= static_cast<long long>(UINT32_MAX))
            c.server_max_sessions_source = "environment:OPENADS_SERVER_MAX_SESSIONS";
    }
    c.server_listen_backlog = env_u32("OPENADS_SERVER_BACKLOG", 256);

    c.server_pool_enabled = env_bool("OPENADS_SERVER_POOL", false);
    c.server_pool_workers = env_u32("OPENADS_SERVER_POOL_WORKERS", 0);

    c.lock_retry_count    = env_u32("OPENADS_LOCK_RETRY_COUNT", 50);
    c.lock_retry_cycle_ms = env_u32("OPENADS_LOCK_RETRY_MS", 100);
    return c;
}

} // namespace

const EnterpriseConfig& EnterpriseConfig::instance() {
    static const EnterpriseConfig cfg = load_from_env();
    return cfg;
}

bool enterprise_pool_enabled() noexcept {
    return env_bool("OPENADS_POOL_ENABLED", true);
}

bool enterprise_pool_odbc_enabled() noexcept {
    if (!enterprise_pool_enabled()) return false;
    return env_bool("OPENADS_POOL_ODBC_ENABLED", true);
}

bool enterprise_pool_sqlite_enabled() noexcept {
    if (!enterprise_pool_enabled()) return false;
    return env_bool("OPENADS_POOL_SQLITE_ENABLED", true);
}

bool enterprise_pool_oledb_enabled() noexcept {
    if (!enterprise_pool_enabled()) return false;
    return env_bool("OPENADS_POOL_OLEDB_ENABLED", true);
}

bool enterprise_server_pool_enabled() noexcept {
    return env_bool("OPENADS_SERVER_POOL", false);
}

std::uint32_t enterprise_server_pool_workers() noexcept {
    return env_u32("OPENADS_SERVER_POOL_WORKERS", 0);
}

} // namespace openads::sql_backend