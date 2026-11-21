#include "engine/server_fs.h"
#include "openads/error.h"
#include "platform/fs_sandbox.h"

#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <fcntl.h>
#if defined(_WIN32)
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

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

util::Result<void> fs_copy(const std::string& abs_src,
                           const std::string& abs_dst) {
    std::error_code ec;
    if (!fs::is_regular_file(abs_src, ec) || ec)
        return io_err(static_cast<std::int32_t>(openads::AE_NO_FILE_FOUND),
                     "file not found", abs_src);
    bool same = fs::equivalent(abs_src, abs_dst, ec);
    if (!ec && same)
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "source and target are the same file", abs_src);
    // Plain stream copy: MinGW's fs::copy_file is unreliable when the
    // target already exists, and truncate-on-open is the wanted overwrite.
    std::ifstream in(abs_src, std::ios::binary);
    if (!in)
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "open source failed", abs_src);
    std::ofstream out(abs_dst, std::ios::binary | std::ios::trunc);
    if (!out)
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "open target failed", abs_dst);
    out << in.rdbuf();
    out.flush();
    if (!out || in.bad())
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "copy failed", abs_src);
    return {};
}

util::Result<std::vector<std::uint8_t>> fs_load(const std::string& abs_path) {
    std::error_code ec;
    if (!fs::is_regular_file(fs::path(abs_path), ec) || ec)
        return io_err(static_cast<std::int32_t>(openads::AE_NO_FILE_FOUND),
                      "not a regular file", abs_path);
    auto sz = fs::file_size(fs::path(abs_path), ec);
    if (ec)
        return io_err(static_cast<std::int32_t>(openads::AE_NO_FILE_FOUND),
                      "stat failed", abs_path);
    if (sz > kMaxFsWholeFile)
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "file too large for one call", abs_path);
    std::ifstream in(abs_path, std::ios::binary);
    if (!in)
        return io_err(static_cast<std::int32_t>(openads::AE_NO_FILE_FOUND),
                      "open failed", abs_path);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(sz));
    if (sz)
        in.read(reinterpret_cast<char*>(out.data()),
                static_cast<std::streamsize>(sz));
    if (in.bad() || static_cast<std::uint64_t>(in.gcount()) != sz)
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "read failed", abs_path);
    return out;
}

util::Result<void> fs_save(const std::string& abs_path,
                           const std::uint8_t* data, std::uint64_t size) {
    if (size > kMaxFsWholeFile)
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "too large for one call", abs_path);
    std::ofstream out(abs_path, std::ios::binary | std::ios::trunc);
    if (!out)
        return io_err(static_cast<std::int32_t>(openads::AE_ACCESS_DENIED),
                      "open failed", abs_path);
    if (size)
        out.write(reinterpret_cast<const char*>(data),
                  static_cast<std::streamsize>(size));
    out.flush();
    if (!out)
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "write failed", abs_path);
    return {};
}

util::Result<void> fs_set_attr(const std::string& abs_path,
                               std::uint32_t attr) {
    std::error_code ec;
    if (!fs::exists(fs::path(abs_path), ec) || ec)
        return io_err(static_cast<std::int32_t>(openads::AE_NO_FILE_FOUND),
                      "not found", abs_path);
    using fs::perms;
    if (attr & 0x01u) {
        fs::permissions(abs_path,
                        perms::owner_write | perms::group_write |
                            perms::others_write,
                        fs::perm_options::remove, ec);
    } else {
        fs::permissions(abs_path,
                        perms::owner_write | perms::group_write |
                            perms::others_write,
                        fs::perm_options::add, ec);
    }
    if (ec)
        return io_err(static_cast<std::int32_t>(openads::AE_ACCESS_DENIED),
                      "chmod failed", abs_path);
    return {};
}

