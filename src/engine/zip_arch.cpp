// Server-side ZIP/UNZIP — see zip_arch.h. Classic minizip over
// std::fstream streaming (128 KiB chunks; no whole-file buffering).

#include "engine/zip_arch.h"

#include "openads/error.h"
#include "platform/fs_sandbox.h"
#include "platform/path.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <random>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "zip.h"
#include "unzip.h"
#include "zlib.h"

namespace fs = std::filesystem;

namespace openads::engine::zip_arch {
namespace {

constexpr std::size_t kChunk = 128u * 1024u;

util::Error make_error(std::int32_t code, const std::string& msg) {
    util::Error e;
    e.code = code;
    e.message = msg;
    return e;
}

// Keep the old destination untouched until streaming, length and CRC checks
// finish. An exclusively created sibling directory isolates the staging file.
struct StagedOutput {
    fs::path directory, file;
    bool create(const fs::path& target, std::error_code& ec) {
        try {
            std::random_device random;
            for (int attempt = 0; attempt < 32; ++attempt) {
                const fs::path parent = target.has_parent_path() ? target.parent_path()
                                                                 : fs::path(".");
                const fs::path candidate = parent /
                    (".openads-zip-stage-" + std::to_string(random()) + "-" +
                     std::to_string(random()));
                if (fs::create_directory(candidate, ec)) {
                    directory = candidate;
                    fs::permissions(directory, fs::perms::owner_all,
                                    fs::perm_options::replace, ec);
                    if (ec) return false;
                    file = directory / "output";
                    return true;
                }
                if (ec && ec != std::errc::file_exists) return false;
            }
        } catch (...) {
            ec = std::make_error_code(std::errc::io_error);
        }
        return false;
    }
    ~StagedOutput() {
        if (!directory.empty()) {
            std::error_code ignored;
            fs::remove(file, ignored);
            fs::remove(directory, ignored);
        }
    }
    bool publish(const fs::path& target, bool overwrite, std::error_code& ec) {
#ifdef _WIN32
        const DWORD flags = overwrite ? MOVEFILE_REPLACE_EXISTING : 0;
        if (!MoveFileExW(file.c_str(), target.c_str(), flags)) {
            ec = std::error_code(static_cast<int>(GetLastError()),
                                 std::system_category());
            return false;
        }
        ec.clear();
#else
        if (overwrite) {
            fs::rename(file, target, ec);
        } else {
            // rename() on POSIX replaces a concurrently created target.
            // link() installs the completed inode only when target is absent.
            fs::create_hard_link(file, target, ec);
            if (!ec) {
                std::error_code ignored;
                fs::remove(file, ignored);
            }
        }
        if (ec) return false;
#endif
        return true;
    }
};

struct Budget {
    Limits limits;
    std::uint64_t entries = 0, bytes = 0, metadata = 0;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    explicit Budget(const Limits& l) : limits(l) {}
    bool timed_out() const {
        return limits.milliseconds && std::chrono::steady_clock::now() - start >=
            std::chrono::milliseconds(limits.milliseconds);
    }
    bool add(std::uint64_t n, std::uint64_t m) {
        if (timed_out() || entries >= limits.entries || n > limits.bytes - bytes ||
            m > limits.metadata - metadata) return false;
        ++entries; bytes += n; metadata += m; return true;
    }
};

// Bundled minizip does not validate the traditional encryption header.
// Read it in raw mode, check its verification byte, then reopen normally.
// This is ZIP's weak 8-bit check, not authenticated encryption.
bool password_header_ok(unzFile uf, const unz_file_info64& info,
                        const std::string& password) {
    if (!(info.flag & 1)) return true;
    if (password.empty() || (info.flag & 64) || info.compressed_size < 12)
        return false;
    if (unzOpenCurrentFile3(uf, nullptr, nullptr, 1, nullptr) != UNZ_OK)
        return false;
    unsigned char header[12]{};
    const int got = unzReadCurrentFile(uf, header, sizeof(header));
    const int closed = unzCloseCurrentFile(uf);
    if (got != 12 || closed != UNZ_OK) return false;
    const z_crc_t* pcrc_32_tab = get_crc_table();
    std::uint32_t keys[3] = {305419896u, 591751049u, 878082192u};
    const auto update = [&](unsigned char c) {
        keys[0] = pcrc_32_tab[(keys[0] ^ c) & 0xff] ^ (keys[0] >> 8);
        keys[1] = (keys[1] + (keys[0] & 0xff)) * 134775813u + 1u;
        keys[2] = pcrc_32_tab[(keys[2] ^ (keys[1] >> 24)) & 0xff] ^
                  (keys[2] >> 8);
    };
    for (char c : password) update(static_cast<unsigned char>(c));
    for (auto& c : header) {
        const std::uint32_t t = (keys[2] & 0xffff) | 2u;
        c ^= static_cast<unsigned char>((t * (t ^ 1u)) >> 8);
        update(c);
    }
    const unsigned check = (info.flag & 8) ? (info.dosDate >> 8) & 0xff
                                          : (info.crc >> 24) & 0xff;
    return header[11] == check;
}

// Read the complete central-directory name. Minizip silently truncates
// caller buffers; never let a truncated spelling choose an output path.
bool read_entry_name(unzFile uf, unz_file_info64& info, std::vector<char>& buffer,
                     std::string& name) {
    if (unzGetCurrentFileInfo64(uf, &info, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK ||
        info.size_filename == 0 || info.size_filename > 0xFFFFu) return false;
    buffer.assign(static_cast<std::size_t>(info.size_filename) + 1, 0);
    if (unzGetCurrentFileInfo64(uf, &info, buffer.data(),
        static_cast<uLong>(buffer.size()), nullptr, 0, nullptr, 0) != UNZ_OK) return false;
    name.assign(buffer.data(), static_cast<std::size_t>(info.size_filename));
    if (name.find('\0') != std::string::npos) return false;
    for (auto& ch : name) if (ch == '\\') ch = '/';
    return true;
}

// File mtime -> minizip dosDate. Thread-safe localtime.
uLong file_dos_date(const fs::path& p) {
    std::error_code ec;
    const auto ftime = fs::last_write_time(p, ec);
    if (ec) return 0;
    const auto systime = std::chrono::time_point_cast<
        std::chrono::system_clock::duration>(
        ftime - fs::file_time_type::clock::now() +
        std::chrono::system_clock::now());
    const std::time_t tt =
        std::chrono::system_clock::to_time_t(systime);
    std::tm tmv{};
#if defined(_WIN32)
    if (localtime_s(&tmv, &tt) != 0) return 0;
#else
    if (localtime_r(&tt, &tmv) == nullptr) return 0;
#endif
    const int year = tmv.tm_year + 1900;
    if (year < 1980) return 0;
    return static_cast<uLong>(
        (static_cast<unsigned>(year - 1980) << 25) |
        (static_cast<unsigned>(tmv.tm_mon + 1) << 21) |
        (static_cast<unsigned>(tmv.tm_mday) << 16) |
        (static_cast<unsigned>(tmv.tm_hour) << 11) |
        (static_cast<unsigned>(tmv.tm_min) << 5) |
        (static_cast<unsigned>(tmv.tm_sec) >> 1));
}

// Zip entry names always use '/'. Reject absolute paths, drive
// letters and ".." components (Zip-Slip) — defense in depth on top
// of the caller's jail.
bool entry_name_ok(const std::string& entry) {
    if (entry.empty()) return false;
    if (entry[0] == '/' || entry[0] == '\\') return false;
    if (entry.size() >= 2 && entry[1] == ':') return false;
    std::string part;
    for (std::size_t i = 0; i <= entry.size(); ++i) {
        const char c = (i < entry.size()) ? entry[i] : '/';
        if (c == '/' || c == '\\') {
            if (part == "..") return false;
            part.clear();
        } else {
            part.push_back(c);
        }
    }
    return true;
}

std::string to_entry_seps(std::string s) {
    for (char& c : s)
        if (c == '\\') c = '/';
    return s;
}

std::string base_name_of(const std::string& p) {
    const std::size_t n = p.find_last_of("/\\");
    return (n == std::string::npos) ? p : p.substr(n + 1);
}

// srcdir-relative entry, or basename when !with_path. Empty when the
// file is outside srcdir (caller error — all inputs are jailed, but
// never trust: fail loud instead of storing a bare name silently).
std::string entry_for(const std::string& abs, const std::string& src_dir,
                      bool with_path) {
    if (!with_path) return base_name_of(abs);
    std::string rel = abs;
    std::string root = src_dir;
    if (!root.empty() && (root.back() == '/' || root.back() == '\\'))
        root.pop_back();
    if (rel.size() > root.size() &&
        (rel[root.size()] == '/' || rel[root.size()] == '\\') &&
        rel.compare(0, root.size(), root) == 0)
        return to_entry_seps(rel.substr(root.size() + 1));
    return std::string();
}

bool excluded(const std::string& abs,
              const std::vector<std::string>& masks) {
    const std::string base = base_name_of(abs);
    for (const auto& m : masks)
        if (platform::match_wildcard(base, m)) return true;
    return false;
}

}  // namespace

util::Result<Stats> zip_files(const std::vector<std::string>& abs_files,
                              const std::string& src_dir_abs,
                              const std::string& archive_abs,
                              const ZipOptions& opt) {
    if (abs_files.empty())
        return make_error(openads::AE_INTERNAL_ERROR,
                          "zip: empty file list");
    if (opt.level < 0 || opt.level > 9)
        return make_error(openads::AE_INTERNAL_ERROR,
                          "zip: level must be 0..9");
    std::error_code ec;
    if (fs::exists(archive_abs, ec) && !ec) {
        if (!opt.overwrite)
            return make_error(openads::AE_INTERNAL_ERROR,
                              "zip: archive exists (overwrite off): " +
                                  archive_abs);
    }
    // Stage files first (existence + entry names) so a missing file
    // fails BEFORE any archive is created.
    struct Job {
        std::string abs;
        std::string entry;
        std::uint64_t size = 0;
    };
    std::vector<Job> jobs;
    Budget budget(opt.limits);
    for (const auto& f : abs_files) {
        if (excluded(f, opt.exclude)) continue;
        if (!fs::is_regular_file(f, ec) || ec)
            return make_error(openads::AE_NO_FILE_FOUND,
                              "zip: source not found: " + f);
        const std::string entry = entry_for(f, src_dir_abs, opt.with_path);
        if (!entry_name_ok(entry))
            return make_error(openads::AE_ACCESS_DENIED,
                              "zip: refusing entry name: " + entry);
        const std::uint64_t sz =
            static_cast<std::uint64_t>(fs::file_size(f, ec));
        if (ec)
            return make_error(openads::AE_INTERNAL_ERROR,
                              "zip: cannot size source: " + f);
        if (!budget.add(sz, entry.size()))
            return make_error(openads::AE_ACCESS_DENIED, "zip: resource budget exceeded");
        jobs.push_back({f, entry, sz});
    }
    if (jobs.empty())
        return make_error(openads::AE_NO_MATCHING_FILE,
                          "zip: nothing left after excludes");

    StagedOutput staged;
    if (!staged.create(fs::path(archive_abs), ec))
        return make_error(openads::AE_INTERNAL_ERROR,
                          "zip: cannot create staging directory: " + archive_abs);
    zipFile zf = zipOpen64(staged.file.string().c_str(), APPEND_STATUS_CREATE);
    if (zf == nullptr)
        return make_error(openads::AE_INTERNAL_ERROR,
                          "zip: cannot create staged archive: " + archive_abs);
    Stats st;
    std::vector<char> buf(kChunk);
    std::string fail;
    for (const auto& j : jobs) {
        if (budget.timed_out()) { fail = "zip: execution deadline"; break; }
        zip_fileinfo zi{};
        zi.dosDate = file_dos_date(j.abs);
        const char* pwd = opt.password.empty() ? nullptr
                                               : opt.password.c_str();
        const int method = (opt.level == 0) ? 0 : Z_DEFLATED;
        // ZipCrypto verifies the password against the file CRC on
        // extract — stream once for the CRC when encrypting.
        uLong crc = 0;
        if (pwd != nullptr) {
            std::ifstream crc_in(j.abs, std::ios::binary);
            if (!crc_in) {
                fail = "zip: cannot read source: " + j.abs;
                break;
            }
            uLong running = crc32(0L, Z_NULL, 0);
            std::uint64_t seen = 0;
            while (crc_in) {
                if (budget.timed_out()) { fail = "zip: execution deadline"; break; }
                crc_in.read(buf.data(),
                            static_cast<std::streamsize>(buf.size()));
                const std::streamsize got = crc_in.gcount();
                if (got > 0) seen += static_cast<std::uint64_t>(got);
                if (seen > j.size) { fail = "zip: source grew during archive"; break; }
                if (got > 0)
                    running = crc32(running,
                                    reinterpret_cast<const Bytef*>(
                                        buf.data()),
                                    static_cast<uInt>(got));
            }
            if (!crc_in.eof() && crc_in.fail()) {
                fail = "zip: cannot read source: " + j.abs;
                break;
            }
            if (!fail.empty()) break;
            crc = running;
        }
        // Zip64 only when the source cannot fit 32 bits (keeps
        // archives maximally compatible otherwise).
        const int use_zip64 = (j.size >= 0xFFFFFFFFu) ? 1 : 0;
        int rc = zipOpenNewFileInZip3_64(
            zf, j.entry.c_str(), &zi, nullptr, 0, nullptr, 0, nullptr,
            method, opt.level, 0, -MAX_WBITS, DEF_MEM_LEVEL,
            Z_DEFAULT_STRATEGY, pwd, crc, use_zip64);
        if (rc != ZIP_OK) {
            fail = "zip: cannot add entry: " + j.entry;
            break;
        }
        std::ifstream in(j.abs, std::ios::binary);
        if (!in) {
            zipCloseFileInZip(zf);
            fail = "zip: cannot read source: " + j.abs;
            break;
        }
        bool werr = false;
        std::uint64_t seen = 0;
        while (in) {
            if (budget.timed_out()) { werr = true; break; }
            in.read(buf.data(),
                    static_cast<std::streamsize>(buf.size()));
            const std::streamsize got = in.gcount();
            if (got > 0) seen += static_cast<std::uint64_t>(got);
            if (seen > j.size) { werr = true; break; }
            if (got > 0 &&
                zipWriteInFileInZip(zf, buf.data(),
                                    static_cast<unsigned>(got)) != ZIP_OK) {
                werr = true;
                break;
            }
        }
        if (zipCloseFileInZip(zf) != ZIP_OK) werr = true;
        if (!in.eof() && in.fail()) werr = true;
        if (werr) {
            fail = "zip: write failed for entry: " + j.entry;
            break;
        }
        ++st.files;
        st.bytes += j.size;
    }
    if (zipClose(zf, nullptr) != ZIP_OK && fail.empty())
        fail = "zip: cannot finalize archive: " + archive_abs;
    if (!fail.empty()) {
        return make_error(openads::AE_INTERNAL_ERROR, fail);
    }
    if (!staged.publish(fs::path(archive_abs), opt.overwrite, ec))
        return make_error(openads::AE_INTERNAL_ERROR,
                          "zip: cannot publish archive: " + archive_abs);
    st.archive_bytes =
        static_cast<std::uint64_t>(fs::file_size(archive_abs, ec));
    if (ec) st.archive_bytes = 0;
    return st;
}

util::Result<Stats> unzip_files(const std::string& archive_abs,
                                const std::string& dest_dir_abs,
                                const UnzipOptions& opt) {
    std::error_code ec;
    if (!fs::is_regular_file(archive_abs, ec) || ec)
        return make_error(openads::AE_NO_FILE_FOUND,
                          "unzip: archive not found: " + archive_abs);
    fs::create_directories(dest_dir_abs, ec);
    if (ec)
        return make_error(openads::AE_INTERNAL_ERROR,
                          "unzip: cannot create destination: " +
                              dest_dir_abs);
    unzFile uf = unzOpen64(archive_abs.c_str());
    if (uf == nullptr)
        return make_error(openads::AE_TABLE_CORRUPTED,
                          "unzip: not a readable archive: " + archive_abs);
    Stats st;
    std::string fail;
    std::int32_t fail_code = openads::AE_INTERNAL_ERROR;
    std::vector<char> namebuf(1024);
    std::vector<char> buf(kChunk);
    // Basenames already extracted (flat mode collision guard).
    std::unordered_set<std::string> flat_seen;
    Budget budget(opt.limits);
    int go = unzGoToFirstFile(uf);
    while (go == UNZ_OK) {
        unz_file_info64 info{};
        std::string entry;
        if (!read_entry_name(uf, info, namebuf, entry)) {
            fail = "unzip: cannot read entry info";
            break;
        }
        if (!entry_name_ok(entry)) {
            fail = "unzip: refusing entry name: " + entry;
            fail_code = openads::AE_ACCESS_DENIED;
            break;
        }
        if (!budget.add(info.uncompressed_size, entry.size() + 64ull)) {
            fail = "unzip: resource budget exceeded";
            fail_code = openads::AE_ACCESS_DENIED; break;
        }
        const bool is_dir = !entry.empty() && entry.back() == '/';
        std::string out_rel = entry;
        if (!opt.with_path) {
            while (!out_rel.empty() && out_rel.back() == '/')
                out_rel.pop_back();
            out_rel = base_name_of(out_rel);
            if (!is_dir) {
                if (flat_seen.count(out_rel)) {
                    fail = "unzip: flat-mode name collision: " +
                           out_rel + " (extract with paths)";
                    break;
                }
            }
        }
        // Lexical entry checks alone do not catch an existing destination
        // symlink (including a symlinked parent). Resolve the actual target
        // under the extraction root before creating or opening anything.
        auto jailed = platform::resolve_under_root(dest_dir_abs, out_rel);
        if (!jailed) {
            fail = "unzip: target outside extraction directory: " + entry;
            fail_code = openads::AE_ACCESS_DENIED;
            break;
        }
        const fs::path out_path = *jailed;
        if (is_dir) {
            fs::create_directories(out_path, ec);
            if (ec) {
                fail = "unzip: cannot create directory: " +
                       out_path.string();
                break;
            }
        } else {
            if (fs::exists(out_path, ec) && !ec && !opt.overwrite) {
                fail = "unzip: target exists (overwrite off): " +
                       out_path.string();
                break;
            }
            fs::create_directories(out_path.parent_path(), ec);
            if (ec) {
                fail = "unzip: cannot create directory: " +
                       out_path.parent_path().string();
                break;
            }
            if (!password_header_ok(uf, info, opt.password)) {
                fail = "unzip: invalid or missing encryption password: " + entry;
                break;
            }
            const char* pwd = (info.flag & 1) ? opt.password.c_str() : nullptr;
            if (unzOpenCurrentFilePassword(uf, pwd) != UNZ_OK) {
                fail = "unzip: cannot open entry (bad password?): " +
                       entry;
                break;
            }
            StagedOutput staged;
            if (!staged.create(out_path, ec)) {
                unzCloseCurrentFile(uf);
                fail = "unzip: cannot create staging directory: " + out_path.string();
                break;
            }
            // Close the staging stream before publish/cleanup on Windows.
            bool rerr = false;
            std::uint64_t extracted = 0;
            {
                std::ofstream out(staged.file, std::ios::binary |
                                                std::ios::trunc);
                if (!out) {
                    unzCloseCurrentFile(uf);
                    fail = "unzip: cannot write target: " +
                           out_path.string();
                    break;
                }
                for (;;) {
                    if (budget.timed_out()) {
                        fail_code = openads::AE_ACCESS_DENIED;
                        fail = "unzip: execution deadline";
                        rerr = true; break;
                    }
                    const int got = unzReadCurrentFile(
                        uf, buf.data(),
                        static_cast<unsigned>(buf.size()));
                    if (got < 0) {
                        rerr = true;
                        break;
                    }
                    if (got == 0) break;
                    extracted += static_cast<std::uint64_t>(got);
                    if (extracted > info.uncompressed_size) { rerr = true; break; }
                    out.write(buf.data(), got);
                    if (!out) {
                        rerr = true;
                        break;
                    }
                }
                out.close();
                if (!out) rerr = true;
            }
            // Some bad-password deflate streams end early. minizip checks CRC
            // only when its expected-length counter reaches zero, so a zero
            // read alone is not proof of successful extraction.
            if (extracted != info.uncompressed_size) rerr = true;
            // NB: unzCloseCurrentFile surfaces the CRC check.
            if (unzCloseCurrentFile(uf) != UNZ_OK) rerr = true;
            if (rerr) {
                if (fail.empty()) fail = "unzip: entry failed (bad password or "
                       "corrupt data): " + entry;
                break;
            }
            // Recheck the destination after reading untrusted contents. This
            // narrows replacement races but does not pin ancestor directories.
            auto final_target = platform::resolve_under_root(dest_dir_abs, out_rel);
            if (!final_target || fs::path(*final_target) != out_path) {
                fail_code = openads::AE_ACCESS_DENIED;
                fail = "unzip: target changed during extraction: " + entry;
                break;
            }
            if (!staged.publish(out_path, opt.overwrite, ec)) {
                fail = "unzip: cannot publish target: " + out_path.string();
                break;
            }
            if (!opt.with_path) flat_seen.insert(out_rel);
            ++st.files;
            st.bytes += static_cast<std::uint64_t>(info.uncompressed_size);
        }
        go = unzGoToNextFile(uf);
    }
    if (fail.empty() && go != UNZ_END_OF_LIST_OF_FILE)
        fail = "unzip: archive walk failed";
    unzClose(uf);
    if (!fail.empty())
        return make_error(fail_code, fail);
    st.archive_bytes =
        static_cast<std::uint64_t>(fs::file_size(archive_abs, ec));
    if (ec) st.archive_bytes = 0;
    return st;
}

util::Result<std::vector<std::string>> list_entries(
    const std::string& archive_abs, const Limits& limits) {
    std::error_code ec;
    if (!fs::is_regular_file(archive_abs, ec) || ec)
        return make_error(openads::AE_NO_FILE_FOUND,
                          "unzip: archive not found: " + archive_abs);
    unzFile uf = unzOpen64(archive_abs.c_str());
    if (uf == nullptr)
        return make_error(openads::AE_TABLE_CORRUPTED,
                          "unzip: not a readable archive: " + archive_abs);
    Budget budget(limits);
    std::vector<std::string> names;
    std::vector<char> namebuf(1024);
    int go = unzGoToFirstFile(uf);
    while (go == UNZ_OK) {
        unz_file_info64 info{};
        std::string entry;
        if (!read_entry_name(uf, info, namebuf, entry)) {
            unzClose(uf);
            return make_error(openads::AE_INTERNAL_ERROR,
                              "unzip: cannot read entry info");
        }
        if (!budget.add(info.uncompressed_size, entry.size() + 64ull)) {
            unzClose(uf); return make_error(openads::AE_ACCESS_DENIED, "ziplist: resource budget exceeded");
        }
        names.emplace_back(std::move(entry));
        go = unzGoToNextFile(uf);
    }
    unzClose(uf);
    if (go != UNZ_END_OF_LIST_OF_FILE)
        return make_error(openads::AE_INTERNAL_ERROR,
                          "unzip: archive walk failed");
    return names;
}

namespace {

// Little-endian appends for pack_zip_entry (self-contained; the
// server_fs TU owns an identical private set — not shared on purpose,
// these two packings must never drift into one layout).
void ze_u16(std::vector<std::uint8_t>& o, std::uint16_t v) {
    o.push_back(static_cast<std::uint8_t>(v & 0xFF));
    o.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}
void ze_u32(std::vector<std::uint8_t>& o, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        o.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}
void ze_u64(std::vector<std::uint8_t>& o, std::uint64_t v) {
    for (int i = 0; i < 8; ++i)
        o.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}
std::uint16_t ze_r16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
std::uint32_t ze_r32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint64_t ze_r64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    return v;
}

}  // namespace

void pack_zip_entry(const ZipEntry& e, std::vector<std::uint8_t>& out) {
    const std::uint16_t n = e.name.size() > 0xFFFF
                                ? 0xFFFF
                                : static_cast<std::uint16_t>(e.name.size());
    const std::uint16_t c =
        e.comment.size() > 0xFFFF
            ? 0xFFFF
            : static_cast<std::uint16_t>(e.comment.size());
    ze_u16(out, n);
    out.insert(out.end(), e.name.begin(), e.name.begin() + n);
    ze_u64(out, e.size);
    ze_u64(out, e.comp_size);
    ze_u16(out, e.method);
    ze_u32(out, e.crc);
    ze_u16(out, e.year);
    out.push_back(e.mon);
    out.push_back(e.day);
    out.push_back(e.hh);
    out.push_back(e.mm);
    out.push_back(e.ss);
    ze_u16(out, e.internal_attr);
    ze_u32(out, e.external_attr);
    out.push_back(e.encrypted ? 1 : 0);
    ze_u16(out, c);
    out.insert(out.end(), e.comment.begin(), e.comment.begin() + c);
}

bool unpack_zip_entry(const std::vector<std::uint8_t>& pl, std::size_t& off,
                      ZipEntry& e) {
    // Fixed tail after the name+comment sums to 38 bytes; anything
    // shorter is a truncated or hostile payload.
    if (off + 2 > pl.size()) return false;
    const auto n = ze_r16(pl.data() + off);
    off += 2;
    if (off + n + 38 > pl.size()) return false;
    e.name.assign(reinterpret_cast<const char*>(pl.data() + off), n);
    off += n;
    e.size = ze_r64(pl.data() + off);
    off += 8;
    e.comp_size = ze_r64(pl.data() + off);
    off += 8;
    e.method = ze_r16(pl.data() + off);
    off += 2;
    e.crc = ze_r32(pl.data() + off);
    off += 4;
    e.year = ze_r16(pl.data() + off);
    off += 2;
    e.mon = pl[off++];
    e.day = pl[off++];
    e.hh = pl[off++];
    e.mm = pl[off++];
    e.ss = pl[off++];
    e.internal_attr = ze_r16(pl.data() + off);
    off += 2;
    e.external_attr = ze_r32(pl.data() + off);
    off += 4;
    e.encrypted = pl[off++] != 0;
    const auto c = ze_r16(pl.data() + off);
    off += 2;
    if (off + c > pl.size()) return false;
    e.comment.assign(reinterpret_cast<const char*>(pl.data() + off), c);
    off += c;
    return true;
}

util::Result<std::vector<ZipEntry>> list_detailed(
    const std::string& archive_abs, const Limits& limits) {
    std::error_code ec;
    if (!fs::is_regular_file(archive_abs, ec) || ec)
        return make_error(openads::AE_NO_FILE_FOUND,
                          "ziplist: archive not found: " + archive_abs);
    unzFile uf = unzOpen64(archive_abs.c_str());
    if (uf == nullptr)
        return make_error(openads::AE_TABLE_CORRUPTED,
                          "ziplist: not a readable archive: " + archive_abs);
    Budget budget(limits);
    std::vector<ZipEntry> out;
    std::vector<char> namebuf(1024);
    std::vector<char> cmtbuf(256);
    int go = unzGoToFirstFile(uf);
    while (go == UNZ_OK) {
        unz_file_info64 info{};
        if (unzGetCurrentFileInfo64(uf, &info, namebuf.data(),
                                    static_cast<uLong>(namebuf.size()),
                                    nullptr, 0, cmtbuf.data(),
                                    static_cast<uLong>(cmtbuf.size())) !=
            UNZ_OK) {
            unzClose(uf);
            return make_error(openads::AE_INTERNAL_ERROR,
                              "ziplist: cannot read entry info");
        }
        if (!budget.add(info.uncompressed_size, info.size_filename + info.size_file_comment + 64ull)) {
            unzClose(uf); return make_error(openads::AE_ACCESS_DENIED, "ziplist: resource budget exceeded");
        }
        // Names/comments past the working buffers are re-queried at
        // exact size (position is unchanged by a failed-size query);
        // absurd sizes mean a corrupt archive, not a big name.
        if (info.size_filename >= namebuf.size() ||
            info.size_file_comment >= cmtbuf.size()) {
            if (info.size_filename > 0xFFFFu ||
                info.size_file_comment > 0xFFFFu) {
                unzClose(uf);
                return make_error(openads::AE_TABLE_CORRUPTED,
                                  "ziplist: entry too large");
            }
            namebuf.assign(info.size_filename + 1, 0);
            cmtbuf.assign(info.size_file_comment + 1, 0);
            if (unzGetCurrentFileInfo64(
                    uf, &info, namebuf.data(),
                    static_cast<uLong>(namebuf.size()), nullptr, 0,
                    cmtbuf.data(),
                    static_cast<uLong>(cmtbuf.size())) != UNZ_OK) {
                unzClose(uf);
                return make_error(openads::AE_INTERNAL_ERROR,
                                  "ziplist: cannot read entry info");
            }
        }
        ZipEntry e;
        e.name = to_entry_seps(std::string(
            namebuf.data(),
            strnlen(namebuf.data(), namebuf.size())));
        e.size = info.uncompressed_size;
        e.comp_size = info.compressed_size;
        e.method = static_cast<std::uint16_t>(info.compression_method);
        e.crc = static_cast<std::uint32_t>(info.crc);
        if (info.tmu_date.tm_year >= 1980) {
            e.year = static_cast<std::uint16_t>(info.tmu_date.tm_year);
            e.mon = static_cast<std::uint8_t>(info.tmu_date.tm_mon + 1);
            e.day = static_cast<std::uint8_t>(info.tmu_date.tm_mday);
            e.hh = static_cast<std::uint8_t>(info.tmu_date.tm_hour);
            e.mm = static_cast<std::uint8_t>(info.tmu_date.tm_min);
            e.ss = static_cast<std::uint8_t>(info.tmu_date.tm_sec);
        }
        e.internal_attr =
            static_cast<std::uint16_t>(info.internal_fa);
        e.external_attr = static_cast<std::uint32_t>(info.external_fa);
        e.encrypted = (info.flag & 1u) != 0u;
        e.comment = std::string(cmtbuf.data(),
                                strnlen(cmtbuf.data(), cmtbuf.size()));
        out.push_back(std::move(e));
        go = unzGoToNextFile(uf);
    }
    unzClose(uf);
    if (go != UNZ_END_OF_LIST_OF_FILE)
        return make_error(openads::AE_INTERNAL_ERROR,
                          "ziplist: archive walk failed");
    return out;
}

}  // namespace openads::engine::zip_arch
