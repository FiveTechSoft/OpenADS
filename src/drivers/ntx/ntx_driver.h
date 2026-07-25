#pragma once

#include "drivers/driver_trait.h"
#include "drivers/cdx/cdx_driver.h"

namespace openads::drivers::ntx {

// In M2 the NTX driver is a label-only specialisation: the .dbf bytes
// look identical to a CDX-typed file. The .ntx index file lands in M3.
class NtxDriver final : public IDriver {
public:
    util::Result<void>
        open(const std::string& path, DriverOpenMode mode) override
    { return inner_.open(path, mode); }

    std::uint32_t record_count() const noexcept override
    { return inner_.record_count(); }
    void refresh_record_count_from_disk() noexcept override
    { inner_.refresh_record_count_from_disk(); }
    std::uint16_t record_length() const noexcept override
    { return inner_.record_length(); }
    std::uint16_t header_length() const noexcept override
    { return inner_.header_length(); }
    const std::vector<DbfField>& fields() const noexcept override
    { return inner_.fields(); }
    platform::File& file() override { return inner_.file(); }

    util::Result<std::vector<std::uint8_t>>
        read_record_raw(std::uint32_t recno) override
    { return inner_.read_record_raw(recno); }

    util::Result<void>
        write_record_raw(std::uint32_t recno,
                         const std::uint8_t* buf, std::size_t n) override
    { return inner_.write_record_raw(recno, buf, n); }

    util::Result<std::uint32_t>
        append_record_raw(const std::uint8_t* buf, std::size_t n) override
    { return inner_.append_record_raw(buf, n); }

    util::Result<void> flush() override { return inner_.flush(); }
    util::Result<void> zap()   override { return inner_.zap();   }

private:
    cdx::CdxDriver inner_;
};

} // namespace openads::drivers::ntx
