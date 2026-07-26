#pragma once

// Shared lock-retry policy between the ABI layer (AdsLockRecord/AdsLockTable)
// and the network session layer (LockRecord/LockTable wire opcodes).

#include <cstdint>
#include <thread>
#include <chrono>

namespace openads::abi {

struct LockPolicy {
    std::uint32_t cycle_ms    = 100;   // ACE default
    std::uint16_t retry_count = 10;
};

// Process-global lock policy — shared between ABI entry points and the
// server session.  ADS_SETLOCKCYCLE / ADS_SETLOCKRETRYCOUNT modify it.
inline LockPolicy& lock_retry_policy() {
    static LockPolicy p;
    return p;
}

} // namespace openads::abi
