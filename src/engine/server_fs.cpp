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
#include <algorithm>
#include <cerrno>
#include <unordered_map>
#include <limits>
#if defined(_WIN32)
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#include <sys/stat.h>
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
    auto src = fs_open(abs_src, ADS_FO_READ, false);
    if (!src) return src.error();
    auto dst = fs_open(abs_dst, ADS_FO_READWRITE, true);
    if (!dst) return dst.error();
    std::vector<char> chunk(65536);
    for (;;) {
        src.value()->stream.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        if (!src.value()->stream) return io_err(5000, "copy read failed", abs_src);
        auto n = src.value()->stream.gcount();
        if (n == 0) break;
        dst.value()->stream.write(chunk.data(), n);
        if (!dst.value()->stream) return io_err(5000, "copy write failed", abs_dst);
    }
    dst.value()->stream.flush();
    if (!dst.value()->stream) return io_err(5000, "copy flush failed", abs_dst);
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
    auto handle = fs_open(abs_path, ADS_FO_READ, false);
    if (!handle) return handle.error();
    auto& in = handle.value()->stream;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(sz));
    if (sz)
        in.read(reinterpret_cast<char*>(out.data()),
                static_cast<std::streamsize>(sz));
    if (!in || static_cast<std::uint64_t>(in.gcount()) != sz)
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "read failed", abs_path);
    return out;
}

