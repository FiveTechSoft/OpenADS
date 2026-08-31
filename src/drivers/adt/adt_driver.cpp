#include "drivers/adt/adt_driver.h"

#include "platform/lock.h"
#include "platform/time.h"

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace openads::drivers::adt {

namespace {

static const char kAdtSignature[] = "Advantage Table";  // 15 chars, no NUL

platform::OpenMode map_mode(DriverOpenMode m) {
    switch (m) {
        case DriverOpenMode::ReadOnly:  return platform::OpenMode::ReadOnly;
        case DriverOpenMode::Shared:    return platform::OpenMode::OpenExisting;
        case DriverOpenMode::Exclusive: return platform::OpenMode::OpenExisting;
    }
    return platform::OpenMode::ReadOnly;
}

std::uint16_t read_u16_le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) |
           static_cast<std::uint16_t>(p[1] << 8);
}

std::uint32_t read_u32_le(const std::uint8_t* p) {
    return  static_cast<std::uint32_t>(p[0])        |
           (static_cast<std::uint32_t>(p[1]) <<  8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

// ADT field type code → DbfFieldType.
DbfFieldType classify_adt_field(std::uint16_t raw_type) {
    switch (raw_type) {
        case  1: return DbfFieldType::Logical;
        case  2: return DbfFieldType::Numeric;  // ASCII digits, right-aligned
        case  3: return DbfFieldType::AdtDate;
        case  4: return DbfFieldType::Character;
        case  5: return DbfFieldType::Memo;
        case  6: return DbfFieldType::Binary;
        case  7: return DbfFieldType::Binary;   // IMAGE — 9-byte .adm ref (Harbour 'P')
        case 10: return DbfFieldType::Double;
        case 11: return DbfFieldType::Integer;
        case 12: return DbfFieldType::ShortInt;
        case 13: return DbfFieldType::Time;
        case 14: return DbfFieldType::AdtTimestamp;
        case 15: return DbfFieldType::AutoInc;
        case 18: return DbfFieldType::AdtMoney;    // MONEY: 8-byte LE int64, value * 10000
        case 20: return DbfFieldType::CiCharacter;
        case 21: return DbfFieldType::RowVersion;  // 8-byte uint64 LE row version counter
        case 22: return DbfFieldType::ModTime;     // 8-byte modification timestamp
        default: return DbfFieldType::Unknown;
    }
}

// Retry-with-backoff lock helper, same pattern as CdxDriver. `kind` lets a
// reader take the header region shared, so open/refresh wait out an appender
// instead of failing with ERROR_LOCK_VIOLATION (mapped to 5000).
util::Result<platform::ByteLock>
acquire_with_retry_(platform::File&    f,
                    std::uint64_t      offset,
                    std::uint64_t      length,
                    platform::LockKind kind = platform::LockKind::Exclusive,
                    int                max_retries = 200)
{
    util::Error last_err{};
    for (int i = 0; i < max_retries; ++i) {
        auto lk = platform::ByteLock::try_acquire(f, offset, length, kind);
        if (lk) return std::move(lk).value();
        last_err = lk.error();
        std::this_thread::sleep_for(
            std::chrono::microseconds(50 + (i * 25)));
    }
    return last_err;
}

} // namespace

// ---------------------------------------------------------------------------
// open
// ---------------------------------------------------------------------------

util::Result<void>
AdtDriver::open(const std::string& path, DriverOpenMode mode) {
    mode_ = mode;
    auto fres = platform::File::open(path, map_mode(mode));
    if (!fres) return fres.error();
    file_ = std::move(fres).value();

    // POSIX counterpart of the share=0 create window: hold an
    // flock(LOCK_SH) for the lifetime of this open, so a concurrent
    // AdsCreateTable (OpenMode::CreateExclusive, flock LOCK_EX|NB)
    // fails instead of ftruncate()ing the live table underneath us.
    // While a create holds its EX lock for the header write, this SH
    // lock contends and retries past the ms-long window. No-op on
    // Win32 (share=0 already provides the exclusion).
    for (int attempt = 0;; ++attempt) {
        auto sl = file_.try_lock_shared();
        if (sl) break;
        if (attempt >= 200) {
            file_ = platform::File{};
            return sl.error();
        }
        std::this_thread::sleep_for(
            std::chrono::microseconds(50 + (attempt * 25)));
    }

    // Coordinate the header read with concurrent appenders (who hold 0..399
    // exclusive). Shared lock + retry so open does not fail with
    // ERROR_LOCK_VIOLATION while another connection appends.
    auto hdr_lock = acquire_with_retry_(file_, 0, 400,
                                        platform::LockKind::Shared);
    if (!hdr_lock) return hdr_lock.error();

    // Read the 400-byte ADT file header.
    std::uint8_t hdr[400]{};
    auto got = file_.read_at(0, hdr, sizeof(hdr));
    if (!got) return got.error();
    if (got.value() < 40) {
        return util::Error{5103, 0, "ADT header truncated", path};
    }
    if (std::memcmp(hdr, kAdtSignature, 15) != 0) {
        return util::Error{5103, 0, "not an ADT file (bad signature)", path};
    }
    rec_count_ = read_u32_le(hdr + 24);
    hdr_len_   = read_u32_le(hdr + 32);
    rec_len_   = read_u32_le(hdr + 36);

    if (hdr_len_ < 400 || (hdr_len_ - 400) % 200 != 0) {
        return util::Error{5103, 0, "ADT header_length invalid", path};
    }
    if (rec_len_ == 0) {
        return util::Error{5103, 0, "ADT record_length is zero", path};
    }

    // Defensive cap: a .DAT (ADT-format) file can carry a header rec_count_
    // larger than the data bytes actually present (truncation, crash, partial
    // copy). Cap it so every read stays inside the file instead of raising a
    // "short read" / "out of range" 5000.
    cap_record_count_from_size_();

    // Read all field descriptors (200 bytes each after the 400-byte header).
    std::uint32_t num_fields = (hdr_len_ - 400) / 200;
    std::vector<std::uint8_t> fd_buf(num_fields * 200, 0);
    if (!fd_buf.empty()) {
        auto fd_got = file_.read_at(400, fd_buf.data(), fd_buf.size());
        if (!fd_got) return fd_got.error();
        if (fd_got.value() < fd_buf.size()) {
            return util::Error{5103, 0,
                "ADT field-descriptor block truncated", path};
        }
    }

    fields_.reserve(num_fields);
    std::uint16_t null_bit = 0;
    for (std::uint32_t fi = 0; fi < num_fields; ++fi) {
        const std::uint8_t* fd = fd_buf.data() + fi * 200;
        DbfField f;
        // Name: null-terminated string in bytes 0-127.
        std::size_t name_len = 0;
        while (name_len < 128 && fd[name_len] != '\0') ++name_len;
        f.name.assign(reinterpret_cast<const char*>(fd), name_len);

        std::uint16_t raw_type = read_u16_le(fd + 129);
        f.raw_type      = static_cast<char>(raw_type & 0xFF);
        f.type          = classify_adt_field(raw_type);
        f.record_offset = read_u16_le(fd + 131);
        f.length        = read_u16_le(fd + 135);

        // Decimal count lives at byte 139, NOT 137.
        //
        // Verified against tables written by the real SAP engine (mp corpus,
        // service.adt): a type-2 numeric declared N(10,2) carries
        //     …135:10  136:0  137:0  138:0  139:2  140:0
        // i.e. 137 stays zero and the scale sits at 139. Reading 137 returned
        // 0 for every SAP-written table.
        //
        // This hid for a long time because ADT type 2 stores the value as
        // ASCII text: a plain read hands back the stored characters verbatim
        // ("      0.00") and never consults `decimals`. Only a path that
        // RE-FORMATS the value from as_double needs the scale — which is why
        // it first surfaced as joins rendering money columns as "0" while the
        // same column read correctly on its own.
        //
        // Fall back to 137 so ADT tables written by older OpenADS builds (which
        // stamped the scale there) still read back with their decimals.
        //
        // Restricted to type 2 on purpose: for an AUTOINC field (type 15)
        // bytes 139-143 are the auto-increment COUNTER, so reading 139 as a
        // scale there would turn a populated counter into nonsense decimals.
        // Type 2 is the only ADT type that carries a meaningful declared
        // scale — DOUBLE (10) stores none at all (stamping one makes the real
        // engine report the table corrupt, 7016) and MONEY (18) is implicitly
        // 4-digit.
        std::uint16_t dec = read_u16_le(fd + 137);
        if (raw_type == 2u) {
            const std::uint16_t dec_139 = read_u16_le(fd + 139);
            if (dec_139 != 0) dec = dec_139;
        }
        if (dec > 18u) dec = 0;   // guard against a stray value
        f.decimals = static_cast<std::uint8_t>(dec);

        // byte 128: flags (bit 1 = nullable, same convention as VFP).
        std::uint8_t flags = fd[128];
        f.nullable = (flags & 0x02u) != 0;
        if (f.nullable) f.null_bit = null_bit++;

        // M13 — ADT NULL is a per-type in-field sentinel, not a bitmap
        // (SAP leaves this flags byte 0 even though every non-autoinc ADT
        // field is nullable). Mark the field so decode_field and the
        // Table NULL paths apply the sentinel convention.
        f.adt = true;

        // AutoInc: autoinc_next at 139 (uint32 LE), step at 143.
        if (f.type == DbfFieldType::AutoInc) {
            f.autoinc      = true;
            f.autoinc_next = read_u32_le(fd + 139);
            f.autoinc_step = fd[143];
            if (f.autoinc_step == 0) f.autoinc_step = 1;
        }

        fields_.push_back(std::move(f));
    }

    // autoinc_next is not persisted in the field descriptor; seed from data.
    for (auto& fld : fields_) {
        if (!fld.autoinc || fld.autoinc_next != 0) continue;
        std::uint32_t max_val = 0;
        for (std::uint32_t rn = 1; rn <= rec_count_; ++rn) {
            auto raw = read_record_raw(rn);
            if (!raw) continue;
            const auto& rec = raw.value();
            const std::size_t off = fld.record_offset;
            if (off + 4 > rec.size()) continue;
            const std::uint32_t v = read_u32_le(rec.data() + off);
            if (v > max_val) max_val = v;
        }
        const std::uint32_t step = fld.autoinc_step ? fld.autoinc_step : 1u;
        fld.autoinc_next = max_val > 0 ? max_val + step : 1u;
    }

    return {};
}

// ---------------------------------------------------------------------------
// read / write / append
// ---------------------------------------------------------------------------

void AdtDriver::normalize_deletion_flag_(std::uint8_t* buf) noexcept {
    // ADT: 0x04 = active, 0x05 = deleted → DBF: ' ' = active, '*' = deleted
    buf[0] = (buf[0] == 0x05) ? static_cast<std::uint8_t>('*')
                               : static_cast<std::uint8_t>(' ');
}

void AdtDriver::denormalize_deletion_flag_(std::uint8_t* buf) noexcept {
    // DBF: '*' = deleted, anything else = active → ADT: 0x05 / 0x04
    buf[0] = (buf[0] == '*') ? 0x05u : 0x04u;
}

util::Result<std::vector<std::uint8_t>>
AdtDriver::read_record_raw(std::uint32_t recno) {
    if (recno == 0) {
        return util::Error{5000, 0, "record number out of range", ""};
    }
    if (recno > rec_count_) {
        // A peer may have appended; refresh (shared) + re-cap before failing.
        if (auto rh = refresh_record_count_shared_(); !rh) return rh.error();
        if (recno > rec_count_) {
            return util::Error{5000, 0, "record number out of range", ""};
        }
    }
    std::vector<std::uint8_t> buf(rec_len_, 0);

    // Fast path: the record is already in the read-ahead block -> serve it
    // with a memcpy, no syscall. The cache holds raw on-disk bytes; normalise
    // the deletion flag on the returned copy (mirrors the non-cached tail).
    if (read_cache_first_ != 0 &&
        recno >= read_cache_first_ &&
        recno <  read_cache_first_ + read_cache_recs_) {
        std::size_t pos = static_cast<std::size_t>(recno - read_cache_first_) *
                          rec_len_;
        std::memcpy(buf.data(), read_cache_.data() + pos, rec_len_);
        normalize_deletion_flag_(buf.data());
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

    // A short read still yields whole records up to what landed; the target
    // recno sits at or after `first`, so only a read that stops before it is
    // a real failure.
    std::uint32_t got_recs = rec_len_ != 0
        ? static_cast<std::uint32_t>(got.value() / rec_len_)
        : 0u;
    if (got_recs == 0 || recno >= first + got_recs) {
        // Re-check size in case a truncate raced; re-cap and re-test.
        invalidate_read_cache_();
        cap_record_count_from_size_();
        if (recno > rec_count_) {
            return util::Error{5000, 0, "record number out of range", ""};
        }
        return util::Error{5000, 0, "short read on ADT record body", ""};
    }
    read_cache_first_ = first;
    read_cache_recs_  = got_recs;

    std::size_t pos = static_cast<std::size_t>(recno - first) * rec_len_;
    std::memcpy(buf.data(), read_cache_.data() + pos, rec_len_);
    normalize_deletion_flag_(buf.data());
    return buf;
}

util::Result<void>
AdtDriver::write_record_raw(std::uint32_t recno,
                            const std::uint8_t* buf, std::size_t n) {
    if (mode_ == DriverOpenMode::ReadOnly) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    invalidate_read_cache_();   // record body about to change on disk
    if (recno == 0) {
        return util::Error{5000, 0, "record number out of range", ""};
    }
    if (recno > rec_count_) {
        if (auto rh = refresh_record_count_shared_(); !rh) return rh.error();
        if (recno > rec_count_) {
            return util::Error{5000, 0, "record number out of range", ""};
        }
    }
    if (n != rec_len_) {
        return util::Error{5000, 0, "record buffer length mismatch", ""};
    }
    std::vector<std::uint8_t> tmp(buf, buf + n);
    denormalize_deletion_flag_(tmp.data());
    // Zero bytes 1-4 (null bitmap) if they are all spaces from
    // make_empty_record; preserve intentional null-bitmap values.
    if (tmp[1] == 0x20 && tmp[2] == 0x20 && tmp[3] == 0x20 && tmp[4] == 0x20) {
        tmp[1] = tmp[2] = tmp[3] = tmp[4] = 0x00;
    }

    std::uint64_t offset = static_cast<std::uint64_t>(hdr_len_) +
                           static_cast<std::uint64_t>(recno - 1) *
                           static_cast<std::uint64_t>(rec_len_);
    auto wrote = file_.write_at(offset, tmp.data(), tmp.size());
    if (!wrote) return wrote.error();
    if (wrote.value() != n) {
        return util::Error{5000, 0, "short write on ADT record body", ""};
    }
    return {};
}

util::Result<std::uint32_t>
AdtDriver::append_record_raw(const std::uint8_t* buf, std::size_t n) {
    if (mode_ == DriverOpenMode::ReadOnly) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    invalidate_read_cache_();   // rec_count_ / trailing block change
    if (n != rec_len_) {
        return util::Error{5000, 0, "record buffer length mismatch", ""};
    }
    // Serialise appenders via an exclusive byte-lock on the header.
    auto lk = acquire_with_retry_(file_, 0, 400);
    if (!lk) return lk.error();
    if (auto rh = refresh_record_count_(); !rh) return rh.error();

    std::uint32_t new_recno = rec_count_ + 1;
    std::uint64_t offset = static_cast<std::uint64_t>(hdr_len_) +
                           static_cast<std::uint64_t>(rec_count_) *
                           static_cast<std::uint64_t>(rec_len_);

    std::vector<std::uint8_t> tmp(buf, buf + n);
    denormalize_deletion_flag_(tmp.data());
    if (tmp[1] == 0x20 && tmp[2] == 0x20 && tmp[3] == 0x20 && tmp[4] == 0x20) {
        tmp[1] = tmp[2] = tmp[3] = tmp[4] = 0x00;
    }

    auto wrote = file_.write_at(offset, tmp.data(), tmp.size());
    if (!wrote) return wrote.error();
    if (wrote.value() != n) {
        return util::Error{5000, 0, "short write on ADT record body", ""};
    }

    rec_count_ = new_recno;
    if (auto r = rewrite_header_(); !r) return r.error();
    return new_recno;
}

// ---------------------------------------------------------------------------
// Header helpers
// ---------------------------------------------------------------------------

util::Result<void> AdtDriver::refresh_record_count_() {
    std::uint8_t buf[4]{};
    auto got = file_.read_at(24, buf, sizeof(buf));
    if (!got) return got.error();
    if (got.value() < 4) {
        return util::Error{5103, 0,
            "ADT header truncated during refresh", ""};
    }
    rec_count_ = read_u32_le(buf);
    cap_record_count_from_size_();
    return {};
}

util::Result<void> AdtDriver::refresh_record_count_shared_() {
    // Wait out any exclusive header lock held by an appender, then refresh
    // (and cap). Mirrors the pattern used by CdxDriver.
    auto lk = acquire_with_retry_(file_, 0, 400, platform::LockKind::Shared);
    (void)lk;
    return refresh_record_count_();
}

void AdtDriver::cap_record_count_from_size_() {
    auto szr = file_.size();
    if (!szr) return;
    auto sz = szr.value();
    std::uint64_t data_sz = (sz > hdr_len_) ? (sz - hdr_len_) : 0ULL;
    std::uint32_t phys = (rec_len_ > 0)
        ? static_cast<std::uint32_t>(data_sz / rec_len_)
        : 0u;
    if (rec_count_ > phys) rec_count_ = phys;
}

util::Result<void> AdtDriver::rewrite_header_() {
    // Only the rec_count field needs updating on ordinary append/zap.
    std::uint8_t buf[4]{};
    buf[0] = static_cast<std::uint8_t>( rec_count_        & 0xFFu);
    buf[1] = static_cast<std::uint8_t>((rec_count_ >>  8) & 0xFFu);
    buf[2] = static_cast<std::uint8_t>((rec_count_ >> 16) & 0xFFu);
    buf[3] = static_cast<std::uint8_t>((rec_count_ >> 24) & 0xFFu);
    auto wrote = file_.write_at(24, buf, sizeof(buf));
    if (!wrote) return wrote.error();
    if (wrote.value() != 4) {
        return util::Error{5000, 0, "short write on ADT header", ""};
    }
    return {};
}

// ---------------------------------------------------------------------------
// flush / zap / bump_autoinc
// ---------------------------------------------------------------------------

util::Result<void> AdtDriver::flush() {
    return file_.sync();
}

util::Result<void> AdtDriver::zap() {
    if (mode_ == DriverOpenMode::ReadOnly) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    invalidate_read_cache_();   // file truncated below
    rec_count_ = 0;
    if (auto r = rewrite_header_(); !r) return r.error();
    // Truncate to the header length so stale record bytes don't linger.
    // Table::pack() calls zap() then re-appends survivors, so this also
    // gives pack a clean starting point.
    if (auto r = file_.truncate(static_cast<std::uint64_t>(hdr_len_)); !r)
        return r.error();
    return file_.sync();
}

util::Result<bool> AdtDriver::truncate_to(std::uint32_t recno) {
    if (mode_ == DriverOpenMode::ReadOnly) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    // Hold the header region while we refresh the count and shrink the file.
    auto lk = acquire_with_retry_(file_, 0, 400);
    if (!lk) return lk.error();
    if (auto rh = refresh_record_count_(); !rh) return rh.error();
    if (recno > rec_count_) return false;   // can't grow; nothing trailing to drop
    rec_count_ = recno;
    if (auto r = rewrite_header_(); !r) return r.error();
    // ADT keeps no 0x1A EOF marker (unlike DBF): file size is exactly
    // hdr_len_ + rec_count_*rec_len_, so shrink to that and a later reopen's
    // physical count matches the header.
    std::uint64_t end_off = static_cast<std::uint64_t>(hdr_len_) +
                            static_cast<std::uint64_t>(rec_count_) *
                            static_cast<std::uint64_t>(rec_len_);
    if (auto tr = file_.truncate(end_off); !tr) return tr.error();
    return true;
}

util::Result<std::uint32_t>
AdtDriver::bump_autoinc(std::uint16_t field_index) {
    if (field_index >= fields_.size()) {
        return util::Error{5063, 0, "field index out of range", ""};
    }
    auto& f = fields_[field_index];
    if (!f.autoinc) {
        return util::Error{5063, 0, "field is not autoinc", f.name};
    }
    std::uint32_t curr = f.autoinc_next;
    std::uint32_t step = f.autoinc_step ? f.autoinc_step : 1u;
    f.autoinc_next = curr + step;
    // In-memory only — bytes 139-143 of AUTOINC descriptors stay zero on disk.
    return curr;
}

} // namespace openads::drivers::adt
