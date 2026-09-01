#pragma once

// Shared lock-retry policy between the ABI layer (AdsLockRecord/AdsLockTable)
// and the network session layer (LockRecord/LockTable wire opcodes).

#include <cstdint>
#include <thread>
#include <chrono>
#include <algorithm>

namespace openads::abi {

struct LockPolicy {
    std::uint32_t cycle_ms    = 100;   // ACE default (AdsSetLockCycle)
    std::uint16_t retry_count = 10;    // ACE default (AdsSetLockRetryCount)

    // Sleep between attempt i and i+1 (0-based). The ACE contract is a flat
    // cycle_ms quantum, but a flat 100ms slice per retry multiplies badly
    // under many-instance contention (Pritpal Bedi's 700-instance B_BIG:
    // a contended RLock that resolves in 20 retries costs a full 2 s of
    // pure sleep). Most contentions resolve within a few milliseconds â€”
    // the holder is inside a REPLACE â€” so the first attempts recheck after
    // 2/4/8 ms and only the later attempts fall back to the configured
    // cycle_ms quantum. AdsSetLockCycle still controls the ceiling.
    std::uint32_t sleep_before_attempt(std::uint32_t attempt) const {
        if (cycle_ms == 0) return 0;
        std::uint32_t backoff = 2u << (attempt < 6 ? attempt : 5);
        return backoff > cycle_ms ? cycle_ms : backoff;
    }

    // Total wait budget for a contended lock, in ms (what N flat
    // cycle_ms cycles would have cost under the plain ACE contract).
    std::uint32_t budget_ms() const {
        return cycle_ms * retry_count;
    }
};

// Process-global lock policy â€” shared between ABI entry points and the
// server session.  ADS_SETLOCKCYCLE / ADS_SETLOCKRETRYCOUNT modify it.
inline LockPolicy& lock_retry_policy() {
    static LockPolicy p;
    return p;
}

// Sleep for the interval the policy prescribes before retry `attempt`.
inline void lock_retry_sleep(std::uint32_t attempt) {
    const auto& p = lock_retry_policy();
    auto ms_ = p.sleep_before_attempt(attempt);
    if (ms_ > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms_));
    }
}

} // namespace openads::abi
