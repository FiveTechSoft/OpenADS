#ifdef _WIN32

#include "platform/lock.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace openads::platform {

namespace {

util::Error os_error(const char* op) {
    DWORD code = ::GetLastError();
    util::Error e;
    e.code     = (code == ERROR_LOCK_VIOLATION) ? 5012 : 5013;
    e.sub_code = static_cast<std::int32_t>(code);
    e.message  = op;
    return e;
}

util::Result<ByteLock> do_lock(File& f, std::uint64_t offset,
                               std::uint64_t length, LockKind kind,
                               DWORD flags) {
    OVERLAPPED ov{};
    ov.Offset     = static_cast<DWORD>(offset & 0xFFFFFFFFu);
    ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD lo      = static_cast<DWORD>(length & 0xFFFFFFFFu);
    DWORD hi      = static_cast<DWORD>(length >> 32);
    DWORD effective_flags = flags;
    if (kind == LockKind::Exclusive) effective_flags |= LOCKFILE_EXCLUSIVE_LOCK;
    HANDLE h = reinterpret_cast<HANDLE>(f.native_handle());
    if (!::LockFileEx(h, effective_flags, 0, lo, hi, &ov)) {
        return os_error("LockFileEx");
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
    OVERLAPPED ov{};
    ov.Offset     = static_cast<DWORD>(offset_ & 0xFFFFFFFFu);
    ov.OffsetHigh = static_cast<DWORD>(offset_ >> 32);
    DWORD lo      = static_cast<DWORD>(length_ & 0xFFFFFFFFu);
    DWORD hi      = static_cast<DWORD>(length_ >> 32);
    ::UnlockFileEx(reinterpret_cast<HANDLE>(native_), 0, lo, hi, &ov);
    native_ = nullptr;
}

util::Result<ByteLock> ByteLock::acquire(File& f, std::uint64_t offset,
                                         std::uint64_t length, LockKind kind) {
    return do_lock(f, offset, length, kind, 0);
}

util::Result<ByteLock> ByteLock::try_acquire(File& f, std::uint64_t offset,
                                             std::uint64_t length,
                                             LockKind kind) {
    return do_lock(f, offset, length, kind, LOCKFILE_FAIL_IMMEDIATELY);
}

util::Result<bool> ByteLock::probe(File& f, std::uint64_t offset,
                                   std::uint64_t length) {
    // Win32 has no query API: a non-blocking take-and-release is the only
    // conflict probe. A same-handle overlap also fails with
    // ERROR_LOCK_VIOLATION, so callers must exclude their own
    // registrations before probing.
    OVERLAPPED ov{};
    ov.Offset     = static_cast<DWORD>(offset & 0xFFFFFFFFu);
    ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
    DWORD lo = static_cast<DWORD>(length & 0xFFFFFFFFu);
    DWORD hi = static_cast<DWORD>(length >> 32);
    HANDLE h = reinterpret_cast<HANDLE>(f.native_handle());
    if (::LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                     0, lo, hi, &ov)) {
        ::UnlockFileEx(h, 0, lo, hi, &ov);
        return false;
    }
    if (::GetLastError() == ERROR_LOCK_VIOLATION) return true;
    return os_error("LockFileEx probe");
}

util::Result<void> ByteLock::release() {
    release_();
    return {};
}

} // namespace openads::platform

#endif // _WIN32
