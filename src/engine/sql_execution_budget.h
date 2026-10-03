#pragma once
#include <chrono>
#include <cstdint>

namespace openads::engine {
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
    explicit SqlExecutionScope(bool remote, std::uint64_t steps = 1000000,
                               std::uint32_t milliseconds = 5000) {
        if (remote && !sql_execution_budget.active) {
            owner = true;
            sql_execution_budget = {true, false, steps,
                std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds)};
        }
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