util::Result<void> fs_set_time(const std::string& abs_path,
                               std::uint16_t year, std::uint8_t mon,
                               std::uint8_t day, std::uint8_t hh,
                               std::uint8_t mm, std::uint8_t ss) {
    std::error_code ec;
    if (!fs::exists(fs::path(abs_path), ec) || ec)
        return io_err(static_cast<std::int32_t>(openads::AE_NO_FILE_FOUND),
                      "not found", abs_path);
    std::tm tmv{};
    tmv.tm_year = static_cast<int>(year) - 1900;
    tmv.tm_mon  = static_cast<int>(mon) - 1;
    tmv.tm_mday = static_cast<int>(day);
    tmv.tm_hour = static_cast<int>(hh);
    tmv.tm_min  = static_cast<int>(mm);
    tmv.tm_sec  = static_cast<int>(ss);
    tmv.tm_isdst = -1;
    std::time_t tt = std::mktime(&tmv);
    if (tt == static_cast<std::time_t>(-1))
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "bad date", abs_path);
    auto sys = std::chrono::system_clock::from_time_t(tt);
    // C++17 file_time_type clock is not system_clock on every library:
    // shift from "now" instead of casting epochs.
    auto ftime = fs::file_time_type::clock::now() +
                 (sys - std::chrono::system_clock::now());
    fs::last_write_time(abs_path, ftime, ec);
    if (ec)
        return io_err(static_cast<std::int32_t>(openads::AE_ACCESS_DENIED),
                      "utime failed", abs_path);
    return {};
}

util::Result<void> fs_truncate(const std::string& abs_path,
                               std::uint64_t size) {
    std::error_code ec;
    fs::resize_file(fs::path(abs_path), size, ec);
    if (ec)
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "truncate failed", abs_path);
    return {};
}

util::Result<void> fs_truncate(FsFile& file, std::uint64_t size) {
    if (!file.writable)
        return io_err(static_cast<std::int32_t>(openads::AE_ACCESS_DENIED),
                      "read-only handle", file.path);
    file.stream.clear();
    auto pos = file.stream.tellg();
    file.stream.flush();
    if (!file.stream)
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "flush failed", file.path);
    auto r = fs_truncate(file.path, size);
    if (!r) return r;
    file.stream.clear();
    if (pos >= 0) {
        file.stream.seekg(pos);
        file.stream.seekp(pos);
    }
    return {};
}

util::Result<std::string> fs_tempfile(const std::string& abs_dir,
                                      const std::string& prefix) {
    std::error_code ec;
    if (!fs::is_directory(fs::path(abs_dir), ec) || ec)
        return io_err(static_cast<std::int32_t>(openads::AE_NO_FILE_FOUND),
                      "not a directory", abs_dir);
    std::string pfx = prefix.empty() ? "oa" : prefix;
    // Keep the prefix path-safe: one dot and no separators.
    for (auto& c : pfx)
        if (c == '/' || c == '\\' || c == ':') c = '_';
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int i = 0; i < 100; ++i) {
        std::ostringstream name;
        name << pfx << std::hex << (stamp + i) << ".tmp";
        fs::path cand = fs::path(abs_dir) / name.str();
        if (fs::exists(cand, ec)) continue;
        // Exclusive creation: never truncate another caller's file.
#if defined(_WIN32)
        int fd = ::_open(cand.string().c_str(),
                         _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
                         _S_IREAD | _S_IWRITE);
        if (fd < 0) continue;
        ::_close(fd);
#else
        int fd = ::open(cand.string().c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (fd < 0) continue;
        ::close(fd);
#endif
        return cand.string();
    }
    return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                  "no free temp name", abs_dir);
}

util::Result<std::uint64_t> fs_dir_space(const std::string& abs_dir,
                                         std::uint8_t type) {
    std::error_code ec;
    auto sp = fs::space(fs::path(abs_dir), ec);
    if (ec)
        return io_err(static_cast<std::int32_t>(openads::AE_NO_FILE_FOUND),
                      "space query failed", abs_dir);
    if (type == 1) return static_cast<std::uint64_t>(sp.capacity);
    if (type == 2) return static_cast<std::uint64_t>(sp.free);
    return static_cast<std::uint64_t>(sp.available);
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
    // A missing directory is an ordinary negative probe, not an I/O error.
    // std::filesystem::is_directory(path, ec) sets no_such_file_or_directory
    // in ec on some standard libraries (notably Linux/libstdc++).
    if (ec == std::errc::no_such_file_or_directory)
        return false;
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
    f->writable = create || (mode != ADS_FO_READ && mode != 3);
    std::ios::openmode om = std::ios::binary;
    if (create) {
        om |= std::ios::in | std::ios::out | std::ios::trunc;
    } else if (mode == ADS_FO_READ || mode == 3 /*ADS_READONLY*/) {
        // Two conventions reach here: Harbour-style FO_READ (0) and the
        // SAP ace.h ADS_READONLY (3) that oads_FOpen callers pass. Both
        // must open read-only so FWrite on the handle fails.
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
