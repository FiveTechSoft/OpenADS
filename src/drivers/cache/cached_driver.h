#pragma once

#include "drivers/driver_trait.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace openads::drivers::cache {

enum class TableCacheMode : std::uint16_t {
    None   = 0,
    Reads  = 1,
    Writes = 2,
};

// RCB 06/28/2026: Driver wrapper for DD-controlled table caching
// (ADS_DD_TABLE_CACHING). It is intentionally a wrapper instead of a new Table
// implementation so every cursor operation still flows through the existing
// Table engine. Small, frequently-read DD tables can be served from memory
// while the physical driver remains responsible for file format details,
// locking, encryption, autoinc counters, and durable writes.
class CachedDriver final : public IDriver {
public:
    static constexpr std::size_t kDefaultMaxBytes = 16u * 1024u * 1024u;

    static util::Result<std::unique_ptr<CachedDriver>>
        create(std::unique_ptr<IDriver> inner,
               TableCacheMode mode,
               std::size_t max_bytes = kDefaultMaxBytes);

    IDriver* unwrap() noexcept override { return inner_->unwrap(); }
    const IDriver* unwrap() const noexcept override { return inner_->unwrap(); }

    util::Result<void> open(const std::string& path,
                            DriverOpenMode mode) override;

    std::uint32_t record_count() const noexcept override;
    std::uint16_t record_length() const noexcept override;
    std::uint16_t header_length() const noexcept override;
    const std::vector<DbfField>& fields() const noexcept override;
    platform::File& file() override;

    util::Result<std::vector<std::uint8_t>>
        read_record_raw(std::uint32_t recno) override;

    util::Result<void>
        write_record_raw(std::uint32_t recno,
                         const std::uint8_t* buf, std::size_t n) override;

    util::Result<std::uint32_t>
        append_record_raw(const std::uint8_t* buf, std::size_t n) override;

    util::Result<void> flush() override;
    util::Result<void> zap() override;
    util::Result<bool> truncate_trailing(std::uint32_t recno) override;
    util::Result<std::uint32_t>
        bump_autoinc(std::uint16_t field_index) override;

    void invalidate_read_cache() noexcept override {}
    void refresh_record_count_from_disk() noexcept override {
        if (inner_) inner_->refresh_record_count_from_disk();
    }

    TableCacheMode mode() const noexcept { return mode_; }

private:
    CachedDriver(std::unique_ptr<IDriver> inner, TableCacheMode mode);

    util::Result<void> load_all_(std::size_t max_bytes);

    std::unique_ptr<IDriver> inner_;
    TableCacheMode mode_ = TableCacheMode::None;
    std::vector<std::vector<std::uint8_t>> records_;
};

} // namespace openads::drivers::cache
