#pragma once

#include "platform/file.h"
#include "util/result.h"

#include <cstdint>

namespace openads::platform {

enum class LockKind { Shared, Exclusive };

class ByteLock {
public:
    ByteLock() = default;
    ByteLock(const ByteLock&) = delete;
    ByteLock& operator=(const ByteLock&) = delete;
    ByteLock(ByteLock&&) noexcept;
    ByteLock& operator=(ByteLock&&) noexcept;
    ~ByteLock();

    static util::Result<ByteLock> acquire    (File& f, std::uint64_t offset,
                                              std::uint64_t length,
                                              LockKind kind);
    static util::Result<ByteLock> try_acquire(File& f, std::uint64_t offset,
                                              std::uint64_t length,
                                              LockKind kind);

    // Non-destructive conflict query (mtfix11): true when any byte of the
    // range is locked by ANOTHER owner (another handle/OFD/process).
    // Never takes or removes a lock. Callers must consult their own lock
    // registrations first: POSIX OFD queries cannot see the querying fd's
    // own locks, and a Win32 same-handle overlap also reports a conflict -
    // neither layer can distinguish "mine".
    static util::Result<bool> probe(File& f, std::uint64_t offset,
                                    std::uint64_t length);

    util::Result<void> release();

    // Internal: construct from a native handle and the locked range.
    // Public so the win32 / posix implementations can construct it from
    // their own translation units without friend declarations. Treat as
    // an implementation detail; callers should always go through
    // `acquire` / `try_acquire`.
    ByteLock(void* native, std::uint64_t off, std::uint64_t len) noexcept
        : native_(native), offset_(off), length_(len) {}

private:
    void release_() noexcept;

    void*         native_ = nullptr;
    std::uint64_t offset_ = 0;
    std::uint64_t length_ = 0;
};

} // namespace openads::platform
