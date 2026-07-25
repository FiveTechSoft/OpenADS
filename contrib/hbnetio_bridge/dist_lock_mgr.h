#pragma once

// ============================================================================
// Distributed Lock Manager — hbnetio / OpenADS Bridge
// ============================================================================
//
// Bridges harbour's hb_vfLock-based distributed locking (as used in the
// PushChatNoteAndPullNotes pattern) with OpenADS's local LockMgr. This
// enables multi-client concurrent access to shared files on a remote
// hbnetio server, where the server-side byte-range locks are coordinated
// through the hbnetio protocol rather than local OS locks.
//
// Copyright 2026 OpenADS Contributors
// Licensed under the same terms as OpenADS (see LICENSE)
// ============================================================================

#include "vfs_adapter.h"
#include "util/result.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <chrono>

namespace openads::network {

// ---------------------------------------------------------------------------
// Lock status tracking
// ---------------------------------------------------------------------------
struct LockInfo {
    std::string   lock_file;
    std::string   resource;
    std::uint64_t offset = 0;
    std::uint64_t length = 1;
    bool          held = false;
    std::chrono::steady_clock::time_point acquired_at{};
    std::uint64_t session_id = 0;
};


// ---------------------------------------------------------------------------
// DistributedLockGuard — RAII lock holder
// ---------------------------------------------------------------------------
//
// Returned by DistributedLockManager::acquire(). Holds the lock file
// handle and releases it on destruction. Move-only (no copy).
//
class DistributedLockGuard {
public:
    DistributedLockGuard() = default;

    // Construct with active lock
    DistributedLockGuard(VfsAdapter& adapter, VfsHandle handle,
                         std::string lock_path, std::uint64_t session_id);

    // Release lock on destruction
    ~DistributedLockGuard();

    DistributedLockGuard(const DistributedLockGuard&) = delete;
    DistributedLockGuard& operator=(const DistributedLockGuard&) = delete;

    // Move support
    DistributedLockGuard(DistributedLockGuard&& other) noexcept;
    DistributedLockGuard& operator=(DistributedLockGuard&& other) noexcept;

    bool locked() const noexcept { return locked_; }
    const std::string& lock_path() const noexcept { return lock_path_; }
    VfsHandle& handle() noexcept { return handle_; }

    // Manually release before destructor.
    void release();

    // Release and close the lock file handle.
    void close();

private:
    VfsAdapter*    adapter_ = nullptr;
    VfsHandle      handle_{};
    std::string    lock_path_;
    std::uint64_t  session_id_ = 0;
    bool           locked_ = false;
};


// ---------------------------------------------------------------------------
// DistributedLockManager
// ---------------------------------------------------------------------------
//
// Manages distributed locks across multiple OpenADS clients connecting
// to the same hbnetio server. Lock state is tracked locally for fast
// status queries, while actual lock enforcement happens on the server.
//
// Thread safety: all public methods are safe to call from any thread.
//
class DistributedLockManager {
public:
    explicit DistributedLockManager(VfsAdapter& adapter);
    ~DistributedLockManager();

    DistributedLockManager(const DistributedLockManager&) = delete;
    DistributedLockManager& operator=(const DistributedLockManager&) = delete;

    // -- Configuration ------------------------------------------------------

    void set_default_timeout(std::uint32_t ms) { default_timeout_ms_ = ms; }
    void set_retry_interval(std::uint32_t ms) { retry_interval_ms_ = ms; }
    void set_max_retries(std::uint32_t n) { max_retries_ = n; }

    // -- Lock operations ----------------------------------------------------

    util::Result<DistributedLockGuard> acquire(
        const std::string& lock_path,
        std::uint32_t timeout_ms = 0,
        std::uint64_t session_id = 0);

    util::Result<DistributedLockGuard> try_acquire(
        const std::string& lock_path,
        std::uint64_t session_id = 0);

    util::Result<void> release(const std::string& lock_path);

    std::uint32_t release_all(std::uint64_t session_id);

    // -- Status queries -----------------------------------------------------

    bool is_locked(const std::string& lock_path) const;
    util::Result<LockInfo> lock_info(const std::string& lock_path) const;
    std::vector<LockInfo> session_locks(std::uint64_t session_id) const;
    std::size_t active_lock_count() const;

    // -- Convenience: with_lock (like __isLocked pattern) -------------------

    template<typename Func>
    util::Result<void> with_lock(const std::string& lock_path,
                                 std::uint32_t timeout_ms,
                                 Func&& operation) {
        auto guard = acquire(lock_path, timeout_ms);
        if (!guard.has_value()) return guard.error();

        if (!operation()) {
            release(lock_path);
            return util::Error{1, 0, "operation failed while lock held", ""};
        }

        return {};
    }

    // -- Heartbeat / keep-alive for long-held locks -------------------------

    util::Result<void> heartbeat(const std::string& lock_path);

private:
    static std::string make_lock_path(const std::string& resource);

    util::Result<VfsHandle> try_lock_with_retry(
        const std::string& lock_path,
        bool wait,
        std::uint32_t timeout_ms);

    VfsAdapter& adapter_;
    mutable std::mutex mu_;

    std::uint32_t default_timeout_ms_ = 5000;
    std::uint32_t retry_interval_ms_  = 200;
    std::uint32_t max_retries_        = 25;

    std::unordered_map<std::string, LockInfo> locks_;
    std::unordered_map<std::uint64_t,
        std::vector<std::string>> session_locks_;
};

} // namespace openads::network