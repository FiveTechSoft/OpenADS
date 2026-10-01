#ifndef _WIN32

#include "platform/lock.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <fcntl.h>
#include <thread>
#include <unistd.h>
#ifdef __APPLE__
#include <algorithm>
#include <mutex>
#include <sys/stat.h>
#include <vector>
#endif

// OFD (open-file-description) locks are used only where they are proven
// to work. The macOS SDK defines F_OFD_SETLK, but there they made the
// engine wait on its own handles (a fresh table refused its first
// append lock and CI sat on it), so macOS keeps the process-scoped
// F_SETLK/F_SETLKW locks the engine was written for.
#if defined(F_OFD_SETLK) && !defined(__APPLE__)
#define OPENADS_USE_OFD_LOCKS 1
#else
#define OPENADS_USE_OFD_LOCKS 0
#endif

namespace openads::platform {

namespace {

util::Error os_error(const char* op) {
    util::Error e;
    e.code     = (errno == EAGAIN || errno == EACCES) ? 5012 : 5013;
    e.sub_code = errno;
    e.message  = op;
    return e;
}

// Prefer OFD (open-file-description) locks where available
// (Linux >= 3.15). Plain F_SETLK locks are process-scoped, which
// breaks the ByteLock contract that two fds in the SAME process
// should still contend (Win32 LockFile is fd-scoped). OFD locks
// are tied to the open file description, matching the Win32
// semantics used by the engine.
#if OPENADS_USE_OFD_LOCKS
constexpr int kSetLk  = F_OFD_SETLK;
[[maybe_unused]] constexpr int kSetLkW = F_OFD_SETLKW;
#else
constexpr int kSetLk  = F_SETLK;
[[maybe_unused]] constexpr int kSetLkW = F_SETLKW;
#endif

// off_t is signed: lock offsets at or above 2^63 (the ADT lock base is
// 0x8000000000000000) wrap NEGATIVE and fcntl rejects them with EINVAL,
// which used to disable ADT byte locks on POSIX silently. Fold them into
// the positive range; the mapping is deterministic across processes, so
// inter-process semantics are preserved (and no real file content ever
// lives at 2^62+, so the folded bytes stay collision-free).
std::uint64_t fold_lock_offset(std::uint64_t offset) {
    if (offset >= 0x8000000000000000ULL) {
        offset -= 0x4000000000000000ULL;
    }
    return offset;
}


#ifdef __APPLE__
// macOS has no OFD locks and plain fcntl locks are process-scoped, so two
// handles of the same process never conflict and one handle's unlock or
// close drops the other's range. This registry restores the per-handle
// semantics inside the process: every held byte range is recorded per
// file identity (dev, ino) with its owner handle, conflicts between
// different owners are refused here, and fcntl is still taken so other
// PROCESSES see the lock. Unlock re-applies the ranges that other owners
// still hold, because an fcntl unlock is process-wide.
struct HeldRange {
    std::uint64_t dev, ino;
    void*         owner;
    std::uint64_t start, len;
    bool          exclusive;
};
std::mutex            g_reg_mu;
std::vector<HeldRange> g_reg;

bool file_id(int fd, std::uint64_t& dev, std::uint64_t& ino) {
    struct stat st{};
    if (::fstat(fd, &st) != 0) return false;
    dev = static_cast<std::uint64_t>(st.st_dev);
    ino = static_cast<std::uint64_t>(st.st_ino);
    return true;
}

// len == 0 means "to end of file" for fcntl: treat as unbounded.
bool overlaps(std::uint64_t as, std::uint64_t al,
              std::uint64_t bs, std::uint64_t bl) {
    const std::uint64_t ae = al == 0 ? ~0ULL : as + al;
    const std::uint64_t be = bl == 0 ? ~0ULL : bs + bl;
    return as < be && bs < ae;
}

bool reg_conflict(std::uint64_t dev, std::uint64_t ino, void* owner,
                  std::uint64_t start, std::uint64_t len, bool excl) {
    for (const auto& h : g_reg) {
        if (h.dev != dev || h.ino != ino || h.owner == owner) continue;
        if (!overlaps(h.start, h.len, start, len)) continue;
        if (excl || h.exclusive) return true;
    }
    return false;
}
#endif

util::Result<ByteLock> do_lock(File& f, std::uint64_t offset,
                               std::uint64_t length, LockKind kind,
                               int cmd) {
    struct flock fl{};
    fl.l_type   = (kind == LockKind::Exclusive) ? F_WRLCK : F_RDLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = static_cast<off_t>(fold_lock_offset(offset));
    fl.l_len    = static_cast<off_t>(length);
    fl.l_pid    = 0;            // OFD requires l_pid=0
    // native_handle() stores (fd + 1) to avoid the nullptr/fd-0 collision.
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(f.native_handle()) - 1);
#ifdef __APPLE__
    std::uint64_t dev = 0, ino = 0;
    const bool have_id = file_id(fd, dev, ino);
    const std::uint64_t fstart = fold_lock_offset(offset);
    std::lock_guard<std::mutex> g(g_reg_mu);
    if (have_id && reg_conflict(dev, ino, f.native_handle(), fstart, length,
                                kind == LockKind::Exclusive)) {
        errno = EAGAIN;
        return os_error("in-process byte lock conflict");
    }
    if (::fcntl(fd, cmd, &fl) == -1) {
        auto e = os_error("fcntl(F_SETLK)");
        return e;
    }
    if (have_id) {
        g_reg.push_back({dev, ino, f.native_handle(), fstart, length,
                         kind == LockKind::Exclusive});
    }
    return ByteLock{f.native_handle(), offset, length};
#else
    if (::fcntl(fd, cmd, &fl) == -1) {
        auto e = os_error("fcntl(F_SETLK)");
        return e;
    }
    return ByteLock{f.native_handle(), offset, length};
#endif
}

} // namespace

