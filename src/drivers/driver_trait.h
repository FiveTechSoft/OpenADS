#pragma once

#include "drivers/dbf_common.h"
#include "platform/file.h"
#include "util/result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openads::drivers {

enum class DriverOpenMode {
    ReadOnly,
    Shared,    // read+write, multiple openers
    Exclusive  // read+write, sole opener
};

class IDriver {
public:
    virtual ~IDriver() = default;

    virtual IDriver* unwrap() noexcept { return this; }
    virtual const IDriver* unwrap() const noexcept { return this; }

    virtual util::Result<void>
        open(const std::string& path, DriverOpenMode mode) = 0;

    virtual std::uint32_t record_count() const noexcept = 0;
    virtual std::uint16_t record_length() const noexcept = 0;
    virtual std::uint16_t header_length() const noexcept = 0;
    virtual const std::vector<DbfField>& fields() const noexcept = 0;
    virtual platform::File& file() = 0;

    virtual util::Result<std::vector<std::uint8_t>>
        read_record_raw(std::uint32_t recno) = 0;

    virtual util::Result<void>
        write_record_raw(std::uint32_t recno,
                         const std::uint8_t* buf, std::size_t n) = 0;

    virtual util::Result<std::uint32_t>
        append_record_raw(const std::uint8_t* buf, std::size_t n) = 0;

    virtual util::Result<void> flush() = 0;

    // Drop every record. Header record count goes to zero, the EOF
    // marker is written right after the field-descriptor block, and
    // the driver's in-memory rec count is reset. The records area on
    // disk may still contain stale bytes — DBF readers respect the
    // header count, so subsequent reads see an empty table. Drivers
    // override to also reset their internal record-cache state.
    virtual util::Result<void> zap() = 0;

    // Roll back a trailing append. If `recno` is currently the last
    // physical record, drop it — lower the header record count and
    // rewrite the EOF marker — and return true. If a concurrent append
    // now sits above `recno`, return false so the caller soft-deletes
    // instead (a buried record can't be popped without a pack). The
    // default rejects with false for drivers that don't implement
    // physical removal, preserving the soft-delete fallback.
    virtual util::Result<bool> truncate_trailing(std::uint32_t /*recno*/) {
        return false;
    }

    // Drop every record above `recno` in one shot (keep 1..recno), updating
    // the header count + EOF marker. Used by PACK after the survivors have
    // been copied down to 1..dst, so it avoids the read-all + zap + re-append
    // pass. Default rejects with false so drivers that don't implement it fall
    // back to the legacy save/zap/re-append path.
    virtual util::Result<bool> truncate_to(std::uint32_t /*recno*/) {
        return false;
    }

    // VFP autoinc bump (M10.11). Returns the value to use for the
    // pending append (the field's current `autoinc_next`), advances
    // the in-memory counter by `autoinc_step`, and persists the new
    // value back to the field-descriptor block on disk. Default
    // implementation rejects with AE_FUNCTION_NOT_AVAILABLE for
    // drivers that don't yet wire up the persistent counter.
    virtual util::Result<std::uint32_t>
        bump_autoinc(std::uint16_t /*field_index*/) {
        return util::Error{5004, 0, "autoinc not supported", ""};
    }

    // Drop any read-ahead block the driver is holding so the next
    // read_record_raw goes back to disk. The engine calls this at
    // coherence points where the in-memory block may be stale relative
    // to disk: an explicit record refresh, and absolute repositioning
    // (GoTo / GoTop / GoBottom) — which is also how a workarea observes
    // a write made through another handle (e.g. a trigger updating a
    // second table). Sequential SKIP does NOT call this, so a scan keeps
    // serving from the block. Default no-op for drivers without a cache.
    virtual void invalidate_read_cache() noexcept {}

    // Re-read the on-disk record count so the in-memory value reflects
    // appends/deletes made by other connections.  The server calls this
    // when answering GetRecordCount to keep the value fresh across
    // concurrent clients.  Default no-op (drivers that cache the count
    // in rec_count_ must override).
    virtual void refresh_record_count_from_disk() noexcept {}
};

} // namespace openads::drivers
