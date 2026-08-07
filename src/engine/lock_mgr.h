#pragma once

#include "platform/file.h"
#include "platform/lock.h"
#include "util/result.h"

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace openads::engine {

enum class TableTypeForLock { Ntx, Cdx, Vfp, Adt };
enum class LockingMode      { Compatible, Proprietary };

class LockHandle {
public:
    LockHandle() = default;
    LockHandle(platform::ByteLock&& lk,
               std::uint64_t offset, std::uint64_t length) noexcept
        : lock_(std::move(lk)), offset_(offset), length_(length) {}

    LockHandle(LockHandle&&) noexcept = default;
    LockHandle& operator=(LockHandle&&) noexcept = default;

    std::uint64_t offset() const noexcept { return offset_; }
    std::uint64_t length() const noexcept { return length_; }

    void release() noexcept { lock_.release(); }

private:
    platform::ByteLock lock_;
    std::uint64_t      offset_ = 0;
    std::uint64_t      length_ = 0;
};

// Per-Table refcount map for nested OS byte-range locks. Each Table
// workarea owns one LockMgr; ABI access is serialized per connection,
// so held_ is not guarded by a mutex here.
class LockMgr {
public:
    util::Result<LockHandle>
        lock_table_excl (platform::File& f,
                         TableTypeForLock t, LockingMode m);

    util::Result<LockHandle>
        lock_record_excl(platform::File& f,
                         TableTypeForLock t, LockingMode m,
                         std::uint32_t recno);

    util::Result<LockHandle>
        lock_record_shared(platform::File& f,
                           TableTypeForLock t, LockingMode m,
                           std::uint32_t recno);

    // Non-blocking variants (M9.18). Return AE_LOCKED when the OS-level
    // byte-range lock is held by another owner; the caller (typically
    // the ABI retry loop) decides whether to back off and try again
    // instead of blocking on the kernel.
    util::Result<LockHandle>
        try_lock_table_excl(platform::File& f,
                            TableTypeForLock t, LockingMode m);

    util::Result<LockHandle>
        try_lock_record_excl(platform::File& f,
                             TableTypeForLock t, LockingMode m,
                             std::uint32_t recno);

    // Decrement the per-key refcount. Returns true when the refcount reached
    // zero so the caller should release the OS-level byte lock held in its
    // LockHandle; false when nested acquires remain and the OS lock must stay.
    bool unlock_table (platform::File& f, TableTypeForLock t, LockingMode m);
    bool unlock_record(platform::File& f, TableTypeForLock t, LockingMode m,
                       std::uint32_t recno);

    // Drop the per-key refcount entry regardless of nesting depth. Used by
    // the FLock-subsumes-RLocks path: the caller releases the OS byte lock
    // via its LockHandle and re-acquires from scratch later. Returns true
    // when an entry existed.
    bool force_unlock_record(platform::File& f, TableTypeForLock t,
                             LockingMode m, std::uint32_t recno);

    static std::uint64_t file_lock_offset(TableTypeForLock t, LockingMode m);
    static std::uint64_t file_lock_length(TableTypeForLock t);

    // VFP-scheme record locks mirror the record's physical position, so
    // the manager needs the table geometry. Set right after the driver
    // opens (Table::open / from_driver); zeroes degrade to a single
    // shared lock byte — over-serialising but never corrupting.
    void set_record_geometry(std::uint32_t header_len,
                             std::uint32_t record_len) noexcept {
        rec_hdr_len_ = header_len;
        rec_len_     = record_len;
    }

private:
    std::uint64_t record_lock_offset(TableTypeForLock t, LockingMode m,
                                     std::uint32_t recno) const;

    std::uint32_t rec_hdr_len_ = 0;
    std::uint32_t rec_len_     = 0;

    struct Key {
        const void*   file;
        std::uint64_t offset;
        bool operator==(const Key& o) const noexcept {
            return file == o.file && offset == o.offset;
        }
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept {
            // FNV-1a 64-bit — avoids XOR clustering on sequential offsets.
            std::uint64_t h = 1469598103934665603ULL;
            auto mix = [&](std::uint64_t v) {
                h ^= v;
                h *= 1099511628211ULL;
            };
            mix(static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(k.file)));
            mix(k.offset);
            return static_cast<std::size_t>(h);
        }
    };
    std::unordered_map<Key, int, KeyHash> held_;
};

} // namespace openads::engine
