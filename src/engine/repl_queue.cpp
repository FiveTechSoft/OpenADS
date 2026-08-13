#include "engine/repl_queue.h"

#include <array>
#include <cstring>

namespace openads::engine {

namespace {

constexpr std::uint32_t RPLQ_MAGIC      = 0x514C5052u; // 'R','P','L','Q' LE
constexpr std::size_t   RPLQ_HEADER_LEN = 28;          // magic..crc inclusive

void write_u16_le(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>( v       & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}
void write_u32_le(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>( v        & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >>  8) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}
void write_u64_le(std::uint8_t* p, std::uint64_t v) {
    write_u32_le(p,     static_cast<std::uint32_t>( v        & 0xFFFFFFFFu));
    write_u32_le(p + 4, static_cast<std::uint32_t>((v >> 32) & 0xFFFFFFFFu));
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
std::uint64_t read_u64_le(const std::uint8_t* p) {
    return static_cast<std::uint64_t>(read_u32_le(p)) |
          (static_cast<std::uint64_t>(read_u32_le(p + 4)) << 32);
}

std::uint32_t crc32c(const std::uint8_t* data, std::size_t n) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            std::uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0x82F63B78u & mask);
        }
    }
    return ~crc;
}

struct ReplScan {
    std::vector<ReplRecord> records;
    std::size_t             end_pos = 0;
};

ReplScan scan_replq_buffer(const std::uint8_t* buf, std::size_t got_n) {
    ReplScan out;
    std::size_t pos = 0;
    while (pos + RPLQ_HEADER_LEN + 4 <= got_n) {
        if (read_u32_le(buf + pos) != RPLQ_MAGIC) break;
        std::uint8_t  type_byte  = buf[pos + 4];
        std::uint8_t  flags_byte = buf[pos + 5];
        std::uint16_t plen       = read_u16_le(buf + pos + 6);
        std::uint64_t lsn        = read_u64_le(buf + pos + 8);
        std::uint64_t tx_id      = read_u64_le(buf + pos + 16);
        std::size_t   rec_size   = RPLQ_HEADER_LEN + plen + 4;
        if (pos + rec_size > got_n) break;
        std::uint32_t stored_crc = read_u32_le(buf + pos + RPLQ_HEADER_LEN + plen);
        std::uint32_t calc_crc   = crc32c(buf + pos, RPLQ_HEADER_LEN + plen);
        if (stored_crc != calc_crc) break;

        ReplRecord r;
        r.type   = static_cast<ReplRecType>(type_byte);
        r.lsn    = lsn;
        r.tx_id  = tx_id;

        const std::uint8_t* p = buf + pos + RPLQ_HEADER_LEN;
        bool has_before = (flags_byte & 0x01) != 0;
        bool has_after  = (flags_byte & 0x02) != 0;

        if (type_byte >= 1 && type_byte <= 3 && plen >= 2) {
            // Row payload
            std::size_t off = 0;
            std::uint16_t tbl_len = read_u16_le(p + off); off += 2;
            if (off + tbl_len > plen) { out.records.push_back(std::move(r)); pos += rec_size; continue; }
            r.source_table.assign(reinterpret_cast<const char*>(p + off), tbl_len);
            off += tbl_len;
            if (off + 2 > plen) { out.records.push_back(std::move(r)); pos += rec_size; continue; }
            std::uint16_t nident = read_u16_le(p + off); off += 2;
            for (std::uint16_t i = 0; i < nident; ++i) {
                if (off + 2 > plen) break;
                std::uint16_t klen = read_u16_le(p + off); off += 2;
                if (off + klen > plen) break;
                std::string key(reinterpret_cast<const char*>(p + off), klen);
                off += klen;
                if (off + 2 > plen) break;
                std::uint16_t vlen = read_u16_le(p + off); off += 2;
                if (off + vlen > plen) break;
                std::string val(reinterpret_cast<const char*>(p + off), vlen);
                off += vlen;
                r.identity.push_back({std::move(key), std::move(val)});
            }
            if (has_before) {
                if (off + 4 > plen) { out.records.push_back(std::move(r)); pos += rec_size; continue; }
                std::uint32_t blen = read_u32_le(p + off); off += 4;
                if (off + blen > plen) { out.records.push_back(std::move(r)); pos += rec_size; continue; }
                r.before.assign(p + off, p + off + blen);
                off += blen;
            }
            if (has_after) {
                if (off + 4 > plen) { out.records.push_back(std::move(r)); pos += rec_size; continue; }
                std::uint32_t alen = read_u32_le(p + off); off += 4;
                if (off + alen > plen) { out.records.push_back(std::move(r)); pos += rec_size; continue; }
                r.after.assign(p + off, p + off + alen);
            }
        }
        out.records.push_back(std::move(r));
        pos += rec_size;
    }
    out.end_pos = pos;
    return out;
}

} // namespace

ReplQueue::ReplQueue(ReplQueue&& o) noexcept
    : file_(std::move(o.file_)),
      write_offset_(o.write_offset_),
      path_(std::move(o.path_)),
      next_lsn_(o.next_lsn_.load(std::memory_order_relaxed)) {
    o.write_offset_ = 0;
    o.path_.clear();
    o.next_lsn_.store(1, std::memory_order_relaxed);
}

