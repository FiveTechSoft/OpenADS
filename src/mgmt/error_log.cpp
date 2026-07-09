#include "mgmt/error_log.h"
#include "mgmt/mg_stats.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace openads::mgmt {

namespace fs = std::filesystem;

namespace {

constexpr const char* kLogName = "ads_err.dbf";

// On-disk DBF3 schema. Field layout mirrors what the SAP docs describe for
// ads_err.dbf: separate DATE and TIME fields (no Error_Number — that
// column exists only in the ADT variant).
struct DbfCol {
    const char*  name;
    char         type;
    std::uint8_t len;
    std::uint8_t dec;
};
constexpr DbfCol kCols[] = {
    {"DATE",       'D',   8, 0},
    {"TIME",       'C',   8, 0},
    {"ERROR_CODE", 'N',  10, 0},
    {"ADS_SOURCE", 'C',  32, 0},
    {"SRC_LINE",   'N',   8, 0},
    {"FILENAME",   'C', 200, 0},
};
constexpr std::size_t kNCols = sizeof(kCols) / sizeof(kCols[0]);

constexpr std::size_t rec_len() {
    std::size_t n = 1;  // delete flag
    for (const auto& c : kCols) n += c.len;
    return n;
}
constexpr std::size_t kRecLen = rec_len();                    // 267
constexpr std::size_t kHdrLen = 32 + 32 * kNCols + 1;         // 225

// Re-entrancy guard: if writing the log itself raises an engine error
// that ends up back here, drop it instead of recursing.
thread_local bool g_in_log = false;

void put_u32_le(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}
std::uint32_t get_u32_le(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::vector<std::uint8_t> build_header(std::uint32_t nrecs) {
    std::vector<std::uint8_t> h(kHdrLen, 0);
    std::time_t tt = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    h[0] = 0x03;
    h[1] = static_cast<std::uint8_t>(tmv.tm_year % 100);
    h[2] = static_cast<std::uint8_t>(tmv.tm_mon + 1);
    h[3] = static_cast<std::uint8_t>(tmv.tm_mday);
    put_u32_le(&h[4], nrecs);
    h[8]  = static_cast<std::uint8_t>(kHdrLen & 0xFFu);
    h[9]  = static_cast<std::uint8_t>((kHdrLen >> 8) & 0xFFu);
    h[10] = static_cast<std::uint8_t>(kRecLen & 0xFFu);
    h[11] = static_cast<std::uint8_t>((kRecLen >> 8) & 0xFFu);
    std::size_t off = 32;
    for (const auto& c : kCols) {
        std::uint8_t* fd = &h[off];
        std::size_t nl = std::strlen(c.name);
        std::memcpy(fd, c.name, nl < 10 ? nl : 10);
        fd[11] = static_cast<std::uint8_t>(c.type);
        fd[16] = c.len;
        fd[17] = c.dec;
        off += 32;
    }
    h[kHdrLen - 1] = 0x0D;
    return h;
}

void fill_field(std::uint8_t* rec, std::size_t col, const std::string& v) {
    std::size_t off = 1;
    for (std::size_t i = 0; i < col; ++i) off += kCols[i].len;
    std::size_t n = v.size() < kCols[col].len ? v.size() : kCols[col].len;
    if (kCols[col].type == 'N') {
        // Right-justified, space-padded, DBF numeric convention.
        std::size_t pad = kCols[col].len - n;
        std::memcpy(rec + off + pad, v.data(), n);
    } else {
        std::memcpy(rec + off, v.data(), n);
    }
}

std::string read_field(const std::uint8_t* rec, std::size_t col) {
    std::size_t off = 1;
    for (std::size_t i = 0; i < col; ++i) off += kCols[i].len;
    std::string s(reinterpret_cast<const char*>(rec + off), kCols[col].len);
    // Trim both sides (N fields are left-padded, C fields right-padded).
    std::size_t b = s.find_first_not_of(' ');
    if (b == std::string::npos) return "";
    std::size_t e = s.find_last_not_of(' ');
    return s.substr(b, e - b + 1);
}

bool dir_usable(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);        // ok if it already exists
    if (!fs::is_directory(dir, ec)) return false;
    // Probe by opening the actual log file for append — proves both the
    // directory and the file are writable by this process.
    std::ofstream f(dir / kLogName, std::ios::binary | std::ios::app);
    return f.good();
}

}  // namespace

ErrorLog& ErrorLog::instance() {
    static ErrorLog g;
    return g;
}

void ErrorLog::set_directory(const std::string& dir) {
    std::lock_guard<std::mutex> lk(mu_);
    override_dir_ = dir;
    resolved_dir_.clear();
}