#ifdef __APPLE__
void forget_byte_locks(void* native) noexcept {
    std::lock_guard<std::mutex> g(g_reg_mu);
    g_reg.erase(std::remove_if(g_reg.begin(), g_reg.end(),
                    [&](const HeldRange& h) { return h.owner == native; }),
                g_reg.end());
}
#endif

ByteLock::ByteLock(ByteLock&& other) noexcept
    : native_(other.native_), offset_(other.offset_), length_(other.length_) {
    other.native_ = nullptr;
}

ByteLock& ByteLock::operator=(ByteLock&& other) noexcept {
    if (this != &other) {
        release_();
        native_ = other.native_;
        offset_ = other.offset_;
        length_ = other.length_;
        other.native_ = nullptr;
    }
    return *this;
}

ByteLock::~ByteLock() { release_(); }

void ByteLock::release_() noexcept {
    if (native_ == nullptr) return;
    struct flock fl{};
    fl.l_type   = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = static_cast<off_t>(fold_lock_offset(offset_));
    fl.l_len    = static_cast<off_t>(length_);
    fl.l_pid    = 0;
    // native_handle() stores (fd + 1) to avoid the nullptr/fd-0 collision.
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(native_) - 1);
#ifdef __APPLE__
    {
        std::uint64_t dev = 0, ino = 0;
        const bool have_id = file_id(fd, dev, ino);
        std::lock_guard<std::mutex> g(g_reg_mu);
        const std::uint64_t fstart = fold_lock_offset(offset_);
        if (have_id) {
            auto it = std::find_if(g_reg.begin(), g_reg.end(),
                [&](const HeldRange& h) {
                    return h.dev == dev && h.ino == ino && h.owner == native_ &&
                           h.start == fstart && h.len == length_;
                });
            if (it != g_reg.end()) g_reg.erase(it);
        }
        ::fcntl(fd, kSetLk, &fl);
        if (have_id) {
            // The unlock above is process-wide: put back what other
            // owners (and this owner's remaining ranges) still hold.
            for (const auto& h : g_reg) {
                if (h.dev != dev || h.ino != ino) continue;
                if (!overlaps(h.start, h.len, fstart, length_)) continue;
                struct flock rl{};
                rl.l_type   = h.exclusive ? F_WRLCK : F_RDLCK;
                rl.l_whence = SEEK_SET;
                rl.l_start  = static_cast<off_t>(h.start);
                rl.l_len    = static_cast<off_t>(h.len);
                ::fcntl(fd, F_SETLK, &rl);
            }
        }
    }
