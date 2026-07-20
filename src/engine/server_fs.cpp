#include "engine/server_fs.h"
#include "openads/error.h"
#include "platform/fs_sandbox.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace openads::engine {

namespace {

util::Error io_err(std::int32_t code, const char* msg,
                   const std::string& path) {
    return util::Error{code, 0, msg, path};
}

void fill_mtime(DirEntry& e, const fs::file_time_type& ftime) {
    using namespace std::chrono;
    auto sctp = time_point_cast<system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + system_clock::now());
    std::time_t tt = system_clock::to_time_t(sctp);
#if defined(_WIN32)
    std::tm tm_buf{};
    localtime_s(&tm_buf, &tt);
    const std::tm* tm = &tm_buf;
#else
    std::tm tm_buf{};
    localtime_r(&tt, &tm_buf);
    const std::tm* tm = &tm_buf;
#endif
    e.year = static_cast<std::uint16_t>(tm->tm_year + 1900);
    e.mon  = static_cast<std::uint8_t>(tm->tm_mon + 1);
    e.day  = static_cast<std::uint8_t>(tm->tm_mday);
    e.hh   = static_cast<std::uint8_t>(tm->tm_hour);
    e.mm   = static_cast<std::uint8_t>(tm->tm_min);
    e.ss   = static_cast<std::uint8_t>(tm->tm_sec);
}

void write_u16(std::vector<std::uint8_t>& o, std::uint16_t v) {
    o.push_back(static_cast<std::uint8_t>(v & 0xFF));
    o.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}
void write_u32(std::vector<std::uint8_t>& o, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        o.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}
void write_u64(std::vector<std::uint8_t>& o, std::uint64_t v) {
    for (int i = 0; i < 8; ++i)
        o.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}
std::uint16_t read_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
std::uint32_t read_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint64_t read_u64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    return v;
}

} // namespace

void pack_dir_entry(const DirEntry& e, std::vector<std::uint8_t>& out) {
    auto n = static_cast<std::uint16_t>(
        e.name.size() > 0xFFFF ? 0xFFFF : e.name.size());
    write_u16(out, n);
    out.insert(out.end(), e.name.begin(), e.name.begin() + n);
    write_u64(out, e.size);
    write_u16(out, e.year);
    out.push_back(e.mon);
    out.push_back(e.day);
    out.push_back(e.hh);
    out.push_back(e.mm);
    out.push_back(e.ss);
    write_u32(out, e.attr);
}

bool unpack_dir_entry(const std::vector<std::uint8_t>& pl, std::size_t& off,
                      DirEntry& e) {
    if (off + 2 > pl.size()) return false;
    auto n = read_u16(pl.data() + off);
    off += 2;
    if (off + n + 8 + 2 + 5 + 4 > pl.size()) return false;
    e.name.assign(reinterpret_cast<const char*>(pl.data() + off), n);
    off += n;
    e.size = read_u64(pl.data() + off);
    off += 8;
    e.year = read_u16(pl.data() + off);
    off += 2;
    e.mon = pl[off++];
    e.day = pl[off++];
    e.hh  = pl[off++];
    e.mm  = pl[off++];
    e.ss  = pl[off++];
    e.attr = read_u32(pl.data() + off);
    off += 4;
    return true;
}

util::Result<bool> fs_exists(const std::string& abs_path) {
    std::error_code ec;
    bool ex = fs::exists(abs_path, ec);
    if (ec) return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                  "exists failed", abs_path);
    return ex;
}

util::Result<void> fs_erase(const std::string& abs_path) {
    std::error_code ec;
    if (!fs::exists(abs_path, ec) || ec)
        return io_err(static_cast<std::int32_t>(openads::AE_NO_FILE_FOUND),
                     "file not found", abs_path);
    if (fs::is_directory(abs_path, ec))
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "is directory", abs_path);
    if (!fs::remove(abs_path, ec) || ec)
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "erase failed", abs_path);
    return {};
}

util::Result<void> fs_rename(const std::string& abs_old,
                             const std::string& abs_new) {
    std::error_code ec;
    if (!fs::exists(abs_old, ec) || ec)
        return io_err(static_cast<std::int32_t>(openads::AE_NO_FILE_FOUND),
                     "file not found", abs_old);
    fs::rename(abs_old, abs_new, ec);
    if (ec)
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "rename failed", abs_old);
    return {};
}

