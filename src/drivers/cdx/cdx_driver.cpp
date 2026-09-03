#include "drivers/cdx/cdx_driver.h"

#include "platform/lock.h"
#include "platform/time.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <thread>
#include <vector>

namespace openads::drivers::cdx {

namespace {

platform::OpenMode map_mode(DriverOpenMode m) {
    switch (m) {
        case DriverOpenMode::ReadOnly:  return platform::OpenMode::ReadOnly;
        case DriverOpenMode::Shared:    return platform::OpenMode::OpenExisting;
        case DriverOpenMode::Exclusive: return platform::OpenMode::OpenExisting;
    }
    return platform::OpenMode::ReadOnly;
}

// Cross-connection / cross-process append serialisation. Each
// CdxDriver instance caches `rec_count_` from the DBF header at
// open() time. When two writers (separate connections in the same
// process, or independent processes) append concurrently they will
// each compute `recno = rec_count_ + 1` from their own stale cache,
// overwrite each other's record, and produce a header whose
// "record count" lags reality. Holding an exclusive byte-lock on
// the header serialises all appenders against the authoritative
// state on disk.
//
// The locked byte is Harbour's DB_DBFLOCK_VFP header/append lock
// position (DBF_LOCKPOS_VFP = 0x40000000, 1 byte — hb_dbfRawLock
// APPEND_LOCK/HEADER_LOCK; DBFCDX defaults to the VFP scheme, see
// hb_cdxOpen), NOT the header bytes themselves: Harbour DBFCDX peers
// (HbDBU, B_BIG-style DBF apps) take the same byte, so OpenADS and
// Harbour appends serialise against each other instead of tearing the
// header (Pritpal Bedi: mixed ADS/DBFCDX corruption). The byte sits past
// EOF and is never written; locking it does not disturb the file
// contents.
//
// `acquire_with_retry_` retries with a short back-off so well-
// behaved contention does not spuriously fail; the caller still
// times out after ~10 s of solid contention. Linux + macOS use
// fcntl(F_SETLK), Windows uses LockFileEx — both honour
// inter-process semantics, so this also fixes the cross-process
// case (multiple openads_serverd / openads_concurrency_stress
// invocations against the same DBF).
constexpr std::uint64_t kHeaderLockOff = 0x40000000ULL;
constexpr std::uint64_t kHeaderLockLen = 1ULL;

util::Result<platform::ByteLock>
acquire_with_retry_(platform::File&      f,
                    std::uint64_t        offset,
                    std::uint64_t        length,
                    platform::LockKind   kind = platform::LockKind::Exclusive,
                    int                  max_retries = 2000)
{
    util::Error last_err{};
    for (int i = 0; i < max_retries; ++i) {
        auto lk = platform::ByteLock::try_acquire(f, offset, length, kind);
        if (lk) return std::move(lk).value();
        last_err = lk.error();
        // Cap the back-off at 5 ms: the default 2000 retries then bound
        // the total wait to ~10 s of solid contention (Harbour peers
        // lock with FLX_WAIT and never give up).
        const auto us = 50 + (i * 25);
        std::this_thread::sleep_for(std::chrono::microseconds(
            us > 5000 ? 5000 : us));
    }
    return last_err;
}

// Serializes in-process DBF header-lock traffic (append EXCLUSIVE and
// open SHARED). See append_record_raw / CdxDriver::open.
static std::mutex g_dbf_append_mu;

} // namespace

util::Result<void>
CdxDriver::open(const std::string& path, DriverOpenMode mode) {
    mode_ = mode;
    // A concurrent AdsCreateTable holds the freshly-created file with an
    // exclusive (share=0) handle for the few ms it writes the header;
    // CreateFile then fails with ERROR_SHARING_VIOLATION (sub_code 32).
    // Retry past that window instead of failing the open — the app-level
    // dance "if (!exists) create; open" would otherwise surface a
    // transient error exactly when the race it guards against happens.
    auto fres = platform::File::open(path, map_mode(mode));
    for (int attempt = 0;
         !fres && fres.error().sub_code == 32 /* ERROR_SHARING_VIOLATION */
             && attempt < 200;
         ++attempt) {
        const auto us = 50 + (attempt * 25);
        std::this_thread::sleep_for(std::chrono::microseconds(
            us > 5000 ? 5000 : us));
        fres = platform::File::open(path, map_mode(mode));
    }
    if (!fres) return fres.error();
    file_ = std::move(fres).value();

    // POSIX counterpart of the share=0 create window above: hold a
    // flock(LOCK_SH) for the whole lifetime of this open. A concurrent
    // AdsCreateTable takes flock(LOCK_EX|LOCK_NB) (OpenMode::
    // CreateExclusive), so it now fails with EWOULDBLOCK — surfaced as
    // 7040 — instead of ftruncate()ing the live table underneath us;
    // and while a create holds its EX lock for the ms-long header write,
    // our LOCK_SH simply contends, so retry past it (matching the Win32
    // sharing-violation retry) rather than erroring the open. No-op on
    // Win32. SH locks of concurrent openers do not conflict.
    for (int attempt = 0;; ++attempt) {
        auto sl = file_.try_lock_shared();
        if (sl) break;
        if (attempt >= 200) {
            file_ = platform::File{};
            return sl.error();
        }
        const auto us = 50 + (attempt * 25);
        std::this_thread::sleep_for(std::chrono::microseconds(
            us > 5000 ? 5000 : us));
    }

    // Coordinate the header read with concurrent appenders. An append
    // holds an EXCLUSIVE byte-lock on the header lock position while it
    // bumps the record count.
    //
    // Storm fix (RCB 2026-09-02): do NOT take that lock here. With ~700
    // in-process append threads the exclusive stream on this byte is
    // continuous and Win32 LockFileEx has no fairness — a SHARED
    // requester starved behind the endless EX hand-offs (observed:
    // OpenIndex avg 8-14 s, 80% of server time; routing the SHARED
    // acquire through the appender gate instead serialized every open
    // behind the whole append queue and made the storm worse). The
    // unlocked read is safe in practice: the 32-byte header rewrite
    // lands inside a single sector, so readers see either the old or
    // the new count, and both are consistent with a table whose append
    // body was written before the count. Later operations re-read the
    // count under the append lock (refresh_record_count_) anyway.
    std::uint8_t hdr_buf[32]{};
    auto got = file_.read_at(0, hdr_buf, sizeof(hdr_buf));
    if (!got) return got.error();
    if (got.value() < 32) {
        return util::Error{5103, 0, "DBF header truncated", path};
    }

    auto hdr = parse_dbf_header(hdr_buf, sizeof(hdr_buf));
    if (!hdr) return hdr.error();
    rec_count_ = hdr.value().record_count;
    rec_len_   = hdr.value().record_length;
    hdr_len_   = hdr.value().header_length;
    encrypted_      = hdr.value().encrypted;
    header_version_ = hdr.value().version;

    if (hdr_len_ < 33) {
        return util::Error{5103, 0, "DBF header length below 33 bytes", path};
    }
    std::size_t fd_size = hdr_len_ - 32;
    std::vector<std::uint8_t> fd_buf(fd_size, 0);
    auto fd_got = file_.read_at(32, fd_buf.data(), fd_buf.size());
    if (!fd_got) return fd_got.error();
    if (fd_got.value() < fd_buf.size()) {
        return util::Error{5103, 0, "field-descriptor block truncated", path};
    }
    auto fields = parse_dbf_fields(fd_buf.data(), fd_buf.size(), hdr_buf[0]);
    if (!fields) return fields.error();
    fields_ = std::move(fields).value();
    partial_enc_ = (hdr_buf[12] == 0x4F);
    enc_bitmap_offset_ = static_cast<std::uint32_t>(hdr_buf[28]) |
                         (static_cast<std::uint32_t>(hdr_buf[29]) << 8) |
                         (static_cast<std::uint32_t>(hdr_buf[30]) << 16) |
                         (static_cast<std::uint32_t>(hdr_buf[31]) << 24);
    if (partial_enc_ && enc_bitmap_offset_ != 0) {
        if (auto lr = load_enc_bitmap_(); !lr) return lr.error();
    }
    return {};
}

util::Result<void> CdxDriver::refresh_count_shared_() {
    // A peer append holds the header EXCLUSIVE while it bumps the count,
    // so take a SHARED lock (mirrors open()) to read a consistent value,
    // then release at end of scope. Modest retry budget keeps the fetch
    // responsive; if the lock can't be taken we still attempt an unlocked
    // refresh so the common single-writer case is never made worse.
    auto lk = acquire_with_retry_(file_, kHeaderLockOff, kHeaderLockLen,
                                  platform::LockKind::Shared, 40);
    (void)lk;  // hold the shared lock (if acquired) across the refresh
    return refresh_record_count_();
}

util::Result<std::vector<std::uint8_t>>
CdxDriver::read_record_raw(std::uint32_t recno) {
    if (recno == 0) {
        return util::Error{5000, 0, "record number out of range", ""};
    }
    if (recno > rec_count_) {
        // Cached count may lag a peer's append (multiuser). Re-read the
        // on-disk count before failing — native ADSCDX tolerates this,
        // and an index walk can legitimately reach a just-appended row
        // mid-REPLACE/DBEVAL. Slow path only: a forward scan never reads
        // past the count, so the common case pays nothing.
        if (auto rh = refresh_count_shared_(); !rh) return rh.error();
        if (recno > rec_count_) {
            return util::Error{5000, 0, "record number out of range", ""};
        }
    }
    std::vector<std::uint8_t> buf(rec_len_, 0);

    // Fast path: the record is already in the read-ahead block -> serve
    // it with a memcpy, no syscall. The block holds raw (encrypted-on-
    // disk) bytes; apply the recno-keyed CTR stream on the copy we return.
    if (read_cache_first_ != 0 &&
        recno >= read_cache_first_ &&
        recno <  read_cache_first_ + read_cache_recs_) {
        std::size_t pos = static_cast<std::size_t>(recno - read_cache_first_) *
                          rec_len_;
        std::memcpy(buf.data(), read_cache_.data() + pos, rec_len_);
        if (aes_ && (encrypted_ || record_encrypted_(recno))) {
            apply_ctr_(buf.data(), buf.size(), recno);
        }
        return buf;
    }

    // Miss: fetch the ALIGNED block that contains recno. Aligning (rather
    // than starting at recno) keeps backward scans and local random reads
    // hitting the cache too, and bounds a record to exactly one block.
    std::uint32_t blk_recs = rec_len_ != 0
        ? static_cast<std::uint32_t>(kReadAheadBytes / rec_len_)
        : 1u;
    if (blk_recs == 0) blk_recs = 1;
    std::uint32_t first = ((recno - 1) / blk_recs) * blk_recs + 1;
    std::uint32_t last  = first + blk_recs - 1;
    if (last > rec_count_) last = rec_count_;
    std::uint32_t nrecs = last - first + 1;

    std::uint64_t offset = static_cast<std::uint64_t>(hdr_len_) +
                           static_cast<std::uint64_t>(first - 1) *
                           static_cast<std::uint64_t>(rec_len_);
    std::size_t block_bytes = static_cast<std::size_t>(nrecs) * rec_len_;
    read_cache_.assign(block_bytes, 0);
    auto got = file_.read_at(offset, read_cache_.data(), block_bytes);
    if (!got) { invalidate_read_cache_(); return got.error(); }

    // A short read still yields whole records up to what landed; the
    // target recno is the first record of the block, so any non-empty
    // read covers it. Keep only complete records in the cache window.
    std::uint32_t got_recs = rec_len_ != 0
        ? static_cast<std::uint32_t>(got.value() / rec_len_)
        : 0u;
    if (got_recs == 0 || recno >= first + got_recs) {
        invalidate_read_cache_();
        return util::Error{5000, 0, "short read on record body", ""};
    }
    read_cache_first_ = first;
    read_cache_recs_  = got_recs;

    std::size_t pos = static_cast<std::size_t>(recno - first) * rec_len_;
    std::memcpy(buf.data(), read_cache_.data() + pos, rec_len_);
    if (aes_ && (encrypted_ || record_encrypted_(recno))) {
        apply_ctr_(buf.data(), buf.size(), recno);
    }
    return buf;
}

util::Result<void>
CdxDriver::write_record_raw(std::uint32_t recno,
                            const std::uint8_t* buf, std::size_t n) {
    if (mode_ == DriverOpenMode::ReadOnly) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    invalidate_read_cache_();   // record body about to change on disk
    if (recno == 0) {
        return util::Error{5000, 0, "record number out of range", ""};
    }
    if (recno > rec_count_) {
        // Same peer-append catch-up as read_record_raw: a REPLACE of a
        // row a peer just appended must not fail with a stale-count 5000.
        if (auto rh = refresh_count_shared_(); !rh) return rh.error();
        if (recno > rec_count_) {
            return util::Error{5000, 0, "record number out of range", ""};
        }
    }
    if (n != rec_len_) {
        return util::Error{5000, 0, "record buffer length mismatch", ""};
    }
    std::uint64_t offset = static_cast<std::uint64_t>(hdr_len_) +
                           static_cast<std::uint64_t>(recno - 1) *
                           static_cast<std::uint64_t>(rec_len_);
    if (aes_ && (encrypted_ || record_encrypted_(recno))) {
        std::vector<std::uint8_t> tmp(buf, buf + n);
        apply_ctr_(tmp.data(), tmp.size(), recno);
        auto wrote = file_.write_at(offset, tmp.data(), tmp.size());
        if (!wrote) return wrote.error();
        if (wrote.value() != n) {
            return util::Error{5000, 0, "short write on record body", ""};
        }
        return {};
    }
    auto wrote = file_.write_at(offset, buf, n);
    if (!wrote) return wrote.error();
    if (wrote.value() != n) {
        return util::Error{5000, 0, "short write on record body", ""};
    }
    return {};
}

util::Result<std::uint32_t>
CdxDriver::append_record_raw(const std::uint8_t* buf, std::size_t n) {
    if (mode_ == DriverOpenMode::ReadOnly) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    invalidate_read_cache_();   // rec_count_ / trailing block change
    if (n != rec_len_) {
        return util::Error{5000, 0, "record buffer length mismatch", ""};
    }
    // Blocking wait, not acquire_with_retry_ (10 s then fail). B_BIG
    // N=700 has ~7000 threads in append_record_raw; the 10 s spin-timeout
    // dropped waiters so the storm stalled ~17k/70k. The header lock is
    // one byte past EOF and is released as soon as the count is rewritten,
    // so a kernel wait is a fair FIFO and does not interact with Browse
    // rec-locks (those sit at 0x40000000+hdr+(recno-1)*rlen).
    //
    // Storm fix (RCB 2026-09-02): 7000 in-process threads each blocked in
    // the kernel LockFileEx wait queue on the SAME byte region. Windows
    // keeps a waiter IRP per blocked thread and every release walks that
    // queue, so the append cost grew with the waiter count — the 700-run
    // collapsed to ~5-15 recs/s while the 200-run did ~200 recs/s, and
    // throughput recovered only as clients exited (waiter queue shrank).
    // Gate the in-process stampede on a cheap userspace mutex first: only
    // ONE thread at a time ever enters the kernel wait, so the OS queue
    // holds a single IRP. Cross-process peers still contend the OS byte
    // normally. One global mutex (not per-file): the critical section is
    // a handful of page-cached writes (<1 ms), so even unrelated tables
    // pay a negligible serialization compared to the kernel-queue storm.
    std::lock_guard<std::mutex> append_gate(g_dbf_append_mu);
    auto lk = platform::ByteLock::acquire(
        file_, kHeaderLockOff, kHeaderLockLen, platform::LockKind::Exclusive);
    if (!lk) return lk.error();
    if (auto rh = refresh_record_count_(); !rh) return rh.error();
    std::uint32_t new_recno = rec_count_ + 1;
    std::uint64_t offset = static_cast<std::uint64_t>(hdr_len_) +
                           static_cast<std::uint64_t>(rec_count_) *
                           static_cast<std::uint64_t>(rec_len_);
    if (encrypted_ && aes_) {
        std::vector<std::uint8_t> tmp(buf, buf + n);
        apply_ctr_(tmp.data(), tmp.size(), new_recno);
        auto wrote = file_.write_at(offset, tmp.data(), tmp.size());
        if (!wrote) return wrote.error();
        if (wrote.value() != n) {
            return util::Error{5000, 0, "short write on record body", ""};
        }
    } else {
        auto wrote = file_.write_at(offset, buf, n);
        if (!wrote) return wrote.error();
        if (wrote.value() != n) {
            return util::Error{5000, 0, "short write on record body", ""};
        }
    }
    std::uint8_t eof = 0x1A;
    file_.write_at(offset + n, &eof, 1);

    rec_count_ = new_recno;
    if (auto r = rewrite_header_(); !r) return r.error();
    return new_recno;
}

util::Result<void> CdxDriver::refresh_record_count_() {
    std::uint8_t hdr_buf[8]{};
    auto got = file_.read_at(0, hdr_buf, sizeof(hdr_buf));
    if (!got) return got.error();
    if (got.value() < 8) {
        return util::Error{5103, 0,
            "DBF header truncated during refresh", ""};
    }
    rec_count_ =  static_cast<std::uint32_t>(hdr_buf[4])        |
                 (static_cast<std::uint32_t>(hdr_buf[5]) <<  8) |
                 (static_cast<std::uint32_t>(hdr_buf[6]) << 16) |
                 (static_cast<std::uint32_t>(hdr_buf[7]) << 24);
    // Harbour DBFCDX derives the record count from the FILE SIZE
    // (hb_dbfCalcRecCount) and flushes the header lazily — a peer's
    // just-appended record can be on disk while the header still lags.
    // Reading the header alone then hands the same recno to two writers
    // (one record overwritten, both keys in the index: 299 rows / 300
    // keys in the mixed stress test). Take the max of both views; zap()
    // truncates the file, so the size-derived count can never resurrect
    // zapped rows.
    if (!partial_enc_ && rec_len_ != 0) {
        // (a partially-encrypted table keeps a bitmap past the EOF record,
        // so its file size does not track the record count)
        if (auto sz = file_.size(); sz && sz.value() > hdr_len_) {
            const std::uint32_t size_count = static_cast<std::uint32_t>(
                (sz.value() - hdr_len_) / rec_len_);
            if (size_count > rec_count_) rec_count_ = size_count;
        }
    }
    return {};
}

util::Result<void> CdxDriver::rewrite_header_() {
    std::uint8_t hdr_buf[32]{};
    auto got = file_.read_at(0, hdr_buf, sizeof(hdr_buf));
    if (!got) return got.error();

    std::int64_t now = platform::utc_unix_micros();
    std::time_t  secs = static_cast<std::time_t>(now / 1'000'000);
    std::tm tm_utc{};
#ifdef _WIN32
    gmtime_s(&tm_utc, &secs);
#else
    gmtime_r(&secs, &tm_utc);
#endif
    hdr_buf[1] = static_cast<std::uint8_t>(tm_utc.tm_year);
    hdr_buf[2] = static_cast<std::uint8_t>(tm_utc.tm_mon + 1);
    hdr_buf[3] = static_cast<std::uint8_t>(tm_utc.tm_mday);

    hdr_buf[4] = static_cast<std::uint8_t>( rec_count_        & 0xFFu);
    hdr_buf[5] = static_cast<std::uint8_t>((rec_count_ >> 8)  & 0xFFu);
    hdr_buf[6] = static_cast<std::uint8_t>((rec_count_ >> 16) & 0xFFu);
    hdr_buf[7] = static_cast<std::uint8_t>((rec_count_ >> 24) & 0xFFu);

    auto wrote = file_.write_at(0, hdr_buf, sizeof(hdr_buf));
    if (!wrote) return wrote.error();
    if (wrote.value() != sizeof(hdr_buf)) {
        return util::Error{5000, 0, "short write on header", ""};
    }
    return {};
}

util::Result<void> CdxDriver::flush() {
    // Record bodies are already written via write_at / append_record_raw.
    // Harbour DBFCDX does not FlushFileBuffers on every commit; matching
    // that default is a multi-x win for ADSCDX bulk append + INDEX ON.
    // Opt into the old power-fail-safe path with OPENADS_FSYNC=1.
    static const bool fsync = [] {
        const char* e = std::getenv("OPENADS_FSYNC");
        return e && (e[0] == '1' || e[0] == 'y' || e[0] == 'Y' ||
                     e[0] == 't' || e[0] == 'T');
    }();
    if (!fsync) return {};
    return file_.sync();
}

util::Result<void> CdxDriver::zap() {
    if (mode_ == DriverOpenMode::ReadOnly) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    invalidate_read_cache_();
    rec_count_ = 0;
    if (auto r = rewrite_header_(); !r) return r.error();
    // Place an EOF marker right after the field-descriptor block and
    // TRUNCATE the file: refresh_record_count_() also derives the count
    // from the file size (Harbour's own convention), and stale record
    // bytes past the EOF would otherwise resurrect as phantom rows.
    std::uint8_t eof = 0x1A;
    if (auto w = file_.write_at(hdr_len_, &eof, 1); !w) return w.error();
    if (!partial_enc_) {
        // (a partially-encrypted table keeps its bitmap at EOF — never cut it)
        if (auto tr = file_.truncate(
                static_cast<std::uint64_t>(hdr_len_) + 1); !tr) {
            return tr.error();
        }
    }
    return file_.sync();
}

util::Result<bool> CdxDriver::truncate_trailing(std::uint32_t recno) {
    if (mode_ == DriverOpenMode::ReadOnly) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    if (recno == 0) return false;
    invalidate_read_cache_();
    // Hold the same header lock the append path uses, then re-read the
    // on-disk count so a concurrent append is observed.
    auto lk = acquire_with_retry_(file_, kHeaderLockOff, kHeaderLockLen);
    if (!lk) return lk.error();
    if (auto rh = refresh_record_count_(); !rh) return rh.error();
    if (recno != rec_count_) {
        // Another connection appended above this record — it is no
        // longer trailing, so it can't be popped. Caller soft-deletes.
        return false;
    }
    rec_count_ = recno - 1;
    if (auto r = rewrite_header_(); !r) return r.error();
    // EOF marker right after the last surviving record, and physically
    // shrink the file: refresh_record_count_() also derives the count
    // from the file size (Harbour's convention), so stale bytes past the
    // EOF would otherwise resurrect as phantom rows.
    std::uint64_t eof_off = static_cast<std::uint64_t>(hdr_len_) +
                            static_cast<std::uint64_t>(rec_count_) *
                            static_cast<std::uint64_t>(rec_len_);
    std::uint8_t eof = 0x1A;
    if (auto w = file_.write_at(eof_off, &eof, 1); !w) return w.error();
    // (a partially-encrypted table keeps its bitmap at EOF — never cut it)
    if (!partial_enc_) {
        if (auto tr = file_.truncate(eof_off + 1); !tr) return tr.error();
    }
    return true;
}

util::Result<bool> CdxDriver::truncate_to(std::uint32_t recno) {
    if (mode_ == DriverOpenMode::ReadOnly) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    invalidate_read_cache_();
    auto lk = acquire_with_retry_(file_, kHeaderLockOff, kHeaderLockLen);
    if (!lk) return lk.error();
    if (auto rh = refresh_record_count_(); !rh) return rh.error();
    if (recno > rec_count_) return false;   // can't grow; nothing trailing
    rec_count_ = recno;
    if (auto r = rewrite_header_(); !r) return r.error();
    std::uint64_t eof_off = static_cast<std::uint64_t>(hdr_len_) +
                            static_cast<std::uint64_t>(rec_count_) *
                            static_cast<std::uint64_t>(rec_len_);
    std::uint8_t eof = 0x1A;
    if (auto w = file_.write_at(eof_off, &eof, 1); !w) return w.error();
    // Physically shrink the file to match the new logical count (the old
    // zap + re-append PACK path did this implicitly). Without it the .dbf
    // keeps the stale trailing records on disk; a later physical-order
    // DBEVAL / reopen that derives the count from file size then walks past
    // rec_count_ -> ADSCDX/5000 "record number out of range".
    if (auto tr = file_.truncate(eof_off + 1); !tr) return tr.error();
    return true;
}

util::Result<std::uint32_t>
CdxDriver::bump_autoinc(std::uint16_t field_index) {
    if (field_index >= fields_.size()) {
        return util::Error{5063, 0, "field index out of range", ""};
    }
    auto& f = fields_[field_index];
    if (!f.autoinc) {
        return util::Error{5063, 0, "field is not autoinc", f.name};
    }
    std::uint32_t curr = f.autoinc_next;
    std::uint32_t next = curr +
        static_cast<std::uint32_t>(f.autoinc_step ? f.autoinc_step : 1);
    f.autoinc_next = next;

    // Field-descriptor block starts at file offset 32; this field's
    // descriptor lives at 32 + 32*field_index; the autoinc-next bytes
    // sit at offset 19 within the descriptor.
    std::uint64_t off = 32u +
                        static_cast<std::uint64_t>(field_index) * 32u +
                        19u;
    std::uint8_t buf[4] = {
        static_cast<std::uint8_t>( next        & 0xFFu),
        static_cast<std::uint8_t>((next >>  8) & 0xFFu),
        static_cast<std::uint8_t>((next >> 16) & 0xFFu),
        static_cast<std::uint8_t>((next >> 24) & 0xFFu),
    };
    auto w = file_.write_at(off, buf, sizeof(buf));
    if (!w) return w.error();
    return curr;
}

void CdxDriver::apply_ctr_(std::uint8_t* buf, std::size_t n,
                           std::uint32_t recno) const {
    // M11.2 — AES-256-CTR over the record body. IV layout:
    // bytes 0..3 = recno LE, bytes 4..15 = block counter (LE).
    // Identical for encrypt and decrypt — XOR is symmetric.
    if (!aes_) return;
    std::array<std::uint8_t, 16> ctr{};
    ctr[0] = static_cast<std::uint8_t>( recno        & 0xFFu);
    ctr[1] = static_cast<std::uint8_t>((recno >>  8) & 0xFFu);
    ctr[2] = static_cast<std::uint8_t>((recno >> 16) & 0xFFu);
    ctr[3] = static_cast<std::uint8_t>((recno >> 24) & 0xFFu);
    for (std::size_t i = 0; i < n; i += 16) {
        std::array<std::uint8_t, 16> ks = ctr;
        aes_->encrypt_block(ks.data());
        std::size_t blk = std::min<std::size_t>(16, n - i);
        for (std::size_t k = 0; k < blk; ++k) {
            buf[i + k] ^= ks[k];
        }
        // Increment 12-byte block counter at bytes 4..15 (LE).
        for (std::size_t b = 4; b < 16; ++b) {
            if (++ctr[b] != 0) break;
        }
    }
}

util::Result<void>
CdxDriver::set_encryption_key(const std::array<std::uint8_t, 32>& key) {
    auto a = engine::Aes::from_key(engine::AesKeyBits::Aes256,
                                   std::vector<std::uint8_t>(
                                       key.begin(), key.end()));
    if (!a) return a.error();
    aes_ = std::move(a).value();
    return {};
}

util::Result<void>
CdxDriver::encrypt_in_place(const std::array<std::uint8_t, 32>& key) {
    // M11.2 — convert a plain DBF to OpenADS-encrypted: re-read every
    // existing record (still plain), set encrypted state, write each
    // record back through the encryption hook, flip header byte to
    // 0xC3 so future opens detect the format.
    if (mode_ == DriverOpenMode::ReadOnly) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    if (encrypted_) {
        return util::Error{5000, 0, "table is already encrypted", ""};
    }
    invalidate_read_cache_();   // every record body is rewritten below
    std::vector<std::vector<std::uint8_t>> plain;
    plain.reserve(rec_count_);
    for (std::uint32_t r = 1; r <= rec_count_; ++r) {
        auto rd = read_record_raw(r);
        if (!rd) return rd.error();
        plain.push_back(std::move(rd).value());
    }
    if (auto r = set_encryption_key(key); !r) return r.error();
    encrypted_ = true;
    for (std::uint32_t r = 1; r <= rec_count_; ++r) {
        if (auto w = write_record_raw(r, plain[r - 1].data(),
                                       plain[r - 1].size()); !w) {
            return w.error();
        }
    }
    std::uint8_t v = 0xC4;  // PBKDF2-derived key (0xC3 = legacy zero-pad)
    auto wh = file_.write_at(0, &v, 1);
    header_version_ = v;
    if (!wh) return wh.error();
    return {};
}

bool CdxDriver::record_encrypted_(std::uint32_t recno) const noexcept {
    if (recno == 0) return false;
    const std::size_t bit = static_cast<std::size_t>(recno - 1);
    const std::size_t byte = bit / 8;
    const std::size_t mask = static_cast<std::size_t>(1u) << (bit % 8);
    return byte < enc_bitmap_.size() && (enc_bitmap_[byte] & mask) != 0;
}

bool CdxDriver::record_encrypted(std::uint32_t recno) const noexcept {
    return encrypted_ || record_encrypted_(recno);
}

void CdxDriver::set_record_encrypted_(std::uint32_t recno, bool on) {
    if (recno == 0) return;
    const std::size_t bit = static_cast<std::size_t>(recno - 1);
    const std::size_t byte = bit / 8;
    const std::uint8_t mask =
        static_cast<std::uint8_t>(static_cast<std::size_t>(1u) << (bit % 8));
    if (enc_bitmap_.size() <= byte) {
        enc_bitmap_.resize(byte + 1, 0);
    }
    if (on) enc_bitmap_[byte] |= mask;
    else    enc_bitmap_[byte] &= static_cast<std::uint8_t>(~mask);
}

util::Result<void> CdxDriver::load_enc_bitmap_() {
    if (enc_bitmap_offset_ == 0) return {};
    const std::size_t need =
        (static_cast<std::size_t>(rec_count_) + 7) / 8;
    enc_bitmap_.assign(need, 0);
    auto got = file_.read_at(enc_bitmap_offset_, enc_bitmap_.data(), need);
    if (!got) return got.error();
    if (got.value() < need) {
        return util::Error{5000, 0, "encryption bitmap truncated", ""};
    }
    return {};
}

util::Result<void> CdxDriver::save_enc_bitmap_() {
    if (!partial_enc_ || enc_bitmap_.empty()) return {};
    if (enc_bitmap_offset_ == 0) {
        auto sz = file_.size();
        if (!sz) return sz.error();
        enc_bitmap_offset_ = static_cast<std::uint32_t>(sz.value());
        if (enc_bitmap_offset_ > 0) {
            --enc_bitmap_offset_;  // insert before trailing 0x1A
        }
    }
    auto wrote = file_.write_at(enc_bitmap_offset_,
                                enc_bitmap_.data(), enc_bitmap_.size());
    if (!wrote) return wrote.error();
    if (wrote.value() != enc_bitmap_.size()) {
        return util::Error{5000, 0, "short write on encryption bitmap", ""};
    }
    std::uint8_t hdr[32]{};
    auto got = file_.read_at(0, hdr, sizeof(hdr));
    if (!got) return got.error();
    hdr[12] = 0x4F;
    hdr[28] = static_cast<std::uint8_t>( enc_bitmap_offset_        & 0xFFu);
    hdr[29] = static_cast<std::uint8_t>((enc_bitmap_offset_ >>  8) & 0xFFu);
    hdr[30] = static_cast<std::uint8_t>((enc_bitmap_offset_ >> 16) & 0xFFu);
    hdr[31] = static_cast<std::uint8_t>((enc_bitmap_offset_ >> 24) & 0xFFu);
    auto wh = file_.write_at(0, hdr, sizeof(hdr));
    if (!wh) return wh.error();
    return {};
}

util::Result<std::vector<std::uint8_t>>
CdxDriver::read_record_disk_(std::uint32_t recno) {
    if (recno == 0) {
        return util::Error{5000, 0, "record number out of range", ""};
    }
    if (recno > rec_count_) {
        if (auto rh = refresh_count_shared_(); !rh) return rh.error();
        if (recno > rec_count_) {
            return util::Error{5000, 0, "record number out of range", ""};
        }
    }
    std::vector<std::uint8_t> buf(rec_len_, 0);
    std::uint64_t offset = static_cast<std::uint64_t>(hdr_len_) +
                           static_cast<std::uint64_t>(recno - 1) *
                           static_cast<std::uint64_t>(rec_len_);
    auto got = file_.read_at(offset, buf.data(), rec_len_);
    if (!got) return got.error();
    if (got.value() < rec_len_) {
        return util::Error{5000, 0, "short read on record body", ""};
    }
    return buf;
}

util::Result<void> CdxDriver::encrypt_record_at(std::uint32_t recno) {
    if (mode_ == DriverOpenMode::ReadOnly) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    if (!aes_) {
        return util::Error{5000, 0, "encryption key not set", ""};
    }
    if (encrypted_) return {};
    if (recno == 0 || recno > rec_count_) {
        return util::Error{5000, 0, "record number out of range", ""};
    }
    if (record_encrypted_(recno)) return {};
    auto plain = read_record_disk_(recno);
    if (!plain) return plain.error();
    partial_enc_ = true;
    set_record_encrypted_(recno, true);
    invalidate_read_cache_();
    std::vector<std::uint8_t> enc = plain.value();
    apply_ctr_(enc.data(), enc.size(), recno);
    std::uint64_t offset = static_cast<std::uint64_t>(hdr_len_) +
                           static_cast<std::uint64_t>(recno - 1) *
                           static_cast<std::uint64_t>(rec_len_);
    auto wrote = file_.write_at(offset, enc.data(), enc.size());
    if (!wrote) return wrote.error();
    return save_enc_bitmap_();
}

util::Result<void> CdxDriver::decrypt_record_at(std::uint32_t recno) {
    if (mode_ == DriverOpenMode::ReadOnly) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    if (!aes_) {
        return util::Error{5000, 0, "encryption key not set", ""};
    }
    if (encrypted_) {
        return util::Error{5000, 0,
                           "decrypt single record not supported on fully "
                           "encrypted tables; use AdsDecryptTable",
                           ""};
    }
    if (recno == 0 || recno > rec_count_) {
        return util::Error{5000, 0, "record number out of range", ""};
    }
    if (!record_encrypted_(recno)) return {};
    auto enc = read_record_disk_(recno);
    if (!enc) return enc.error();
    set_record_encrypted_(recno, false);
    invalidate_read_cache_();
    std::vector<std::uint8_t> plain = enc.value();
    apply_ctr_(plain.data(), plain.size(), recno);
    std::uint64_t offset = static_cast<std::uint64_t>(hdr_len_) +
                           static_cast<std::uint64_t>(recno - 1) *
                           static_cast<std::uint64_t>(rec_len_);
    auto wrote = file_.write_at(offset, plain.data(), plain.size());
    if (!wrote) return wrote.error();
    return save_enc_bitmap_();
}

} // namespace openads::drivers::cdx