#else
    ::fcntl(fd, kSetLk, &fl);
#endif
    native_ = nullptr;
}

util::Result<ByteLock> ByteLock::acquire(File& f, std::uint64_t offset,
                                         std::uint64_t length, LockKind kind) {
#if OPENADS_USE_OFD_LOCKS
    return do_lock(f, offset, length, kind, kSetLkW);
#else
    // Process-scoped locks (macOS) can still wait forever on a stale
    // holder in another process. Poll the non-blocking command and give
    // up after a bounded wait, so a stall becomes a lock error instead of
    // a hang.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(30);
    for (;;) {
        auto r = do_lock(f, offset, length, kind, kSetLk);
        if (r) return r;
        if (r.error().sub_code != EAGAIN && r.error().sub_code != EACCES) {
            return r;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            struct flock q{};
            q.l_type   = (kind == LockKind::Exclusive) ? F_WRLCK : F_RDLCK;
            q.l_whence = SEEK_SET;
            q.l_start  = static_cast<off_t>(fold_lock_offset(offset));
            q.l_len    = static_cast<off_t>(length);
            int fd = static_cast<int>(
                reinterpret_cast<intptr_t>(f.native_handle()) - 1);
            long holder = -1;
            const int get_rc   = ::fcntl(fd, F_GETLK, &q);
            const int get_errn = (get_rc == -1) ? errno : 0;
            if (get_rc == 0 && q.l_type != F_UNLCK) {
                holder = static_cast<long>(q.l_pid);
            }
            std::fprintf(stderr,
                "openads: byte lock wait timed out (offset=%llu len=%llu "
                "set_errno=%d get_rc=%d get_errno=%d get_type=%d "
                "holder_pid=%ld self_pid=%ld fd=%d)\n",
                static_cast<unsigned long long>(offset),
                static_cast<unsigned long long>(length),
                r.error().sub_code, get_rc, get_errn,
                static_cast<int>(q.l_type), holder,
                static_cast<long>(::getpid()), fd);
            return r;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
#endif
}

util::Result<ByteLock> ByteLock::try_acquire(File& f, std::uint64_t offset,
                                             std::uint64_t length,
                                             LockKind kind) {
    return do_lock(f, offset, length, kind, kSetLk);
}

util::Result<bool> ByteLock::probe(File& f, std::uint64_t offset,
                                   std::uint64_t length) {
    struct flock fl{};
    fl.l_type   = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = static_cast<off_t>(fold_lock_offset(offset));
    fl.l_len    = static_cast<off_t>(length);
    fl.l_pid    = 0;
    // native_handle() stores (fd + 1) to avoid the nullptr/fd-0 collision.
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(f.native_handle()) - 1);
#if OPENADS_USE_OFD_LOCKS
    // F_OFD_GETLK reports conflicts against OTHER open file descriptions -
    // exactly "another handle/session holds this byte". The querying fd's
    // own locks are invisible to it; callers check their own list first.
    if (::fcntl(fd, F_OFD_GETLK, &fl) == -1) return os_error("fcntl(F_OFD_GETLK)");
#else
    // Pre-3.15 fallback: process-scoped GETLK reports other PROCESSES only;
    // same-process conflicts stay invisible (degraded, never wrong-safe).
    if (::fcntl(fd, F_GETLK, &fl) == -1) return os_error("fcntl(F_GETLK)");
#endif
#ifdef __APPLE__
    {
        std::uint64_t dev = 0, ino = 0;
        if (file_id(fd, dev, ino)) {
            std::lock_guard<std::mutex> g(g_reg_mu);
            if (reg_conflict(dev, ino, f.native_handle(),
                             fold_lock_offset(offset), length, true)) {
                return true;
            }
        }
    }
#endif
    return fl.l_type != F_UNLCK;
}

util::Result<void> ByteLock::release() {
    release_();
    return {};
}

} // namespace openads::platform

#endif // !_WIN32
