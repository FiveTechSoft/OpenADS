#include "drivers/fpt/fpt_memo.h"

#include <cstring>
#include <vector>

namespace openads::drivers::fpt {

namespace {

constexpr std::uint16_t FPT_HEADER_LEN = 512;

void write_u16_be(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    p[1] = static_cast<std::uint8_t>( v       & 0xFFu);
}
void write_u32_be(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >>  8) & 0xFFu);
    p[3] = static_cast<std::uint8_t>( v        & 0xFFu);
}
std::uint16_t read_u16_be(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}
std::uint32_t read_u32_be(const std::uint8_t* p) {
    return  (static_cast<std::uint32_t>(p[0]) << 24) |
            (static_cast<std::uint32_t>(p[1]) << 16) |
            (static_cast<std::uint32_t>(p[2]) <<  8) |
             static_cast<std::uint32_t>(p[3]);
}

platform::OpenMode map_mode(MemoOpenMode m) {
    switch (m) {
        case MemoOpenMode::ReadOnly:  return platform::OpenMode::ReadOnly;
        case MemoOpenMode::Shared:    return platform::OpenMode::OpenExisting;
        case MemoOpenMode::Exclusive: return platform::OpenMode::OpenExisting;
    }
    return platform::OpenMode::ReadOnly;
}

} // namespace

util::Result<void>
FptMemo::open(const std::string& path, MemoOpenMode mode) {
    mode_ = mode;
    auto fres = platform::File::open(path, map_mode(mode));
    if (!fres) return fres.error();
    file_ = std::move(fres).value();

    std::uint8_t hdr[FPT_HEADER_LEN]{};
    auto got = file_.read_at(0, hdr, FPT_HEADER_LEN);
    if (!got) return got.error();
    if (got.value() < 8) {
        return util::Error{5103, 0, "FPT header truncated", path};
    }
    next_avail_ = read_u32_be(hdr);
    block_size_ = read_u16_be(hdr + 6);
    if (block_size_ == 0) block_size_ = 64;
    return {};
}

util::Result<std::string>
FptMemo::read(std::uint32_t block_no) {
    if (block_no == 0) return std::string{};
    std::uint64_t off = static_cast<std::uint64_t>(block_no) * block_size_;
    std::uint8_t entry[8]{};
    auto got = file_.read_at(off, entry, sizeof(entry));
    if (!got) return got.error();
    if (got.value() < sizeof(entry)) {
        return util::Error{5103, 0, "FPT memo header truncated", ""};
    }
    /* uint32_t type = read_u32_be(entry); */
    std::uint32_t length = read_u32_be(entry + 4);
    if (length > read_limit_)
        return util::Error{7079, 0, "remote memo read budget exceeded", ""};
    auto size = file_.size();
    if (!size) return size.error();
    if (off + 8 > size.value() || length > size.value() - (off + 8))
        return util::Error{5103, 0, "FPT memo payload truncated", ""};
    std::vector<std::uint8_t> buf(length, 0);
    if (length > 0) {
        auto rg = file_.read_at(off + 8, buf.data(), buf.size());
        if (!rg) return rg.error();
        if (rg.value() < buf.size()) {
            return util::Error{5103, 0, "FPT memo payload truncated", ""};
        }
    }
    return std::string(reinterpret_cast<const char*>(buf.data()), length);
}

util::Result<std::uint32_t>
FptMemo::write(const std::string& payload) {
    return write_typed(payload, MemoBlockType::Text);
}

util::Result<std::uint32_t>
FptMemo::write_typed(const std::string& payload, MemoBlockType type) {
    if (mode_ == MemoOpenMode::ReadOnly) {
        return util::Error{5000, 0, "FPT opened read-only", ""};
    }
    std::uint32_t start = next_avail_;
    std::size_t needed = 8 + payload.size();
    std::size_t blocks = (needed + block_size_ - 1) / block_size_;
    std::vector<std::uint8_t> buf(blocks * block_size_, 0);
    write_u32_be(buf.data(),     static_cast<std::uint32_t>(type));
    write_u32_be(buf.data() + 4, static_cast<std::uint32_t>(payload.size()));
    if (!payload.empty()) {
        std::memcpy(buf.data() + 8, payload.data(), payload.size());
    }
    std::uint64_t off = static_cast<std::uint64_t>(start) * block_size_;
    auto wrote = file_.write_at(off, buf.data(), buf.size());
    if (!wrote) return wrote.error();
    if (wrote.value() != buf.size()) {
        return util::Error{5000, 0, "short write on FPT memo", ""};
    }
    next_avail_ = static_cast<std::uint32_t>(start + blocks);
    if (auto r = rewrite_header_(); !r) return r.error();
    return start;
}

util::Result<MemoBlockType>
FptMemo::read_type(std::uint32_t block_no) {
    if (block_no == 0) return MemoBlockType::Text;
    std::uint64_t off = static_cast<std::uint64_t>(block_no) * block_size_;
    std::uint8_t entry[8]{};
    auto got = file_.read_at(off, entry, sizeof(entry));
    if (!got) return got.error();
    if (got.value() < sizeof(entry)) {
        return util::Error{5103, 0, "FPT memo header truncated", ""};
    }
    std::uint32_t raw = read_u32_be(entry);
    switch (raw) {
        case 0:  return MemoBlockType::Picture;
        case 1:  return MemoBlockType::Text;
        case 2:  return MemoBlockType::Object;
        default: return MemoBlockType::Text;
    }
}

util::Result<void> FptMemo::free_block(std::uint32_t /*block_no*/) {
    return {};
}

util::Result<void> FptMemo::flush() {
    return file_.sync();
}

util::Result<void> FptMemo::rewrite_header_() {
    std::uint8_t hdr[FPT_HEADER_LEN]{};
    write_u32_be(hdr,     next_avail_);
    write_u16_be(hdr + 6, block_size_);
    auto wrote = file_.write_at(0, hdr, FPT_HEADER_LEN);
    if (!wrote) return wrote.error();
    return {};
}

util::Result<FptMemo>
FptMemo::create(const std::string& path, std::uint16_t block_size) {
    auto fres = platform::File::open(path, platform::OpenMode::CreateRW);
    if (!fres) return fres.error();
    platform::File file = std::move(fres).value();

    if (block_size == 0) block_size = 64;
    // First memo block lives at offset 8 * block_size (== FPT_HEADER_LEN
    // when block_size = 64; for larger blocks the header spans more area
    // but only the first 8 bytes hold information).
    std::uint32_t first_block = (FPT_HEADER_LEN + block_size - 1) / block_size;

    std::uint8_t hdr[FPT_HEADER_LEN]{};
    write_u32_be(hdr,     first_block);
    write_u16_be(hdr + 6, block_size);
    auto wrote = file.write_at(0, hdr, FPT_HEADER_LEN);
    if (!wrote) return wrote.error();
    if (auto s = file.sync(); !s) return s.error();

    FptMemo m;
    m.file_       = std::move(file);
    m.mode_       = MemoOpenMode::Shared;
    m.next_avail_ = first_block;
    m.block_size_ = block_size;
    return m;
}

} // namespace openads::drivers::fpt
