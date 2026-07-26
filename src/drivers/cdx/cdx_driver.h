#pragma once

#include "drivers/driver_trait.h"
#include "engine/aes.h"
#include "platform/file.h"

#include <array>
#include <optional>

namespace openads::drivers::cdx {

class CdxDriver final : public IDriver {
public:
    util::Result<void>
        open(const std::string& path, DriverOpenMode mode) override;

    std::uint32_t record_count() const noexcept override { return rec_count_; }
    std::uint16_t record_length() const noexcept override { return rec_len_; }
    std::uint16_t header_length() const noexcept override { return hdr_len_; }
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
    util::Result<bool> truncate_trailing(std::uint32_t recno) override;
    util::Result<bool> truncate_to(std::uint32_t recno) override;

    util::Result<std::uint32_t>
        bump_autoinc(std::uint16_t field_index) override;

    void invalidate_read_cache() noexcept override { invalidate_read_cache_(); }
    void refresh_record_count_from_disk() noexcept override {
        (void)refresh_count_shared_();
    }

    // M11.2 — encrypted DBF support. `encrypted()` reflects the
    // header version byte (0xC3 = OpenADS-encrypted variant);
    // `set_encryption_key` installs the AES-256 key the driver uses
    // to transparently encrypt / decrypt record bodies. Encrypts
    // every existing record on demand for plain → encrypted upgrade.
    bool encrypted() const noexcept { return encrypted_; }
    std::uint8_t header_version() const noexcept { return header_version_; }
    bool partial_encrypted() const noexcept { return partial_enc_; }
    bool record_encrypted(std::uint32_t recno) const noexcept;
    util::Result<void>
        set_encryption_key(const std::array<std::uint8_t, 32>& key);
    util::Result<void> encrypt_in_place(
        const std::array<std::uint8_t, 32>& key);
    util::Result<void> encrypt_record_at(std::uint32_t recno);
    util::Result<void> decrypt_record_at(std::uint32_t recno);

private:
    util::Result<void> load_enc_bitmap_();
    util::Result<void> save_enc_bitmap_();
    bool               record_encrypted_(std::uint32_t recno) const noexcept;
    void               set_record_encrypted_(std::uint32_t recno, bool on);
    util::Result<void> rewrite_header_();
    // Read-ahead block cache (#4 perf). Sequential scans otherwise pay
    // one positioned ReadFile + one heap alloc PER record in
    // read_record_raw; with N records that is N syscalls (and, over a
    // network share or in server mode, N round-trips). Instead we read
    // one ALIGNED block of up to kReadAheadBytes worth of records and
    // serve subsequent records from it (forward AND backward within the
    // block). The cache holds RAW (still-encrypted) file bytes; the
    // CTR keystream is applied per served record (it is recno-keyed).
    // Invalidated on every mutation routed through this driver instance
    // (write / append / zap / truncate / autoinc / re-key). Cross-process
    // update coherence in shared mode is bounded by the block size — a
    // record changed by ANOTHER connection after we buffered it is served
    // stale until the block is refetched; the lock path can force a
    // refetch via invalidate_read_cache_(). [review caveat]
    static constexpr std::size_t kReadAheadBytes = 64u * 1024u;
    void invalidate_read_cache_() noexcept {
        read_cache_first_ = 0;
        read_cache_recs_  = 0;
    }
    // Re-read bytes 0..7 of the DBF header from disk and refresh
    // rec_count_ from the on-disk truth. Caller must hold an
    // exclusive byte-lock on the header before invoking, otherwise
    // the refresh races against other writers.
    util::Result<void> refresh_record_count_();
    // Re-read the on-disk record count under a shared header lock, used
    // by the fetch path when a recno appears to be past the end: a peer
    // connection may have appended since we cached rec_count_ at open().
    // Best-effort — falls back to an unlocked refresh if the lock can't
    // be taken in time, never leaving rec_count_ worse than it was.
    util::Result<void> refresh_count_shared_();
    // Raw on-disk bytes — no CTR transform (used by record-level encrypt).
    util::Result<std::vector<std::uint8_t>>
        read_record_disk_(std::uint32_t recno);
    void               apply_ctr_(std::uint8_t* buf, std::size_t n,
                                  std::uint32_t recno) const;

    platform::File              file_;
    std::vector<DbfField>       fields_;
    DriverOpenMode              mode_      = DriverOpenMode::ReadOnly;
    std::uint32_t               rec_count_ = 0;
    std::uint16_t               rec_len_   = 0;
    std::uint16_t               hdr_len_   = 0;
    // M11.2 — encryption state. encrypted_ mirrors the header byte;
    // aes_ is populated once the connection's password key is bound.
    bool                        encrypted_ = false;
    std::uint8_t                header_version_ = 0;
    bool                        partial_enc_ = false;
    std::uint32_t               enc_bitmap_offset_ = 0;
    std::vector<std::uint8_t>   enc_bitmap_;
    std::optional<engine::Aes>  aes_;
    // Read-ahead block cache state (raw file bytes). read_cache_first_ == 0
    // means empty; otherwise it holds records [first .. first+recs-1].
    std::vector<std::uint8_t>   read_cache_;
    std::uint32_t               read_cache_first_ = 0;
    std::uint32_t               read_cache_recs_  = 0;
};

} // namespace openads::drivers::cdx
