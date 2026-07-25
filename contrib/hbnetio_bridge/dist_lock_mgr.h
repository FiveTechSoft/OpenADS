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
// The key pattern from hbnetio (your __isLocked function):
//
//   hb_vfOpen(lock_file, FO_READWRITE)          // create/open .lck file
//   hb_vfLock(handle, 0, 1,                      // lock byte 0..1
//             HB_FLX_EXCLUSIVE + HB_FLX_WAIT)    // exclusive, blocking
//   // ... critical section ...
//   hb_vfUnlock(handle, 0, 1)
//   hb_vfClose(handle)
//
// This module wraps that pattern with:
//
//   1. A lock-retry strategy with configurable timeout and backoff
//   2. Lock file management (automatic .lck file lifecycle)
//   3. Integration with OpenADS's LockMgr for local operations
//   4. Transaction-safe lock release (RAII via DistributedLockGuard)
//
// Architecture:
//
//   OpenADS Client                    hbnetio Server (EC2)
//   ──────────────                    ─────────────────────
//   DistLockMgr::acquire()   ──TCP──> hb_vfOpen(.lck)
//                                     hb_vfLock(0,1,EXCL|WAIT)
//   DistLockMgr::release()   ──TCP──> hb_vfUnlock(0,1)
//                                     hb_vfClose()
//
// Usage:
//   DistLockMgr lock_mgr(vfs_adapter);
//   auto guard = lock_mgr.acquire("chats/room1.lck",
//                                  /*timeout_ms=*/5000);
//   if (guard) {
//       // ... exclusive access, file operations ...
//   }
//   // guard destructor releases the lock
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
    std::string   lock_file;       // e.g. "chats/room1.lck"
    std::string   resource;        // the actual resource being protected
    std::uint64_t offset = 0;
    std::uint64_t length = 1;
    bool          held = false;
    std::chrono::steady_clock::time_point acquired_at{};
    std::uint64_t session_id = 0;  // owning session
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
// The internal lock registry is guarded by a mutex.
//
class DistributedLockManager {
public:
    explicit DistributedLockManager(VfsAdapter& adapter);
    ~DistributedLockManager();

    DistributedLockManager(const DistributedLockManager&) = delete;
    DistributedLockManager& operator=(const DistributedLockManager&) = delete;

    // -- Configuration ------------------------------------------------------

    // Set the default timeout for blocking lock attempts (ms).
    void set_default_timeout(std::uint32_t ms) { default_timeout_ms_ = ms; }

    // Set the retry interval for non-blocking lock attempts (ms).
    void set_retry_interval(std::uint32_t ms) { retry_interval_ms_ = ms; }

    // Set maximum number of retries before giving up.
    void set_max_retries(std::uint32_t n) { max_retries_ = n; }

    // -- Lock operations ----------------------------------------------------

    // Acquire an exclusive lock on a remote lock file.
    //
    // This mirrors the __isLocked pattern:
    //   1. Open (or create) the .lck file via VfsAdapter::open()
    //   2. Attempt exclusive lock via VfsAdapter::lock()
    //   3. If lock fails and wait=true, retry with backoff
    //   4. Return RAII guard that releases lock on destruction
    //
    // Parameters:
    //   lock_path   — path to the lock file (e.g. "chats/room1.lck")
    //   timeout_ms  — maximum time to wait (0 = use default)
    //   session_id  — identifier of the owning session
    //
    // Returns:
    //   DistributedLockGuard on success (RAII — auto-releases)
    //   util::Error on failure (timeout, connection error, etc.)
    util::Result<DistributedLockGuard> acquire(
        const std::string& lock_path,
        std::uint32_t timeout_ms = 0,
        std::uint64_t session_id = 0);

    // Non-blocking attempt. Returns immediately.
    util::Result<DistributedLockGuard> try_acquire(
        const std::string& lock_path,
        std::uint64_t session_id = 0);

    // Release a specific lock (usually done by guard destructor).
    util::Result<void> release(const std::string& lock_path);

    // Release all locks held by a session (cleanup on disconnect).
    std::uint32_t release_all(std::uint64_t session_id);

    // -- Status queries -----------------------------------------------------

    // Check if a lock file is currently held locally.
    bool is_locked(const std::string& lock_path) const;

    // Get info about a specific lock.
    util::Result<LockInfo> lock_info(const std::string& lock_path) const;

    // Get all locks held by a session.
    std::vector<LockInfo> session_locks(std::uint64_t session_id) const;

    // Get total number of active locks.
    std::size_t active_lock_count() const;

    // -- Convenience: file-based locking (like PushChatNoteAndPullNotes) ----

    // Acquire a lock on a file, perform an operation, then release.
    //
    // This is the C++ equivalent of:
    //   IF __isLocked( cLockFile, @pHandle )
    //       // ... do work ...
    //       hb_vfUnlock( pHandle, 0, 1 )
    //       hb_vfClose( pHandle )
    //   ENDIF
    //
    // The callback receives a VfsHandle for the lock file and
    // should return true on success.
    template<typename Func>
    util::Result<void> with_lock(const std::string& lock_path,
                                 std::uint32_t timeout_ms,
                                 Func&& operation) {
        auto guard = acquire(lock_path, timeout_ms);
        if (!guard) return guard.error();

        if (!operation()) {
            release(lock_path);
            return util::Error{1, "operation failed while lock held"};
        }

        return util::Ok();
    }

    // -- Heartbeat / keep-alive for long-held locks -------------------------

    // Send a periodic "still alive" signal for a lock.
    // On some hbnetio configurations, locks may time out if not
    // refreshed. Call this periodically for long transactions.
    util::Result<void> heartbeat(const std::string& lock_path);

private:
    // Derive the lock file name from a resource name.
    // "chats/room1.json" -> "chats/room1.json.lck"
    static std::string make_lock_path(const std::string& resource);

    // Internal lock attempt with retry logic.
    util::Result<VfsHandle> try_lock_with_retry(
        const std::string& lock_path,
        bool wait,
        std::uint32_t timeout_ms);

    VfsAdapter& adapter_;
    mutable std::mutex mu_;

    std::uint32_t default_timeout_ms_ = 5000;
    std::uint32_t retry_interval_ms_  = 200;
    std::uint32_t max_retries_        = 25;

    // Active lock registry: lock_path -> LockInfo
    std::unordered_map<std::string, LockInfo> locks_;

    // Session -> set of lock paths (for bulk release)
    std::unordered_map<std::uint64_t,
        std::vector<std::string>> session_locks_;
};


// ---------------------------------------------------------------------------
// DistributedLockGuard — RAII lock holder
// ---------------------------------------------------------------------------
//
// Returned by DistributedLockManager::acquire(). Holds the lock file
// handle and releases it on destruction. Copy/move is disabled to
// prevent accidental double-release.
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

} // namespace openads::network
