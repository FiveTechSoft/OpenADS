#ifndef _WIN32

#include "platform/lock.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

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
#ifdef F_OFD_SETLK
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
    if (::fcntl(fd, cmd, &fl) == -1) {
        auto e = os_error("fcntl(F_SETLK)");
        return e;
    }
    return ByteLock{f.native_handle(), offset, length};
}

} // namespace

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
    ::fcntl(fd, kSetLk, &fl);
    native_ = nullptr;
}

util::Result<ByteLock> ByteLock::acquire(File& f, std::uint64_t offset,
                                         std::uint64_t length, LockKind kind) {
#if defined(F_OFD_SETLK) && !defined(__APPLE__)
    return do_lock(f, offset, length, kind, kSetLkW);
#else
    // macOS: its SDK defines the OFD commands, but a blocking F_OFD_SETLKW
    // waits forever when a leaked holder lives in this same process (CI
    // test runs sat on it until the job timeout). Poll the non-blocking
    // command and give up after a bounded wait, so a stall becomes a lock
    // error instead of a hang.
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
#ifdef F_OFD_GETLK
            q.l_pid = 0;
            const int kGetLk = F_OFD_GETLK;
#else
            const int kGetLk = F_GETLK;
#endif
            if (::fcntl(fd, kGetLk, &q) == 0 && q.l_type != F_UNLCK) {
                holder = static_cast<long>(q.l_pid);
            }
            std::fprintf(stderr,
                "openads: byte lock wait timed out (offset=%llu len=%llu "
                "holder_pid=%ld self_pid=%ld)\n",
                static_cast<unsigned long long>(offset),
                static_cast<unsigned long long>(length), holder,
                static_cast<long>(::getpid()));
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

util::Result<void> ByteLock::release() {
    release_();
    return {};
}

} // namespace openads::platform

#endif // !_WIN32
