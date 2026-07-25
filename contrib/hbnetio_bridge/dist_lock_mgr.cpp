// ============================================================================
// Distributed Lock Manager Implementation
// ============================================================================
//
// Implements the distributed locking pattern from hbnetio for OpenADS.
// The core algorithm mirrors the __isLocked() function you shared:
//
//   1. Open the lock file (create if needed)
//   2. Try to acquire exclusive lock
//   3. If blocked, sleep and retry
//   4. Track lock state for cleanup
//
// Copyright 2026 OpenADS Contributors
// ============================================================================

#include "dist_lock_mgr.h"

#include <algorithm>
#include <thread>

namespace openads::network {

// ===========================================================================
// DistributedLockManager
// ===========================================================================

DistributedLockManager::DistributedLockManager(VfsAdapter& adapter)
    : adapter_(adapter) {}

DistributedLockManager::~DistributedLockManager() {
    // Release all outstanding locks
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& [path, info] : locks_) {
        if (info.held) {
            VfsHandle h;
            h.fd    = 0;
            h.valid = false;
            h.path  = info.lock_file;
            // Best-effort unlock — ignore errors during destruction
            adapter_.unlock(h, info.offset, info.length);
            // Also try to close the lock file handle
            adapter_.close(h);
        }
    }
    locks_.clear();
    session_locks_.clear();
}


// ---------------------------------------------------------------------------
// Lock file naming
// ---------------------------------------------------------------------------

std::string DistributedLockManager::make_lock_path(
    const std::string& resource) {
    if (resource.size() >= 4 &&
        resource.substr(resource.size() - 4) == ".lck") {
        return resource;   // already has .lck extension
    }
    return resource + ".lck";
}


// ---------------------------------------------------------------------------
// try_lock_with_retry — internal retry loop
// ---------------------------------------------------------------------------

util::Result<VfsHandle>
DistributedLockManager::try_lock_with_retry(const std::string& lock_path,
                                             bool wait,
                                             std::uint32_t timeout_ms) {
    // Step 1: Open (or create) the lock file
    // This mirrors: pHandle := hb_vfOpen(cLockFile, FO_READWRITE)
    //              if Empty(pHandle)
    //                 pHandle := hb_vfOpen(cLockFile, HB_FO_CREAT + FO_READWRITE)
    //              endif
    auto handle = adapter_.open(lock_path,
                                vfs::FO_READWRITE | vfs::FO_CREAT);
    if (!handle.has_value()) return handle.error();

    VfsHandle h = std::move(handle).value();

    // Step 2: Try to acquire exclusive lock
    // This mirrors: hb_vfLock(pHandle, 0, 1, HB_FLX_EXCLUSIVE + HB_FLX_WAIT)
    std::uint16_t lock_flags = vfs::FLX_EXCLUSIVE;
    if (wait) lock_flags |= vfs::FLX_WAIT;

    std::uint32_t timeout = (timeout_ms > 0) ? timeout_ms : default_timeout_ms_;
    std::uint32_t elapsed = 0;

    while (true) {
        auto locked = adapter_.lock(h, 0, 1, lock_flags);
        if (locked && locked.value()) {
            return h;   // Lock acquired
        }

        if (!wait) {
            adapter_.close(h);
            return util::Error{10, 0, "lock not available (non-blocking)", ""};
        }

        // Retry with backoff (mirrors: hb_idleSleep(2))
        if (elapsed >= timeout) {
            adapter_.close(h);
            return util::Error{11, 0, "lock timeout exceeded", ""};
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(retry_interval_ms_));
        elapsed += retry_interval_ms_;
    }
}


// ---------------------------------------------------------------------------
// acquire — blocking lock acquisition
// ---------------------------------------------------------------------------

util::Result<DistributedLockGuard>
DistributedLockManager::acquire(const std::string& lock_path,
                                 std::uint32_t timeout_ms,
                                 std::uint64_t session_id) {
    std::string actual_path = make_lock_path(lock_path);

    auto handle = try_lock_with_retry(actual_path, true, timeout_ms);
    if (!handle.has_value()) return handle.error();

    VfsHandle h = std::move(handle).value();

    // Register the lock
    {
        std::lock_guard<std::mutex> lock(mu_);

        LockInfo info;
        info.lock_file  = actual_path;
        info.resource   = lock_path;
        info.offset     = 0;
        info.length     = 1;
        info.held       = true;
        info.acquired_at = std::chrono::steady_clock::now();
        info.session_id = session_id;

        locks_[actual_path] = info;
        session_locks_[session_id].push_back(actual_path);
    }

    return DistributedLockGuard(adapter_, h, actual_path, session_id);
}


// ---------------------------------------------------------------------------
// try_acquire — non-blocking lock attempt
// ---------------------------------------------------------------------------

util::Result<DistributedLockGuard>
DistributedLockManager::try_acquire(const std::string& lock_path,
                                     std::uint64_t session_id) {
    std::string actual_path = make_lock_path(lock_path);

    auto handle = try_lock_with_retry(actual_path, false, 0);
    if (!handle.has_value()) return handle.error();

    VfsHandle h = std::move(handle).value();

    {
        std::lock_guard<std::mutex> lock(mu_);

        LockInfo info;
        info.lock_file  = actual_path;
        info.resource   = lock_path;
        info.offset     = 0;
        info.length     = 1;
        info.held       = true;
        info.acquired_at = std::chrono::steady_clock::now();
        info.session_id = session_id;

        locks_[actual_path] = info;
        session_locks_[session_id].push_back(actual_path);
    }

    return DistributedLockGuard(adapter_, h, actual_path, session_id);
}