void ErrorLog::set_max_kbytes(std::uint32_t kb) {
    std::lock_guard<std::mutex> lk(mu_);
    max_kb_ = kb == 0 ? 1 : kb;
}

std::uint32_t ErrorLog::max_kbytes() {
    std::lock_guard<std::mutex> lk(mu_);
    return max_kb_;
}

std::string ErrorLog::resolve_dir_locked() {
    if (!resolved_dir_.empty()) return resolved_dir_;

    std::vector<fs::path> candidates;
    if (!override_dir_.empty()) candidates.emplace_back(override_dir_);
    if (const char* env = std::getenv("OPENADS_ERROR_LOG_PATH");
        env != nullptr && env[0] != '\0') {
        candidates.emplace_back(env);
    }
#ifdef _WIN32
    // SAP default: root of the C: drive (writable for a service owner);
    // then ProgramData, then temp.
    candidates.emplace_back("C:\\");
    if (const char* pd = std::getenv("ProgramData");
        pd != nullptr && pd[0] != '\0') {
        candidates.emplace_back(fs::path(pd) / "OpenADS");
    }
#else
    // SAP default on Linux; HOME fallback covers unprivileged runs and
    // macOS, where /var/log isn't generally writable.
    candidates.emplace_back("/var/log/advantage");
    if (const char* home = std::getenv("HOME");
        home != nullptr && home[0] != '\0') {
        candidates.emplace_back(fs::path(home) / ".openads");
    }
#endif
    std::error_code ec;
    candidates.emplace_back(fs::temp_directory_path(ec));

    for (const auto& c : candidates) {
        if (c.empty()) continue;
        if (dir_usable(c)) {
            resolved_dir_ = c.string();
            return resolved_dir_;
        }
    }
    return {};   // nowhere writable: log() becomes a no-op
}

std::string ErrorLog::directory() {
    std::lock_guard<std::mutex> lk(mu_);
    return resolve_dir_locked();
}

std::string ErrorLog::file_path() {
    std::lock_guard<std::mutex> lk(mu_);
    std::string d = resolve_dir_locked();
    if (d.empty()) return {};
    return (fs::path(d) / kLogName).string();
}

