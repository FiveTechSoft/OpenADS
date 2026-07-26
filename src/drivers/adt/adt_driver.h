#pragma once

#include "drivers/driver_trait.h"
#include "platform/file.h"
#include "platform/lock.h"

namespace openads::drivers::adt {

// SAP Advantage Data Table (.adt) driver.
//
// File layout:
//   400-byte header: signature "Advantage Table" at 0-14, rec_count at 24
//   (uint32 LE), hdr_len at 32 (uint32 LE, = 400 + num_fields*200),
//   rec_len at 36 (uint32 LE).
//   Field descriptors: 200 bytes each, starting at offset 400.
//     name at 0-127 (null-terminated), type at 129 (uint16 LE), record
//     offset at 131 (uint16 LE), length at 135 (uint16 LE), decimals at
//     137 (uint16 LE). AutoInc fields have autoinc_next at 139 (uint32 LE)
//     and autoinc_step at 143 (uint8).
//
// Record layout (rec_len bytes per record):
//   Byte 0:    deletion flag 0x04=active / 0x05=deleted.
//              Normalised to DBF ' '/'*' on the way out of read_record_raw
//              and translated back on write.
//   Bytes 1-4: 4-byte null bitmap (bit N = field N is null, declaration order).
//   Bytes 5+:  field data at offsets matching DbfField::record_offset.
//
// Memo/Binary refs (9 bytes in-record): uint32 LE block_no + uint32 LE
// data_len + 0x00. block_no == 0 → no content. Resolved by AdmMemo.
class AdtDriver final : public IDriver {
public:
    util::Result<void>
        open(const std::string& path, DriverOpenMode mode) override;

    std::uint32_t record_count() const noexcept override { return rec_count_; }
    std::uint16_t record_length() const noexcept override {
        return static_cast<std::uint16_t>(rec_len_);
    }
    std::uint16_t header_length() const noexcept override {
        return static_cast<std::uint16_t>(hdr_len_);
    }
    const std::vector<DbfField>& fields() const noexcept override { return fields_; }
    platform::File& file() override { return file_; }

    util::Result<std::vector<std::uint8_t>>
        read_record_raw(std::uint32_t recno) override;

    util::Result<void>
        write_record_raw(std::uint32_t recno,
                         const std::uint8_t* buf, std::size_t n) override;

    util::Result<std::uint32_t>
        append_record_raw(const std::uint8_t* buf, std::size_t n) override;

    util::Result<void> flush() override;
    util::Result<void> zap()   override;

    util::Result<std::uint32_t>
        bump_autoinc(std::uint16_t field_index) override;

    void refresh_record_count_from_disk() noexcept override {
        (void)refresh_record_count_();
    }

    // Drop the sequential read-ahead block (the engine calls this when a peer
    // may have changed the file under us, mirroring CdxDriver).
    void invalidate_read_cache() noexcept override { invalidate_read_cache_(); }

private:
    util::Result<void> refresh_record_count_();
    // Refresh the header count while an appender may hold 0..399 exclusive:
    // take the region shared (with retry) first, so the read waits instead of
    // failing.
    util::Result<void> refresh_record_count_shared_();
    // Clamp rec_count_ to the records that physically fit in the file.
    void               cap_record_count_from_size_();
    util::Result<void> rewrite_header_();

    // Sequential read-ahead block cache (raw on-disk bytes). The reindex /
    // PACK scans read every record 1..N once per tag; without this each read
    // was a positioned syscall + heap alloc, making an ADT reindex of a large
    // table (ESTAELEC ~441k) take minutes vs ~seconds for DBF/CDX. Mirrors
    // CdxDriver: a miss fetches one ALIGNED block of up to kReadAheadBytes,
    // hits are served by memcpy. Invalidated on any local write/append/zap.
    static constexpr std::size_t kReadAheadBytes = 64u * 1024u;
    void invalidate_read_cache_() noexcept {
        read_cache_first_ = 0;
        read_cache_recs_  = 0;
    }

    // Translate a record buffer between ADT on-disk format and the
    // DBF-convention format exposed to the engine layer. The only
    // byte that differs is byte 0 (deletion flag).
    static void normalize_deletion_flag_(std::uint8_t* buf) noexcept;
    static void denormalize_deletion_flag_(std::uint8_t* buf) noexcept;

    platform::File        file_;
    std::vector<DbfField> fields_;
    DriverOpenMode        mode_      = DriverOpenMode::ReadOnly;
    std::uint32_t         rec_count_ = 0;
    std::uint32_t         rec_len_   = 0;
    std::uint32_t         hdr_len_   = 0;

    // Read-ahead block cache state (raw file bytes). read_cache_first_ == 0
    // means empty/invalid. Records [first, first+recs) live in read_cache_.
    std::vector<std::uint8_t> read_cache_;
    std::uint32_t             read_cache_first_ = 0;
    std::uint32_t             read_cache_recs_  = 0;
};

} // namespace openads::drivers::adt
