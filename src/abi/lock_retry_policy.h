#pragma once

// Shared lock-retry policy between the ABI layer (AdsLockRecord/AdsLockTable)
// and the network session layer (LockRecord/LockTable wire opcodes).

#include <cstdint>
#include <string>
#include <thread>
#include <chrono>
#include <algorithm>

#include "util/client_config.h"

namespace openads::abi {

struct LockPolicy {
    std::uint32_t cycle_ms    = 100;   // ACE ceiling (AdsSetLockCycle)
    std::uint16_t retry_count = 0;     // single attempt by default (see below)

    // Sleep between attempt i and i+1 (0-based). The ACE contract is a flat
    // cycle_ms quantum, but a flat 100ms slice per retry multiplies badly
    // under many-instance contention (Pritpal Bedi's 700-instance B_BIG:
    // a contended RLock that resolves in 20 retries costs a full 2 s of
    // pure sleep). Most contentions resolve within a few milliseconds -
    // the holder is inside a REPLACE - so the first attempts recheck after
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

// Process-global lock policy for the client-facing ACE lock calls.
//
// DEFAULT IS SINGLE-ATTEMPT (retry_count 0): xBase RLOCK()/FLOCK() are
// single-attempt primitives - fail means fail, the application owns any
// retry loop (Vouch polls with its own waits; CacheRDD does the same).
// A hidden client-side wait underneath double-waits the app and, worse,
// turns every intentional lock PROBE (login semaphores, "is anyone
// logged" walks) into a full-budget burn: the server already answered
// fail-fast on the first attempt. (Pritpal Bedi, 2026-09-24: "If lock
// fails it must return immediately. App will decide on what it has to
// do next.")
//
// Opt back into ACE-style waits either per-app (AdsSetLockCycle /
// AdsSetLockRetryCount override these values at runtime, unchanged ABI
// contract) or per-install:
//   openads.ini:  lock_retry_count = 10   (any value > 0 re-enables waits)
//                 lock_cycle_ms    = 100  (ceiling per wait cycle)
//   env: OPENADS_LOCK_RETRY_COUNT / OPENADS_LOCK_CYCLE_MS (env wins).
inline LockPolicy& lock_retry_policy() {
    static LockPolicy p = [] {
        LockPolicy d;
        try {
            const std::string rc = openads::util::client_setting(
                "OPENADS_LOCK_RETRY_COUNT", "lock_retry_count");
            if (!rc.empty())
                d.retry_count = static_cast<std::uint16_t>(std::stoul(rc));
            const std::string cm = openads::util::client_setting(
                "OPENADS_LOCK_CYCLE_MS", "lock_cycle_ms");
            if (!cm.empty())
                d.cycle_ms = static_cast<std::uint32_t>(std::stoul(cm));
        } catch (...) {}
        return d;
    }();
    return p;
}

// Engine-internal lock policy for the append auto-lock (Table::append_blank).
// Deliberately decoupled from the client-facing policy above: the auto-lock
// on a freshly appended record is an internal wait the application cannot
// perform itself, and a single attempt there makes appends fail (5012)
// under any FLock/Browse convoy. Keeps the historical 100ms x 10 budget;
// tunable via engine_lock_retry_count / engine_lock_cycle_ms (env
// OPENADS_ENGINE_LOCK_RETRY_COUNT / OPENADS_ENGINE_LOCK_CYCLE_MS).
inline const LockPolicy& engine_append_lock_policy() {
    static const LockPolicy p = [] {
        LockPolicy d;
        d.cycle_ms    = 100;
        d.retry_count = 10;
        try {
            const std::string rc = openads::util::client_setting(
                "OPENADS_ENGINE_LOCK_RETRY_COUNT", "engine_lock_retry_count");
            if (!rc.empty())
                d.retry_count = static_cast<std::uint16_t>(std::stoul(rc));
            const std::string cm = openads::util::client_setting(
                "OPENADS_ENGINE_LOCK_CYCLE_MS", "engine_lock_cycle_ms");
            if (!cm.empty())
                d.cycle_ms = static_cast<std::uint32_t>(std::stoul(cm));
        } catch (...) {}
        return d;
    }();
    return p;
}

// Sleep for the interval the given policy prescribes before retry `attempt`.
inline void lock_retry_sleep(const LockPolicy& p, std::uint32_t attempt) {
    auto ms_ = p.sleep_before_attempt(attempt);
    if (ms_ > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms_));
    }
}

// Sleep for the interval the client-facing policy prescribes.
inline void lock_retry_sleep(std::uint32_t attempt) {
    lock_retry_sleep(lock_retry_policy(), attempt);
}

} // namespace openads::abi