// ---------------------------------------------------------------------------
// release — manual lock release
// ---------------------------------------------------------------------------

util::Result<void>
DistributedLockManager::release(const std::string& lock_path) {
    std::string actual_path = make_lock_path(lock_path);

    std::lock_guard<std::mutex> lock(mu_);
    auto it = locks_.find(actual_path);
    if (it == locks_.end() || !it->second.held) {
        return util::Error{12, 0, "lock not held", ""};
    }

    // Unlock on server
    VfsHandle h;
    h.fd    = 0;
    h.valid = false;
    h.path  = actual_path;
    adapter_.unlock(h, 0, 1);
    adapter_.close(h);

    it->second.held = false;

    // Remove from session tracking
    auto sit = session_locks_.find(it->second.session_id);
    if (sit != session_locks_.end()) {
        auto& vec = sit->second;
        vec.erase(std::remove(vec.begin(), vec.end(), actual_path),
                  vec.end());
        if (vec.empty()) session_locks_.erase(sit);
    }

    locks_.erase(it);
    return {};
}


// ---------------------------------------------------------------------------
// release_all — release all locks for a session
// ---------------------------------------------------------------------------

std::uint32_t
DistributedLockManager::release_all(std::uint64_t session_id) {
    std::lock_guard<std::mutex> lock(mu_);

    auto sit = session_locks_.find(session_id);
    if (sit == session_locks_.end()) return 0;

    std::uint32_t count = 0;
    for (const auto& path : sit->second) {
        auto lit = locks_.find(path);
        if (lit != locks_.end() && lit->second.held) {
            VfsHandle h;
            h.fd    = 0;
            h.valid = false;
            h.path  = path;
            adapter_.unlock(h, 0, 1);
            adapter_.close(h);
            lit->second.held = false;
            locks_.erase(lit);
            ++count;
        }
    }

    session_locks_.erase(sit);
    return count;
}


// ---------------------------------------------------------------------------
// Status queries
// ---------------------------------------------------------------------------

bool DistributedLockManager::is_locked(const std::string& lock_path) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = locks_.find(make_lock_path(lock_path));
    return it != locks_.end() && it->second.held;
}

util::Result<LockInfo>
DistributedLockManager::lock_info(const std::string& lock_path) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = locks_.find(make_lock_path(lock_path));
    if (it == locks_.end()) {
        return util::Error{13, 0, "lock not found", ""};
    }
    return it->second;
}

std::vector<LockInfo>
DistributedLockManager::session_locks(std::uint64_t session_id) const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<LockInfo> result;

    auto sit = session_locks_.find(session_id);
    if (sit == session_locks_.end()) return result;

    for (const auto& path : sit->second) {
        auto it = locks_.find(path);
        if (it != locks_.end()) {
            result.push_back(it->second);
        }
    }

    return result;
}

std::size_t DistributedLockManager::active_lock_count() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::size_t count = 0;
    for (const auto& [path, info] : locks_) {
        if (info.held) ++count;
    }
    return count;
}


// ---------------------------------------------------------------------------
// heartbeat — refresh a long-held lock
// ---------------------------------------------------------------------------

util::Result<void>
DistributedLockManager::heartbeat(const std::string& lock_path) {
    std::string actual_path = make_lock_path(lock_path);

    std::lock_guard<std::mutex> lock(mu_);
    auto it = locks_.find(actual_path);
    if (it == locks_.end() || !it->second.held) {
        return util::Error{14, 0, "lock not held for heartbeat", ""};
    }

    // Refresh the acquisition timestamp
    it->second.acquired_at = std::chrono::steady_clock::now();

    // Optionally: send a zero-length lock on the same range
    // to signal liveness to the server. This is a no-op on
    // most hbnetio configurations but safe to call.
    VfsHandle h;
    h.fd    = 0;
    h.valid = false;
    h.path  = actual_path;
    adapter_.test_lock(h, 0, 1, vfs::FLX_EXCLUSIVE);

    return {};
}


// ===========================================================================
// DistributedLockGuard
// ===========================================================================

DistributedLockGuard::DistributedLockGuard(VfsAdapter& adapter,
                                           VfsHandle handle,
                                           std::string lock_path,
                                           std::uint64_t session_id)
    : adapter_(&adapter), handle_(std::move(handle)),
      lock_path_(std::move(lock_path)), session_id_(session_id),
      locked_(true) {}

DistributedLockGuard::~DistributedLockGuard() {
    release();
}

DistributedLockGuard::DistributedLockGuard(
    DistributedLockGuard&& other) noexcept
    : adapter_(other.adapter_),
      handle_(std::move(other.handle_)),
      lock_path_(std::move(other.lock_path_)),
      session_id_(other.session_id_),
      locked_(other.locked_) {
    other.locked_ = false;
    other.adapter_ = nullptr;
}

DistributedLockGuard&
DistributedLockGuard::operator=(DistributedLockGuard&& other) noexcept {
    if (this != &other) {
        release();
        adapter_   = other.adapter_;
        handle_    = std::move(other.handle_);
        lock_path_ = std::move(other.lock_path_);
        session_id_ = other.session_id_;
        locked_    = other.locked_;
        other.locked_ = false;
        other.adapter_ = nullptr;
    }
    return *this;
}

void DistributedLockGuard::release() {
    if (locked_ && adapter_) {
        adapter_->unlock(handle_, 0, 1);
        locked_ = false;
    }
}

void DistributedLockGuard::close() {
    if (locked_ && adapter_) {
        adapter_->unlock(handle_, 0, 1);
        adapter_->close(handle_);
        locked_ = false;
    }
}

} // namespace openads::network
