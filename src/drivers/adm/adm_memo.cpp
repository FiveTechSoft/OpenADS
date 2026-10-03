#include "drivers/adm/adm_memo.h"

#include <cstring>
#include <vector>

namespace openads::drivers::adm {

namespace {

platform::OpenMode map_mode(MemoOpenMode m) {
    switch (m) {
        case MemoOpenMode::ReadOnly:  return platform::OpenMode::ReadOnly;
        case MemoOpenMode::Shared:    return platform::OpenMode::OpenExisting;
        case MemoOpenMode::Exclusive: return platform::OpenMode::OpenExisting;
    }
    return platform::OpenMode::ReadOnly;
}

std::uint32_t read_u32_le(const std::uint8_t* p) {
    return  static_cast<std::uint32_t>(p[0])        |
           (static_cast<std::uint32_t>(p[1]) <<  8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

void write_u32_le(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>( v        & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >>  8) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

} // namespace

util::Result<void>
AdmMemo::open(const std::string& path, MemoOpenMode mode) {
    mode_ = mode;
    auto fres = platform::File::open(path, map_mode(mode));
    if (!fres) return fres.error();
    file_ = std::move(fres).value();

    // Recover next_avail_ (offset 20, uint32 LE) from the file header prefix.
    std::uint8_t hdr[32]{};
    auto got = file_.read_at(0, hdr, sizeof(hdr));
    if (!got) return got.error();
    if (got.value() < 24) {
        return util::Error{5103, 0, "ADM header truncated", ""};
    }
    next_avail_ = read_u32_le(hdr + 20);
    if (next_avail_ < kDataBlockOrigin) next_avail_ = kDataBlockOrigin;
    return {};
}

// ADM stores data verbatim at block_num * kBlockSize; there is no
// per-block length header. data_len is supplied from the 9-byte
// in-record reference (4-byte block_no + 4-byte data_len + 0x00).
util::Result<std::string>
AdmMemo::read(std::uint32_t block_no) {
    // Without data_len we can't know where the content ends; return empty
    // so callers that don't supply a length see a graceful blank instead
    // of garbage or an error. The engine always calls read(block_no, len).
    if (block_no == 0) return std::string{};
    return std::string{};
}

util::Result<std::string>
AdmMemo::read(std::uint32_t block_no, std::uint32_t data_len) {
    if (block_no == 0 || data_len == 0) return std::string{};
    std::uint64_t off = static_cast<std::uint64_t>(block_no) * kBlockSize;
    if (data_len > read_limit_)
        return util::Error{7079, 0, "remote memo read budget exceeded", ""};
    auto size = file_.size();
    if (!size) return size.error();
    if (off > size.value() || data_len > size.value() - off)
        return util::Error{5103, 0, "ADM memo payload truncated", ""};
    std::vector<std::uint8_t> buf(data_len, 0);
    auto got = file_.read_at(off, buf.data(), buf.size());
    if (!got) return got.error();
    if (got.value() < buf.size()) {
        return util::Error{5103, 0, "ADM memo payload truncated", ""};
    }
    return std::string(reinterpret_cast<const char*>(buf.data()), data_len);
}

util::Result<std::uint32_t>
AdmMemo::write(const std::string& payload) {
    if (mode_ == MemoOpenMode::ReadOnly) {
        return util::Error{5000, 0, "ADM opened read-only", ""};
    }
    std::uint32_t start = next_avail_;
    // Round up to whole blocks.
    std::uint32_t blocks = static_cast<std::uint32_t>(
        (payload.size() + kBlockSize - 1) / kBlockSize);
    if (blocks == 0) blocks = 1;

    std::vector<std::uint8_t> buf(
        static_cast<std::size_t>(blocks) * kBlockSize, 0);
    if (!payload.empty()) {
        std::memcpy(buf.data(), payload.data(),
                    std::min(payload.size(), buf.size()));
    }
    std::uint64_t off = static_cast<std::uint64_t>(start) * kBlockSize;
    auto wrote = file_.write_at(off, buf.data(), buf.size());
    if (!wrote) return wrote.error();
    if (wrote.value() != buf.size()) {
        return util::Error{5000, 0, "short write on ADM memo", ""};
    }
    next_avail_ = start + blocks;
    if (auto r = rewrite_header_(); !r) return r.error();
    return start;
}

util::Result<void> AdmMemo::flush() {
    return file_.sync();
}

util::Result<void> AdmMemo::rewrite_header_() {
    // Write only the next_avail_ field back (offset 20); the rest of
    // the header block is left untouched to preserve ADS metadata.
    std::uint8_t buf[4]{};
    write_u32_le(buf, next_avail_);
    auto wrote = file_.write_at(20, buf, sizeof(buf));
    if (!wrote) return wrote.error();
    return {};
}

util::Result<AdmMemo>
AdmMemo::create(const std::string& path) {
    auto fres = platform::File::open(path, platform::OpenMode::CreateRW);
    if (!fres) return fres.error();
    platform::File file = std::move(fres).value();

    // All-zero ADM headers are rejected by some clients; write a minimal prefix.
    static const std::uint8_t kAdmPrefix[20] = {
        0x00, 0x00, 0x37, 0xA0, 0x00, 0x00, 0x00, 0x08,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };
    std::vector<std::uint8_t> hdr(1024, 0);
    std::memcpy(hdr.data(), kAdmPrefix, sizeof(kAdmPrefix));
    write_u32_le(hdr.data() + 20, kDataBlockOrigin);
    auto wrote = file.write_at(0, hdr.data(), hdr.size());
    if (!wrote) return wrote.error();
    if (auto s = file.sync(); !s) return s.error();

    AdmMemo m;
    m.file_       = std::move(file);
    m.mode_       = MemoOpenMode::Shared;
    m.next_avail_ = kDataBlockOrigin;
    return m;
}

} // namespace openads::drivers::adm
