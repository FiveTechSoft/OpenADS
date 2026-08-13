#pragma once

#include "platform/file.h"
#include "util/result.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace openads::engine {

enum class ReplRecType : std::uint8_t {
    Insert    = 1,
    Update    = 2,
    Delete    = 3,
    TxBegin   = 4,
    TxCommit  = 5,
    TxAbort   = 6,
};

struct ReplIdent {
    std::string name;
    std::string value;
};

struct ReplRecord {
    ReplRecType            type = ReplRecType::Insert;
    std::uint64_t          lsn = 0;
    std::uint64_t          tx_id = 0;
    std::string            source_table;
    std::vector<ReplIdent> identity;
    std::vector<std::uint8_t> before;
    std::vector<std::uint8_t> after;
    std::string            origin_id;  // M12.34 — originating server identity
};

// Append-only durable replication queue. Each record has a 28-byte
// fixed header followed by variable payload and trailing CRC-32C.
// Layout (LE):
//
//   bytes 0-3   : magic 0x52504C51 ("RPLQ")
//   byte  4     : type (1..6)
//   byte  5     : flags (bit0=has_before, bit1=has_after, bit2=has_origin_id)
//   bytes 6-7   : payload length (uint16)
//   bytes 8-15  : lsn (uint64)
//   bytes 16-23 : tx_id (uint64)
//   bytes 24-27 : crc32c(header[0..23] + payload)
//   bytes 28..  : payload
//
// Row payload (types 1-3): source_table u16-len + utf8, then
// u16 nident, then nident * (u16 klen + key + u16 vlen + value),
// then if has_before u32 blen + bytes, then if has_after u32 alen + bytes,
// then if has_origin_id u16 olen + origin_id utf8 bytes.
// TX_* payloads are empty.

class ReplQueue {
public:
    ReplQueue() = default;
    ReplQueue(const ReplQueue&) = delete;
    ReplQueue& operator=(const ReplQueue&) = delete;
    ReplQueue(ReplQueue&& o) noexcept;
    ReplQueue& operator=(ReplQueue&& o) noexcept;

    util::Result<void> open(const std::string& path);

    util::Result<std::uint64_t> append(const ReplRecord& rec);

    util::Result<std::vector<ReplRecord>> read_from(std::uint64_t after_lsn);

    std::uint64_t high_water_lsn() const noexcept {
        return next_lsn_.load(std::memory_order_acquire);
    }
    bool is_open() const noexcept { return file_.is_open(); }

private:
    platform::File          file_;
    std::uint64_t           write_offset_ = 0;
    std::string             path_;
    std::atomic<std::uint64_t> next_lsn_{1};
    std::mutex                 append_mu_;
};

} // namespace openads::engine