void ErrorLog::log(std::int32_t code, const std::string& source,
                   std::int32_t src_line, const std::string& detail) {
    if (g_in_log) return;
    g_in_log = true;
    if (code != 0) {
        process_mg_stats().logged_errors.fetch_add(
            1, std::memory_order_relaxed);
    }
    std::lock_guard<std::mutex> lk(mu_);
    std::string d = resolve_dir_locked();
    if (d.empty()) { g_in_log = false; return; }
    fs::path p = fs::path(d) / kLogName;

    std::time_t tt = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    char datebuf[9], timebuf[9];
    std::snprintf(datebuf, sizeof(datebuf), "%04d%02d%02d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    std::snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d",
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

    std::vector<std::uint8_t> rec(kRecLen, ' ');
    fill_field(rec.data(), 0, datebuf);
    fill_field(rec.data(), 1, timebuf);
    fill_field(rec.data(), 2, std::to_string(code));
    fill_field(rec.data(), 3, source);
    fill_field(rec.data(), 4, std::to_string(src_line));
    fill_field(rec.data(), 5, detail);

    // Read just the header to learn the current record count. The common
    // case appends in place; a full read + rewrite happens only when the
    // size cap forces the SAP rotation (drop oldest third, pack) or the
    // file is missing/stale.
    std::uint32_t nrecs = 0;
    bool have_file = false;
    {
        std::ifstream in(p, std::ios::binary);
        if (in.good()) {
            std::vector<std::uint8_t> hdr(kHdrLen);
            in.read(reinterpret_cast<char*>(hdr.data()), kHdrLen);
            std::uint16_t rl = static_cast<std::uint16_t>(
                hdr[10] | (hdr[11] << 8));
            if (in.gcount() == static_cast<std::streamsize>(kHdrLen) &&
                hdr[0] == 0x03 && rl == kRecLen &&
                get_u32_le(&hdr[4]) < 1000000u) {
                nrecs = get_u32_le(&hdr[4]);
                have_file = true;
            }
            // A schema mismatch (old layout) just starts the file over.
        }
    }

    const std::size_t max_bytes =
        static_cast<std::size_t>(max_kb_) * 1024u;
    const bool over_cap =
        kHdrLen + (static_cast<std::size_t>(nrecs) + 1) * kRecLen + 1 >
        max_bytes;

    if (have_file && over_cap && nrecs > 2) {
        // Rotation: keep the newest two thirds, then append.
        std::vector<std::vector<std::uint8_t>> recs;
        {
            std::ifstream in(p, std::ios::binary);
            in.seekg(kHdrLen, std::ios::beg);
            for (std::uint32_t i = 0; i < nrecs && in.good(); ++i) {
                std::vector<std::uint8_t> r(kRecLen);
                in.read(reinterpret_cast<char*>(r.data()), kRecLen);
                if (in.gcount() !=
                    static_cast<std::streamsize>(kRecLen)) break;
                recs.push_back(std::move(r));
            }
        }
        recs.erase(recs.begin(),
                   recs.begin() + static_cast<std::ptrdiff_t>(
                       recs.size() / 3));
        recs.push_back(std::move(rec));
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        if (out.good()) {
            auto hdr = build_header(static_cast<std::uint32_t>(recs.size()));
            out.write(reinterpret_cast<const char*>(hdr.data()),
                      static_cast<std::streamsize>(hdr.size()));
            for (const auto& r : recs) {
                out.write(reinterpret_cast<const char*>(r.data()),
                          static_cast<std::streamsize>(r.size()));
            }
            out.put('\x1A');
        }
    } else if (have_file) {
        // Fast path: overwrite the EOF marker with the record, re-add the
        // marker, bump the header count.
        std::fstream out(p, std::ios::binary | std::ios::in | std::ios::out);
        if (out.good()) {
            out.seekp(static_cast<std::streamoff>(kHdrLen) +
                          static_cast<std::streamoff>(nrecs) *
                              static_cast<std::streamoff>(kRecLen),
                      std::ios::beg);
            out.write(reinterpret_cast<const char*>(rec.data()),
                      static_cast<std::streamsize>(rec.size()));
            out.put('\x1A');
            std::uint8_t cnt[4];
            put_u32_le(cnt, nrecs + 1);
            out.seekp(4, std::ios::beg);
            out.write(reinterpret_cast<const char*>(cnt), 4);
        }
    } else {
        // New (or unusable) file: write header + first record.
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        if (out.good()) {
            auto hdr = build_header(1);
            out.write(reinterpret_cast<const char*>(hdr.data()),
                      static_cast<std::streamsize>(hdr.size()));
            out.write(reinterpret_cast<const char*>(rec.data()),
                      static_cast<std::streamsize>(rec.size()));
            out.put('\x1A');
        }
    }
    g_in_log = false;
}

std::vector<ErrorLogEntry> ErrorLog::read_last(std::size_t n) {
    std::vector<ErrorLogEntry> out;
    std::lock_guard<std::mutex> lk(mu_);
    std::string d = resolve_dir_locked();
    if (d.empty()) return out;
    fs::path p = fs::path(d) / kLogName;
    std::ifstream in(p, std::ios::binary);
    if (!in.good()) return out;
    std::vector<std::uint8_t> hdr(kHdrLen);
    in.read(reinterpret_cast<char*>(hdr.data()), kHdrLen);
    if (in.gcount() != static_cast<std::streamsize>(kHdrLen) ||
        hdr[0] != 0x03) {
        return out;
    }
    std::uint32_t total = get_u32_le(&hdr[4]);
    std::uint16_t rl = static_cast<std::uint16_t>(hdr[10] | (hdr[11] << 8));
    std::uint16_t hl = static_cast<std::uint16_t>(hdr[8]  | (hdr[9]  << 8));
    if (rl != kRecLen || total > 1000000u) return out;

    std::uint32_t start = (n < total)
        ? static_cast<std::uint32_t>(total - n) : 0u;
    in.seekg(hl + static_cast<std::streamoff>(start) *
                     static_cast<std::streamoff>(kRecLen),
             std::ios::beg);
    for (std::uint32_t i = start; i < total && in.good(); ++i) {
        std::vector<std::uint8_t> r(kRecLen);
        in.read(reinterpret_cast<char*>(r.data()), kRecLen);
        if (in.gcount() != static_cast<std::streamsize>(kRecLen)) break;
        ErrorLogEntry e;
        std::string dt = read_field(r.data(), 0);   // YYYYMMDD
        if (dt.size() == 8) {
            dt = dt.substr(0, 4) + "-" + dt.substr(4, 2) + "-" +
                 dt.substr(6, 2);
        }
        e.datetime = dt + " " + read_field(r.data(), 1);
        e.code     = static_cast<std::int32_t>(
            std::atol(read_field(r.data(), 2).c_str()));
        e.source   = read_field(r.data(), 3);
        e.src_line = static_cast<std::int32_t>(
            std::atol(read_field(r.data(), 4).c_str()));
        e.detail   = read_field(r.data(), 5);
        out.push_back(std::move(e));
    }
    return out;
}

}  // namespace openads::mgmt