util::Result<void> fs_save(const std::string& abs_path,
                           const std::uint8_t* data, std::uint64_t size) {
    if (size > kMaxFsWholeFile)
        return io_err(static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                      "too large for one call", abs_path);
    auto handle = fs_open(abs_path, ADS_FO_READWRITE, true);
    if (!handle) return handle.error();
    auto& out = handle.value()->stream;
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
    auto native = file.stream.native;
    std::lock_guard<std::mutex> guard(native->mu);
    auto r = native->file.truncate(size);
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
    // Harbour fileio.ch HB_DISK_* values, not a private numbering.
    if (type == 1) return static_cast<std::uint64_t>(sp.free);
    if (type == 2) return static_cast<std::uint64_t>(sp.capacity - sp.free);
    if (type == 3) return static_cast<std::uint64_t>(sp.capacity);
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

void FsStream::read(char* data, std::streamsize n) {
    count_ = 0;
    if (!good_ || n < 0) { good_ = false; return; }
    std::lock_guard<std::mutex> guard(native->mu);
    auto v = native->file.read_at(static_cast<std::uint64_t>(pos_), data, static_cast<std::size_t>(n));
    if (!v) { good_ = false; return; }
    count_ = static_cast<std::streamsize>(v.value());
    pos_ += static_cast<std::streamoff>(v.value());
}
void FsStream::write(const char* data, std::streamsize n) {
    if (!good_ || !writable || n < 0) { good_ = false; return; }
    std::lock_guard<std::mutex> guard(native->mu);
    auto v = native->file.write_at(static_cast<std::uint64_t>(pos_), data, static_cast<std::size_t>(n));
    if (!v) { good_ = false; return; }
    pos_ += static_cast<std::streamoff>(v.value());
    good_ = v.value() == static_cast<std::size_t>(n);
}
void FsStream::flush() {
    // Native positional I/O is unbuffered at this layer. Flush is not
    // Commit: like hb_vfFlush, it does not promise a durable disk sync.
}
void FsStream::seekg(std::streamoff off, std::ios::seekdir way) {
    std::streamoff base = 0;
    if (way == std::ios::cur) base = pos_;
    else if (way == std::ios::end) {
        std::lock_guard<std::mutex> guard(native->mu);
        auto n = native->file.size();
        if (!n || n.value() > 0x7FFFFFFFFFFFFFFFULL) { good_ = false; return; }
        base = static_cast<std::streamoff>(n.value());
    }
    if ((off > 0 && base > 0x7FFFFFFFFFFFFFFFLL - off) ||
        (off < 0 && off < -base)) { good_ = false; return; }
    pos_ = base + off;
}

namespace {
thread_local std::uint64_t fs_current_owner = 0;
std::mutex fs_files_mu;
std::unordered_map<std::string, std::weak_ptr<FsNative>> fs_files;
std::string fs_identity(const std::string& path) {
#if defined(_WIN32)
    HANDLE h = ::CreateFileA(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    BY_HANDLE_FILE_INFORMATION i{};
    bool ok = ::GetFileInformationByHandle(h, &i) != 0;
    ::CloseHandle(h);
    if (!ok) return {};
    return std::to_string(i.dwVolumeSerialNumber) + ":" +
           std::to_string(i.nFileIndexHigh) + ":" + std::to_string(i.nFileIndexLow);
#else
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return {};
    return std::to_string(st.st_dev) + ":" + std::to_string(st.st_ino);
#endif
}
std::int32_t own_lock_pid() {
#if defined(_WIN32)
    return 1;
#else
    return static_cast<std::int32_t>(::getpid());
#endif
}
util::Error native_lock_error(const char* msg, const std::string& path) {
#if defined(_WIN32)
    auto code = static_cast<std::int32_t>(::GetLastError());
#else
    auto code = errno;
#endif
    return util::Error{5013, code, msg, path};
}
}

FsOwnerScope::FsOwnerScope(std::uint64_t owner) : previous_(fs_current_owner) {
    fs_current_owner = owner;
}
FsOwnerScope::~FsOwnerScope() { fs_current_owner = previous_; }

util::Result<std::int32_t> fs_lock_step(FsFile& f, std::uint64_t start,
                                std::uint64_t len, std::uint16_t type,
                                std::uint8_t op) {
    if (op > 2 || (type & ~0x0300u) != 0 || start > 0x7FFFFFFFFFFFFFFFULL ||
        len > 0x7FFFFFFFFFFFFFFFULL - start)
        return io_err(5000, "invalid lock range or flags", f.path);
    auto state = f.stream.native;
    std::lock_guard<std::mutex> guard(state->mu);
    const auto end = len == 0 ? 0x8000000000000000ULL : start + len;
    auto overlap = [&](const FsRange& h) {
        const auto he = h.len == 0 ? 0x8000000000000000ULL : h.start + h.len;
        return start < he && h.start < end;
    };
    auto& ranges = state->ranges;
    // Like Harbour filebuf.c: test the merged process-local registrations
    // first. Re-locking an owned overlap is an error, even with WAIT.
    if (op != 1 && std::any_of(ranges.begin(), ranges.end(), overlap)) {
        if (op == 2) return own_lock_pid();
        return io_err(5012, "range already locked by this process", f.path);
    }
    auto containing = ranges.end();
    if (op == 1) {
        containing = std::find_if(ranges.begin(), ranges.end(), [&](const FsRange& h) {
            auto he = h.len == 0 ? 0x8000000000000000ULL : h.start + h.len;
            return start >= h.start && end <= he;
        });
        if (containing == ranges.end()) return io_err(5012, "unlock range not held", f.path);
    }
    bool shared = (type & 0x0100u) != 0;
#if !defined(_WIN32)
    // Harbour's Unix backend forces read-only opens to shared locks.
    if (!f.writable) shared = true;
#endif
#if defined(_WIN32)
    OVERLAPPED ov{};
    ov.Offset = static_cast<DWORD>(start);
    ov.OffsetHigh = static_cast<DWORD>(start >> 32);
    auto h = reinterpret_cast<HANDLE>(state->file.native_handle());
    auto lo = static_cast<DWORD>(len), hi = static_cast<DWORD>(len >> 32);
    if (op == 1) {
        if (!::UnlockFileEx(h, 0, lo, hi, &ov)) return native_lock_error("UnlockFileEx", f.path);
    } else {
        DWORD flags = LOCKFILE_FAIL_IMMEDIATELY | (shared ? 0 : LOCKFILE_EXCLUSIVE_LOCK);
        if (!::LockFileEx(h, flags, 0, lo, hi, &ov)) {
            if (::GetLastError() == ERROR_LOCK_VIOLATION) return op == 2 ? 1 : 0;
            return native_lock_error("LockFileEx", f.path);
        }
        if (op == 2) {
            if (!::UnlockFileEx(h, 0, lo, hi, &ov)) return native_lock_error("UnlockFileEx test", f.path);
            return 0;
        }
    }
#else
    int fd = static_cast<int>(reinterpret_cast<intptr_t>(state->file.native_handle()) - 1);
    struct flock fl{};
    fl.l_whence = SEEK_SET;
    fl.l_start = static_cast<off_t>(start);
    fl.l_len = static_cast<off_t>(len);
    fl.l_type = op == 1 ? F_UNLCK : shared ? F_RDLCK : F_WRLCK;
    fl.l_pid = 0;
    // OFD locks preserve ownership for separate clients in one server
    // process, and closing another client's descriptor cannot drop them.
#if defined(F_OFD_SETLK) && defined(F_OFD_GETLK)
    constexpr int set_cmd = F_OFD_SETLK;
    constexpr int get_cmd = F_OFD_GETLK;
#else
#error "FS client-owned locks require OFD byte-range lock support on this POSIX platform"
#endif
    if (op == 2) {
        if (::fcntl(fd, get_cmd, &fl) != 0) return native_lock_error("F_OFD_GETLK", f.path);
        // POSIX reports -1 for an OFD owner: no process ID exists there.
        return fl.l_type == F_UNLCK ? 0 : fl.l_pid > 0 ? static_cast<std::int32_t>(fl.l_pid) : 1;
    }
    if (::fcntl(fd, set_cmd, &fl) != 0) {
        if (op == 0 && (errno == EAGAIN || errno == EACCES)) return 0;
        return native_lock_error("F_SETLK", f.path);
    }
#endif
    if (op == 1) {
        const auto hs = containing->start;
        const auto he = containing->len == 0 ? 0x8000000000000000ULL : hs + containing->len;
        auto idx = static_cast<std::size_t>(containing - ranges.begin());
        ranges.erase(containing);
        if (hs < start) ranges.insert(ranges.begin() + static_cast<std::ptrdiff_t>(idx++), {hs, start-hs});
        if (end < he) ranges.insert(ranges.begin() + static_cast<std::ptrdiff_t>(idx),
                                    {end, he == 0x8000000000000000ULL ? 0 : he-end});
    } else {
        ranges.push_back({start,len});
        std::sort(ranges.begin(), ranges.end(), [](const FsRange& a, const FsRange& b) { return a.start < b.start; });
        for (std::size_t i = 1; i < ranges.size();) {
            auto& prev = ranges[i-1];
            if (prev.len != 0 && prev.start + prev.len == ranges[i].start) {
                prev.len = ranges[i].len == 0 ? 0 : prev.len + ranges[i].len;
                ranges.erase(ranges.begin() + static_cast<std::ptrdiff_t>(i));
            } else ++i;
        }
    }
    return 1;
}

util::Result<std::unique_ptr<FsFile>> fs_open(const std::string& abs_path,
                                              std::uint16_t mode, bool create) {
    auto f = std::make_unique<FsFile>();
    f->path = abs_path;
    f->writable = create || (mode != ADS_FO_READ && mode != 3);
    std::lock_guard<std::mutex> global(fs_files_mu);
    auto owner_key = [&](const std::string& identity) {
        return identity.empty() ? identity : identity + ":owner:" + std::to_string(fs_current_owner);
    };
    auto key = owner_key(fs_identity(abs_path));
    std::shared_ptr<FsNative> state;
    if (!key.empty()) state = fs_files[key].lock();
    if (state) {
        std::lock_guard<std::mutex> guard(state->mu);
        if (f->writable && !state->writable) {
            // Win32 lock ownership is per native handle. Never switch away
            // from a handle that still holds byte ranges.
            if (!state->ranges.empty()) return io_err(5012, "locked read-only file cannot upgrade", abs_path);
            auto upgraded = platform::File::open(abs_path, platform::OpenMode::ReadWrite);
            if (!upgraded) return upgraded.error();
            state->retained.push_back(std::move(state->file));
            state->file = std::move(upgraded.value());
            state->writable = true;
        }
        if (create) {
            auto v = state->file.truncate(0);
            if (!v) return v.error();
        }
    } else {
        auto native = platform::File::open(abs_path, create ? platform::OpenMode::CreateRW :
                     f->writable ? platform::OpenMode::ReadWrite : platform::OpenMode::ReadOnly);
        if (!native && create) {
            std::error_code ec;
            fs::create_directories(fs::path(abs_path).parent_path(), ec);
            native = platform::File::open(abs_path, platform::OpenMode::CreateRW);
        }
        if (!native) return native.error();
        state = std::make_shared<FsNative>();
        state->file = std::move(native.value());
        state->writable = f->writable;
        key = owner_key(fs_identity(abs_path));
        if (!key.empty()) fs_files[key] = state;
        // Remove expired weak entries so file churn does not retain names.
        for (auto it = fs_files.begin(); it != fs_files.end();)
            if (it->second.expired()) it = fs_files.erase(it); else ++it;
    }
    f->stream.native = std::move(state);
    f->stream.writable = f->writable;
    return f;
}

} // namespace openads::engine