ReplQueue& ReplQueue::operator=(ReplQueue&& o) noexcept {
    if (this != &o) {
        file_         = std::move(o.file_);
        write_offset_ = o.write_offset_;
        path_         = std::move(o.path_);
        next_lsn_.store(o.next_lsn_.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
        o.write_offset_ = 0;
        o.path_.clear();
        o.next_lsn_.store(1, std::memory_order_relaxed);
    }
    return *this;
}

util::Result<void> ReplQueue::open(const std::string& path) {
    path_ = path;
    auto fres = platform::File::open(path, platform::OpenMode::ReadWrite);
    if (!fres) {
        auto cre = platform::File::open(path, platform::OpenMode::CreateRW);
        if (!cre) return cre.error();
        file_ = std::move(cre).value();
    } else {
        file_ = std::move(fres).value();
    }
    auto sz = file_.size();
    if (!sz) return sz.error();
    write_offset_ = sz.value();

    next_lsn_.store(1, std::memory_order_relaxed);
    if (write_offset_ > 0) {
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(write_offset_), 0);
        auto got = file_.read_at(0, buf.data(), buf.size());
        if (!got) return got.error();
        ReplScan scan = scan_replq_buffer(buf.data(), got.value());
        if (scan.end_pos != write_offset_) {
            // Truncate at last good record (corrupt tail).
            write_offset_ = scan.end_pos;
            (void)file_.truncate(write_offset_);
        }
        std::uint64_t hi = 0;
        for (auto& r : scan.records) if (r.lsn > hi) hi = r.lsn;
        next_lsn_.store(hi + 1, std::memory_order_relaxed);
    }
    return {};
}

util::Result<std::uint64_t> ReplQueue::append(const ReplRecord& rec) {
    // Build payload for row types (1-3)
    std::vector<std::uint8_t> payload;
    if (rec.type >= ReplRecType::Insert && rec.type <= ReplRecType::Delete) {
        // source_table u16-len + utf8
        std::size_t plen = 2 + rec.source_table.size() + 2;
        for (auto& id : rec.identity) {
            plen += 2 + id.name.size() + 2 + id.value.size();
        }
        if (!rec.before.empty()) plen += 4 + rec.before.size();
        if (!rec.after.empty())  plen += 4 + rec.after.size();
        if (plen > 0xFFFFu) {
            return util::Error{5000, 0, "replq payload too large", ""};
        }
        payload.resize(plen);
        std::uint8_t* p = payload.data();
        std::size_t off = 0;
        write_u16_le(p + off, static_cast<std::uint16_t>(rec.source_table.size())); off += 2;
        if (!rec.source_table.empty()) {
            std::memcpy(p + off, rec.source_table.data(), rec.source_table.size());
            off += rec.source_table.size();
        }
        write_u16_le(p + off, static_cast<std::uint16_t>(rec.identity.size())); off += 2;
        for (auto& id : rec.identity) {
            write_u16_le(p + off, static_cast<std::uint16_t>(id.name.size())); off += 2;
            if (!id.name.empty()) { std::memcpy(p + off, id.name.data(), id.name.size()); off += id.name.size(); }
            write_u16_le(p + off, static_cast<std::uint16_t>(id.value.size())); off += 2;
            if (!id.value.empty()) { std::memcpy(p + off, id.value.data(), id.value.size()); off += id.value.size(); }
        }
        if (!rec.before.empty()) {
            write_u32_le(p + off, static_cast<std::uint32_t>(rec.before.size())); off += 4;
            std::memcpy(p + off, rec.before.data(), rec.before.size()); off += rec.before.size();
        }
        if (!rec.after.empty()) {
            write_u32_le(p + off, static_cast<std::uint32_t>(rec.after.size())); off += 4;
            std::memcpy(p + off, rec.after.data(), rec.after.size()); off += rec.after.size();
        }
    }
    // TX_* types: empty payload

    std::lock_guard<std::mutex> lk(append_mu_);
    std::uint64_t lsn = next_lsn_.fetch_add(1, std::memory_order_relaxed);

    std::uint8_t flags_byte = 0;
    if (!rec.before.empty()) flags_byte |= 0x01;
    if (!rec.after.empty())  flags_byte |= 0x02;

    std::vector<std::uint8_t> rec_buf(RPLQ_HEADER_LEN + payload.size() + 4, 0);
    write_u32_le(rec_buf.data() + 0, RPLQ_MAGIC);
    rec_buf[4] = static_cast<std::uint8_t>(rec.type);
    rec_buf[5] = flags_byte;
    write_u16_le(rec_buf.data() + 6, static_cast<std::uint16_t>(payload.size()));
    write_u64_le(rec_buf.data() + 8,  lsn);
    write_u64_le(rec_buf.data() + 16, rec.tx_id);
    if (!payload.empty()) {
        std::memcpy(rec_buf.data() + RPLQ_HEADER_LEN, payload.data(), payload.size());
    }
    std::uint32_t crc = crc32c(rec_buf.data(), RPLQ_HEADER_LEN + payload.size());
    write_u32_le(rec_buf.data() + RPLQ_HEADER_LEN + payload.size(), crc);

    auto wrote = file_.write_at(write_offset_, rec_buf.data(), rec_buf.size());
    if (!wrote) return wrote.error();
    if (wrote.value() != rec_buf.size()) {
        return util::Error{5000, 0, "short write on replq", ""};
    }
    write_offset_ += rec_buf.size();
    return lsn;
}

util::Result<std::vector<ReplRecord>> ReplQueue::read_from(std::uint64_t after_lsn) {
    if (write_offset_ == 0) return std::vector<ReplRecord>{};

    std::vector<std::uint8_t> buf(static_cast<std::size_t>(write_offset_), 0);
    auto got = file_.read_at(0, buf.data(), buf.size());
    if (!got) return got.error();
    ReplScan scan = scan_replq_buffer(buf.data(), got.value());

    std::vector<ReplRecord> result;
    for (auto& r : scan.records) {
        if (r.lsn > after_lsn) {
            result.push_back(std::move(r));
        }
    }
    return result;
}

} // namespace openads::engine
