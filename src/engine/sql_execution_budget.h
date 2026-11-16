#pragma once
#include "util/client_config.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>

namespace openads::engine {

// Operator override of a remote-SQL limit: environment variable `env`, else
// key `key` in the openads.ini beside the server (util::client_setting).
// Absent / unparsable -> `def`; 0 -> unlimited (UINT64_MAX). Real ADS has
// none of these limits; servers that run maintenance SQL over large tables
// (multi-million-row UPDATE/DELETE) raise or disable them.
inline std::uint64_t remote_sql_limit(const char* env, const char* key,
                                      std::uint64_t def) {
    const std::string v = util::client_setting(env, key);
    if (v.empty()) return def;
    char* end = nullptr;
    const unsigned long long n = std::strtoull(v.c_str(), &end, 10);
    if (end == v.c_str()) return def;
    return n == 0 ? std::numeric_limits<std::uint64_t>::max()
                  : static_cast<std::uint64_t>(n);
}
// Engine steps per remote statement (remote_sql_max_steps, default 1,000,000).
inline std::uint64_t remote_sql_max_steps() {
    return remote_sql_limit("OPENADS_REMOTE_SQL_MAX_STEPS",
                            "remote_sql_max_steps", 1000000);
}
// Wall-clock deadline per remote statement in ms (remote_sql_timeout_ms,
// default 5000).
inline std::uint64_t remote_sql_timeout_ms() {
    return remote_sql_limit("OPENADS_REMOTE_SQL_TIMEOUT_MS",
                            "remote_sql_timeout_ms", 5000);
}

// Cooperative, thread-local allowance shared by one remote SQL call tree.
// Nested scopes never refill it. Local execution is unchanged.
struct SqlExecutionBudget {
    bool active = false, exhausted = false;
    std::uint64_t remaining = 0;
    std::chrono::steady_clock::time_point deadline;
};
inline thread_local SqlExecutionBudget sql_execution_budget;
struct SqlExecutionScope {
    bool owner = false;
    explicit SqlExecutionScope(bool remote) {
        if (remote && !sql_execution_budget.active)
            arm(remote_sql_max_steps(), remote_sql_timeout_ms());
    }
    SqlExecutionScope(bool remote, std::uint64_t steps,
                      std::uint64_t milliseconds) {
        if (remote && !sql_execution_budget.active) arm(steps, milliseconds);
    }
    void arm(std::uint64_t steps, std::uint64_t milliseconds) {
        owner = true;
        using clock = std::chrono::steady_clock;
        // Unlimited (or absurdly long) deadline: never expires.
        const auto deadline =
            milliseconds >= (std::uint64_t{1} << 40)
                ? clock::time_point::max()
                : clock::now() + std::chrono::milliseconds(milliseconds);
        sql_execution_budget = {true, false, steps, deadline};
    }
    ~SqlExecutionScope() { if (owner) sql_execution_budget = {}; }
};
inline bool sql_execution_step(std::uint64_t amount = 1) {
    auto& b = sql_execution_budget;
    if (!b.active) return true;
    if (b.exhausted || amount > b.remaining ||
        std::chrono::steady_clock::now() >= b.deadline) {
        b.exhausted = true;
        return false;
    }
    b.remaining -= amount;
    return true;
}
inline bool sql_execution_exhausted() {
    return !sql_execution_step(0);
}
} // namespace openads::engine