util::Result<std::uint64_t> fs_size(const std::string& abs_path) {
    std::error_code ec;
    if (!fs::is_regular_file(abs_path, ec) || ec)
        return io_err(static_cast<std::int32_t>(openads::AE_NO_FILE_FOUND),
                     "not a file", abs_path);
    auto sz = fs::file_size(abs_path, ec);
    if (ec)
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "size failed", abs_path);
    return static_cast<std::uint64_t>(sz);
}

util::Result<DirEntry> fs_stat_entry(const std::string& abs_path) {
    std::error_code ec;
    if (!fs::exists(abs_path, ec) || ec)
        return io_err(static_cast<std::int32_t>(openads::AE_NO_FILE_FOUND),
                     "not found", abs_path);
    DirEntry e;
    e.name = fs::path(abs_path).filename().string();
    if (fs::is_directory(abs_path, ec)) {
        e.attr = 0x10;
        e.size = 0;
    } else {
        e.size = static_cast<std::uint64_t>(fs::file_size(abs_path, ec));
        if (ec) e.size = 0;
        auto perms = fs::status(abs_path, ec).permissions();
        if ((perms & fs::perms::owner_write) == fs::perms::none)
            e.attr |= 0x01;
    }
    auto ft = fs::last_write_time(abs_path, ec);
    if (!ec) fill_mtime(e, ft);
    return e;
}

util::Result<std::vector<DirEntry>> fs_directory(const std::string& base_dir,
                                                 const std::string& mask) {
    namespace fs = std::filesystem;
    std::string dir = base_dir;
    std::string pat = mask.empty() ? "*" : mask;
    // Split directory prefix from mask.
    auto slash = pat.find_last_of("/\\");
    if (slash != std::string::npos) {
        auto sub = pat.substr(0, slash);
        pat = pat.substr(slash + 1);
        if (pat.empty()) pat = "*";
        fs::path combined = fs::path(base_dir) / sub;
        dir = combined.lexically_normal().string();
    }
    std::error_code ec;
    if (!fs::is_directory(dir, ec) || ec)
        return io_err(static_cast<std::int32_t>(openads::AE_NO_FILE_FOUND),
                     "not a directory", dir);

    std::vector<DirEntry> out;
    for (const auto& ent : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        const auto name = ent.path().filename().string();
        if (!openads::platform::match_wildcard(name, pat)) continue;
        auto st = fs_stat_entry(ent.path().string());
        if (!st) continue;
        DirEntry e = st.value();
        e.name = name;
        out.push_back(std::move(e));
    }
    return out;
}

util::Result<bool> fs_dir_exist(const std::string& abs_path) {
    std::error_code ec;
    bool d = fs::is_directory(abs_path, ec);
    if (ec)
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "dir exist failed", abs_path);
    return d;
}

util::Result<void> fs_dir_make(const std::string& abs_path) {
    std::error_code ec;
    if (fs::exists(abs_path, ec) && fs::is_directory(abs_path, ec))
        return {};
    if (!fs::create_directories(abs_path, ec) || ec)
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "mkdir failed", abs_path);
    return {};
}

util::Result<void> fs_dir_remove(const std::string& abs_path) {
    std::error_code ec;
    if (!fs::is_directory(abs_path, ec) || ec)
        return io_err(static_cast<std::int32_t>(openads::AE_NO_FILE_FOUND),
                      "not a directory", abs_path);
    if (!fs::is_empty(abs_path, ec) || ec)
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "directory not empty", abs_path);
    if (!fs::remove(abs_path, ec) || ec)
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "rmdir failed", abs_path);
    return {};
}

util::Result<std::unique_ptr<FsFile>> fs_open(const std::string& abs_path,
                                              std::uint16_t mode,
                                              bool create) {
    auto f = std::make_unique<FsFile>();
    f->path = abs_path;
    std::ios::openmode om = std::ios::binary;
    if (create) {
        om |= std::ios::in | std::ios::out | std::ios::trunc;
    } else if (mode == ADS_FO_READ) {
        om |= std::ios::in;
    } else if (mode == ADS_FO_WRITE) {
        om |= std::ios::in | std::ios::out;
    } else {
        om |= std::ios::in | std::ios::out;
    }
    f->stream.open(abs_path, om);
    if (!f->stream) {
        // Try create parent path for FCreate.
        if (create) {
            std::error_code ec;
            fs::create_directories(fs::path(abs_path).parent_path(), ec);
            f->stream.clear();
            f->stream.open(abs_path, om);
        }
    }
    if (!f->stream)
        return io_err(static_cast<std::int32_t>(
                          create ? openads::AE_INTERNAL_ERROR
                                 : openads::AE_NO_FILE_FOUND),
                      create ? "create failed" : "open failed", abs_path);
    return f;
}

} // namespace openads::engine
