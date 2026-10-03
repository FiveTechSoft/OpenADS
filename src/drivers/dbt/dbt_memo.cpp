#include "drivers/dbt/dbt_memo.h"

#include <cstring>
#include <vector>

namespace openads::drivers::dbt {

namespace {

constexpr std::uint16_t DBT_BLOCK = 512;

void write_u32_le(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>( v        & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >>  8) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

std::uint32_t read_u32_le(const std::uint8_t* p) {
    return  static_cast<std::uint32_t>(p[0])        |
           (static_cast<std::uint32_t>(p[1]) <<  8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
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
DbtMemo::open(const std::string& path, MemoOpenMode mode) {
    mode_ = mode;
    auto fres = platform::File::open(path, map_mode(mode));
    if (!fres) return fres.error();
    file_ = std::move(fres).value();

    std::uint8_t hdr[DBT_BLOCK]{};
    auto got = file_.read_at(0, hdr, DBT_BLOCK);
    if (!got) return got.error();
    if (got.value() < 4) {
        return util::Error{5103, 0, "DBT header truncated", path};
    }
    next_avail_ = read_u32_le(hdr);
    if (next_avail_ < 1) next_avail_ = 1;
    return {};
}

util::Result<std::string>
DbtMemo::read(std::uint32_t block_no) {
    if (block_no == 0) return std::string{};

    std::string out;
    out.reserve(DBT_BLOCK);
    std::vector<std::uint8_t> buf(DBT_BLOCK, 0);
    std::uint32_t cur = block_no;
    while (true) {
        std::uint64_t off = static_cast<std::uint64_t>(cur) * DBT_BLOCK;
        auto got = file_.read_at(off, buf.data(), buf.size());
        if (!got) return got.error();
        std::size_t got_n = got.value();
        // Search for 0x1A 0x1A terminator.
        std::size_t terminator = got_n;
        for (std::size_t i = 0; i + 1 < got_n; ++i) {
            if (buf[i] == 0x1A && buf[i + 1] == 0x1A) {
                terminator = i;
                break;
            }
        }
        if (terminator > read_limit_ - out.size())
            return util::Error{7079, 0, "remote memo read budget exceeded", ""};
        out.append(reinterpret_cast<const char*>(buf.data()), terminator);
        if (terminator < got_n) break;
        if (got_n < DBT_BLOCK) break;
        if (cur == std::numeric_limits<std::uint32_t>::max())
            return util::Error{5103, 0, "DBT memo block overflow", ""};
        ++cur;
    }
    return out;
}

util::Result<std::uint32_t>
DbtMemo::write(const std::string& payload) {
    if (mode_ == MemoOpenMode::ReadOnly) {
        return util::Error{5000, 0, "DBT opened read-only", ""};
    }
    std::uint32_t start = next_avail_;
    // Total bytes including the 0x1A 0x1A terminator, padded to a
    // whole block boundary with NULs.
    std::size_t needed = payload.size() + 2;
    std::size_t blocks = (needed + DBT_BLOCK - 1) / DBT_BLOCK;
    std::vector<std::uint8_t> buf(blocks * DBT_BLOCK, 0);
    std::memcpy(buf.data(), payload.data(), payload.size());
    buf[payload.size()]     = 0x1A;
    buf[payload.size() + 1] = 0x1A;

    std::uint64_t off = static_cast<std::uint64_t>(start) * DBT_BLOCK;
    auto wrote = file_.write_at(off, buf.data(), buf.size());
    if (!wrote) return wrote.error();
    if (wrote.value() != buf.size()) {
        return util::Error{5000, 0, "short write on DBT memo", ""};
    }
    next_avail_ = static_cast<std::uint32_t>(start + blocks);
    if (auto r = rewrite_header_(); !r) return r.error();
    return start;
}

util::Result<void> DbtMemo::free_block(std::uint32_t /*block_no*/) {
    // dBase III DBT does not maintain a free list; freed blocks become
    // dead space until the file is reorganised. No-op here matches that.
    return {};
}

util::Result<void> DbtMemo::flush() {
    return file_.sync();
}

util::Result<void> DbtMemo::rewrite_header_() {
    std::uint8_t hdr[DBT_BLOCK]{};
    write_u32_le(hdr, next_avail_);
    auto wrote = file_.write_at(0, hdr, DBT_BLOCK);
    if (!wrote) return wrote.error();
    return {};
}

util::Result<DbtMemo>
DbtMemo::create(const std::string& path) {
    auto fres = platform::File::open(path, platform::OpenMode::CreateRW);
    if (!fres) return fres.error();
    platform::File file = std::move(fres).value();

    std::uint8_t hdr[DBT_BLOCK]{};
    write_u32_le(hdr, 1);   // next available block
    auto wrote = file.write_at(0, hdr, DBT_BLOCK);
    if (!wrote) return wrote.error();
    if (auto s = file.sync(); !s) return s.error();

    DbtMemo m;
    m.file_       = std::move(file);
    m.mode_       = MemoOpenMode::Shared;
    m.next_avail_ = 1;
    return m;
}

} // namespace openads::drivers::dbt
