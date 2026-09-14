#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace openads::network {

// M12.32 — Server-side distributed mutex manager.
// Each named mutex is owned by one session at a time. Other sessions
// block (with optional timeout) until the owner unlocks.
class MutexManager {
public:
    // Create a named mutex. Returns false if it already exists.
    // `creator` is the owning session's serial (see
    // Session::conn_serial): the mutex is destroyed automatically when
    // the creator session ends, so a dead process can never wedge a
    // name forever (field: OAds_MutexCreate failing until server
    // restart). Empty creator = no auto-destroy (tests / tools).
    bool create(const std::string& name, const std::string& creator = {});

    // Lock a named mutex. Blocks until acquired or timeout expires.
    // timeout_ms = 0 means wait forever. Returns true if acquired.
    bool lock(const std::string& name, std::uint32_t timeout_ms,
              const std::string& owner);

    // Non-blocking lock attempt. Returns true if acquired immediately.
    bool try_lock(const std::string& name, const std::string& owner);

    // Unlock a named mutex. Only the owner can unlock.
    // Returns true if successfully unlocked.
    bool unlock(const std::string& name, const std::string& owner);

    // Destroy a named mutex. Only the owner (or any session if unowned)
    // can destroy. Returns true if destroyed.
    bool destroy(const std::string& name, const std::string& owner);

    // Check if a mutex exists.
    bool exists(const std::string& name) const;

    // Check if a mutex is currently locked.
    bool is_locked(const std::string& name) const;

    // Get the owner of a locked mutex (empty if unowned).
    std::string get_owner(const std::string& name) const;

    // Cleanup all mutexes owned by a session (called on disconnect).
    void release_all(const std::string& owner);

    // Session teardown: unlock everything held by `owner`, then destroy
    // every mutex created by `owner` — unless a DIFFERENT live session
    // currently holds it, in which case creation transfers to that
    // holder instead of destroying under it. Called from
    // Session::cleanup, which runs on Disconnect, peer-close and
    // exception paths alike, so a vanished process leaves neither a
    // stale lock nor a stale name behind.
    void release_session(const std::string& owner);

private:
    struct MutexState {
        bool        locked = false;
        std::string owner;    // current lock holder (session serial)
        std::string creator;  // session serial that created the name
        std::mutex  mu;
        std::condition_variable cv;
    };

    mutable std::mutex mu_;
    std::unordered_map<std::string, std::shared_ptr<MutexState>> mutexes_;
};

} // namespace openads::network
