// Server-side ZIP/UNZIP — see zip_arch.h. Classic minizip over
// std::fstream streaming (128 KiB chunks; no whole-file buffering).

#include "engine/zip_arch.h"

#include "openads/error.h"
#include "platform/fs_sandbox.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

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
        fs::remove(archive_abs, ec);
        if (ec)
            return make_error(openads::AE_INTERNAL_ERROR,
                              "zip: cannot remove existing archive: " +
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
        jobs.push_back({f, entry, sz});
    }
    if (jobs.empty())
        return make_error(openads::AE_NO_MATCHING_FILE,
                          "zip: nothing left after excludes");

    zipFile zf = zipOpen64(archive_abs.c_str(), APPEND_STATUS_CREATE);
    if (zf == nullptr)
        return make_error(openads::AE_INTERNAL_ERROR,
                          "zip: cannot create archive: " + archive_abs);
    Stats st;
    std::vector<char> buf(kChunk);
    std::string fail;
    for (const auto& j : jobs) {
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
            while (crc_in) {
                crc_in.read(buf.data(),
                            static_cast<std::streamsize>(buf.size()));
                const std::streamsize got = crc_in.gcount();
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
        while (in) {
            in.read(buf.data(),
                    static_cast<std::streamsize>(buf.size()));
            const std::streamsize got = in.gcount();
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
        fs::remove(archive_abs, ec);  // no half archives left behind
        return make_error(openads::AE_INTERNAL_ERROR, fail);
    }
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
    std::vector<std::string> flat_seen;
    int go = unzGoToFirstFile(uf);
    while (go == UNZ_OK) {
        unz_file_info64 info{};
        if (unzGetCurrentFileInfo64(uf, &info, namebuf.data(),
                                    static_cast<uLong>(namebuf.size()),
                                    nullptr, 0, nullptr, 0) != UNZ_OK) {
            fail = "unzip: cannot read entry info";
            break;
        }
        std::string entry(namebuf.data());
        entry = to_entry_seps(entry);
        if (!entry_name_ok(entry)) {
            fail = "unzip: refusing entry name: " + entry;
            fail_code = openads::AE_ACCESS_DENIED;
            break;
        }
        const bool is_dir = !entry.empty() && entry.back() == '/';
        std::string out_rel = entry;
        if (!opt.with_path) {
            while (!out_rel.empty() && out_rel.back() == '/')
                out_rel.pop_back();
            out_rel = base_name_of(out_rel);
            if (!is_dir) {
                for (const auto& s : flat_seen) {
                    if (s == out_rel) {
                        fail = "unzip: flat-mode name collision: " +
                               out_rel + " (extract with paths)";
                        break;
                    }
                }
                if (!fail.empty()) break;
            }
        }
        const fs::path out_path =
            fs::path(dest_dir_abs) / out_rel;
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
            const char* pwd = opt.password.empty() ? nullptr
                                                   : opt.password.c_str();
            if (unzOpenCurrentFilePassword(uf, pwd) != UNZ_OK) {
                fail = "unzip: cannot open entry (bad password?): " +
                       entry;
                break;
            }
            // Scoped so the stream closes before any cleanup remove()
            // below — Windows cannot delete an open file.
            bool rerr = false;
            {
                std::ofstream out(out_path, std::ios::binary |
                                                std::ios::trunc);
                if (!out) {
                    unzCloseCurrentFile(uf);
                    fail = "unzip: cannot write target: " +
                           out_path.string();
                    break;
                }
                for (;;) {
                    const int got = unzReadCurrentFile(
                        uf, buf.data(),
                        static_cast<unsigned>(buf.size()));
                    if (got < 0) {
                        rerr = true;
                        break;
                    }
                    if (got == 0) break;
                    out.write(buf.data(), got);
                    if (!out) {
                        rerr = true;
                        break;
                    }
                }
                out.close();
            }
            // NB: unzCloseCurrentFile surfaces the CRC check.
            if (unzCloseCurrentFile(uf) != UNZ_OK) rerr = true;
            if (rerr) {
                fs::remove(out_path, ec);
                fail = "unzip: entry failed (bad password or "
                       "corrupt data): " + entry;
                break;
            }
            if (!opt.with_path) flat_seen.push_back(out_rel);
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
    const std::string& archive_abs) {
    std::error_code ec;
    if (!fs::is_regular_file(archive_abs, ec) || ec)
        return make_error(openads::AE_NO_FILE_FOUND,
                          "unzip: archive not found: " + archive_abs);
    unzFile uf = unzOpen64(archive_abs.c_str());
    if (uf == nullptr)
        return make_error(openads::AE_TABLE_CORRUPTED,
                          "unzip: not a readable archive: " + archive_abs);
    std::vector<std::string> names;
    std::vector<char> namebuf(1024);
    int go = unzGoToFirstFile(uf);
    while (go == UNZ_OK) {
        unz_file_info64 info{};
        if (unzGetCurrentFileInfo64(uf, &info, namebuf.data(),
                                    static_cast<uLong>(namebuf.size()),
                                    nullptr, 0, nullptr, 0) != UNZ_OK) {
            unzClose(uf);
            return make_error(openads::AE_INTERNAL_ERROR,
                              "unzip: cannot read entry info");
        }
        names.emplace_back(to_entry_seps(std::string(namebuf.data())));
        go = unzGoToNextFile(uf);
    }
    unzClose(uf);
    if (go != UNZ_END_OF_LIST_OF_FILE)
        return make_error(openads::AE_INTERNAL_ERROR,
                          "unzip: archive walk failed");
    return names;
}

}  // namespace openads::engine::zip_arch
