#include "engine/lock_mgr.h"

namespace openads::engine {

namespace {

// Harbour DBFNTX default locking scheme (DB_DBFLOCK_CLIPPER — hbrdddbf.h
// DBF_LOCKPOS_CLIPPER / hb_dbfRawLock): the DBF header/append lock is
// 1 byte at 1e9, a record lock on recno N is 1 byte at 1e9+N, and FLock
// is the range [1e9+1, 1e9+1+1e9) which deliberately covers every
// record-lock byte but not the header-lock byte.
constexpr std::uint64_t HB_FLOCK_BASE = 1'000'000'001ULL;
constexpr std::uint64_t HB_FLOCK_LEN  = 1'000'000'000ULL;
constexpr std::uint64_t HB_REC_BASE   = 1'000'000'000ULL;
// Harbour DBFCDX defaults to DB_DBFLOCK_VFP (dbfcdx1.c hb_cdxOpen line
// 7179 — NOT Clipper): the header/append lock is 1 byte at 0x40000000,
// a record lock mirrors the record's PHYSICAL position above the 1 GiB
// mark (0x40000000 + header_len + (recno-1)*record_len), and FLock is
// [0x40000001, +0x3ffffffd) covering every record-lock byte.
// OpenADS uses these exact offsets so a mixed fleet — HbDBU / DBFCDX
// apps, local ace64 clients and openads_serverd — serialises on the
// same bytes instead of corrupting shared files (Pritpal Bedi:
// "If ADS and DBF both are connected together indexes go corrupt").
constexpr std::uint64_t VFP_HDRLOCK    = 0x40000000ULL;
constexpr std::uint64_t VFP_FLOCK_BASE = 0x40000001ULL;
constexpr std::uint64_t VFP_FLOCK_LEN  = 0x3FFFFFFDULL;
constexpr std::uint64_t ADT_FILE_BASE  = 0x8000000000000000ULL;
constexpr std::uint64_t ADT_FILE_LEN   = 0x10000ULL;

} // namespace

std::uint64_t LockMgr::file_lock_offset(TableTypeForLock t, LockingMode m) {
    (void)m;
    switch (t) {
        case TableTypeForLock::Ntx: return HB_FLOCK_BASE;
        case TableTypeForLock::Cdx: return VFP_FLOCK_BASE;
        case TableTypeForLock::Vfp: return VFP_FLOCK_BASE;
        case TableTypeForLock::Adt: return ADT_FILE_BASE;
    }
    return VFP_FLOCK_BASE;
}

std::uint64_t LockMgr::file_lock_length(TableTypeForLock t) {
    switch (t) {
        case TableTypeForLock::Ntx: return HB_FLOCK_LEN;
        case TableTypeForLock::Cdx: return VFP_FLOCK_LEN;
        case TableTypeForLock::Vfp: return VFP_FLOCK_LEN;
        case TableTypeForLock::Adt: return ADT_FILE_LEN;
    }
    return 1ULL;
}

std::uint64_t LockMgr::record_lock_offset(TableTypeForLock t, LockingMode m,
                                          std::uint32_t recno) const {
    (void)m;
    switch (t) {
        case TableTypeForLock::Ntx:
            return HB_REC_BASE + recno;
        case TableTypeForLock::Cdx:
        case TableTypeForLock::Vfp:
            return VFP_HDRLOCK + rec_hdr_len_ +
                   static_cast<std::uint64_t>(recno > 0 ? recno - 1 : 0) *
                       rec_len_;
        case TableTypeForLock::Adt:
            return ADT_FILE_BASE +
                   (static_cast<std::uint64_t>(recno) << 16);
    }
    return HB_REC_BASE + recno;
}

util::Result<LockHandle>
LockMgr::lock_table_excl(platform::File& f, TableTypeForLock t, LockingMode m) {
    std::uint64_t off = file_lock_offset(t, m);
    std::uint64_t len = file_lock_length(t);
    Key k{&f, off};
    auto it = held_.find(k);
    if (it != held_.end()) {
        ++it->second;
        return LockHandle{platform::ByteLock{}, off, len};
    }
    auto bl = platform::ByteLock::acquire(f, off, len, platform::LockKind::Exclusive);
    if (!bl) return bl.error();
    held_[k] = 1;
    return LockHandle{std::move(bl).value(), off, len};
}

util::Result<LockHandle>
LockMgr::lock_record_excl(platform::File& f, TableTypeForLock t, LockingMode m,
                          std::uint32_t recno) {
    std::uint64_t off = record_lock_offset(t, m, recno);
    Key k{&f, off};
    auto it = held_.find(k);
    if (it != held_.end()) {
        ++it->second;
        return LockHandle{platform::ByteLock{}, off, 1};
    }
    auto bl = platform::ByteLock::acquire(f, off, 1, platform::LockKind::Exclusive);
    if (!bl) return bl.error();
    held_[k] = 1;
    return LockHandle{std::move(bl).value(), off, 1};
}

util::Result<LockHandle>
LockMgr::lock_record_shared(platform::File& f, TableTypeForLock t, LockingMode m,
                            std::uint32_t recno) {
    std::uint64_t off = record_lock_offset(t, m, recno);
    Key k{&f, off};
    auto it = held_.find(k);
    if (it != held_.end()) {
        ++it->second;
        return LockHandle{platform::ByteLock{}, off, 1};
    }
    auto bl = platform::ByteLock::acquire(f, off, 1, platform::LockKind::Shared);
    if (!bl) return bl.error();
    held_[k] = 1;
    return LockHandle{std::move(bl).value(), off, 1};
}

util::Result<LockHandle>
LockMgr::try_lock_table_excl(platform::File& f, TableTypeForLock t, LockingMode m) {
    std::uint64_t off = file_lock_offset(t, m);
    std::uint64_t len = file_lock_length(t);
    Key k{&f, off};
    auto it = held_.find(k);
    if (it != held_.end()) {
        ++it->second;
        return LockHandle{platform::ByteLock{}, off, len};
    }
    auto bl = platform::ByteLock::try_acquire(f, off, len,
                                              platform::LockKind::Exclusive);
    if (!bl) return bl.error();
    held_[k] = 1;
    return LockHandle{std::move(bl).value(), off, len};
}

util::Result<LockHandle>
LockMgr::try_lock_record_excl(platform::File& f, TableTypeForLock t, LockingMode m,
                              std::uint32_t recno) {
    std::uint64_t off = record_lock_offset(t, m, recno);
    Key k{&f, off};
    auto it = held_.find(k);
    if (it != held_.end()) {
        ++it->second;
        return LockHandle{platform::ByteLock{}, off, 1};
    }
    auto bl = platform::ByteLock::try_acquire(f, off, 1,
                                              platform::LockKind::Exclusive);
    if (!bl) return bl.error();
    held_[k] = 1;
    return LockHandle{std::move(bl).value(), off, 1};
}

bool LockMgr::unlock_table(platform::File& f, TableTypeForLock t, LockingMode m) {
    Key k{&f, file_lock_offset(t, m)};
    auto it = held_.find(k);
    if (it == held_.end()) return false;
    if (--it->second <= 0) {
        held_.erase(it);
        return true;
    }
    return false;
}

bool LockMgr::unlock_record(platform::File& f, TableTypeForLock t, LockingMode m,
                            std::uint32_t recno) {
    Key k{&f, record_lock_offset(t, m, recno)};
    auto it = held_.find(k);
    if (it == held_.end()) return false;
    if (--it->second <= 0) {
        held_.erase(it);
        return true;
    }
    return false;
}

bool LockMgr::force_unlock_record(platform::File& f, TableTypeForLock t,
                                  LockingMode m, std::uint32_t recno) {
    Key k{&f, record_lock_offset(t, m, recno)};
    return held_.erase(k) > 0;
}

} // namespace openads::engine
