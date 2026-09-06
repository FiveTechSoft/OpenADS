#include "mgmt/error_log.h"
#include "mgmt/mg_stats.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace openads::mgmt {

namespace fs = std::filesystem;

namespace {

constexpr const char* kLogName = "ads_err.dbf";
constexpr const char* kTextName = "ads_err.log";

// Fixed-width text mirror layout. Every leading column is padded (or
// truncated) to exactly its width; columns are joined with 3 spaces.
// DETAIL is the tail column and is NOT truncated (DBF already caps at 200).
constexpr const char* kSep = "   ";
constexpr std::size_t kWDatetime = 19;   // "YYYY-MM-DD HH:MM:SS"
constexpr std::size_t kWCode     = 6;    // right-aligned
constexpr std::size_t kWSource   = 8;
constexpr std::size_t kWLine     = 8;    // right-aligned
constexpr std::size_t kWPid      = 8;    // right-aligned
constexpr std::size_t kWTid      = 8;    // last 8 hex of hashed thread id
constexpr std::size_t kWSession  = 8;    // right-aligned
constexpr std::size_t kWClient   = 21;   // "ip:port"
constexpr std::size_t kWOp       = 12;
constexpr std::size_t kWTable    = 24;

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

// Per-thread request context (see set_log_context). Plain log() picks
// these up so existing call sites gain SESSION/CLIENT/OP/TABLE columns
// without signature churn.
struct LogContext {
    std::uint64_t session = 0;
    std::string   client;
    std::string   op;
    std::string   table;
};
thread_local LogContext g_ctx;

void ErrorLog::set_log_context(std::uint64_t session,
                               const std::string& client,
                               const std::string& op,
                               const std::string& table) {
    g_ctx.session = session;
    g_ctx.client  = client;
    g_ctx.op      = op;
    g_ctx.table   = table;
}

// When the caller did not set OP/TABLE context, derive them from the
// conventional "Op: rest" detail shape the server uses
// (e.g. "OpenIndex: foo.cdx"). Only splits when the head looks like a
// bare opcode/identifier so free-text messages with colons are untouched.
void derive_op_table(const std::string& detail, const std::string& op_in,
                     const std::string& table_in, std::string& op_out,
                     std::string& table_out) {
    op_out = op_in;
    table_out = table_in;
    if (!op_out.empty() && !table_out.empty()) return;
    auto pos = detail.find(':');
    if (pos == std::string::npos || pos == 0 || pos > 24) return;
    for (std::size_t i = 0; i < pos; ++i) {
        char c = detail[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == ' ')) return;
    }
    std::string head = detail.substr(0, pos);
    while (!head.empty() && head.back() == ' ') head.pop_back();
    std::string tail = detail.substr(pos + 1);
    std::size_t b = tail.find_first_not_of(' ');
    if (b != std::string::npos) tail = tail.substr(b);
    if (op_out.empty()) op_out = head;
    // Only treat the tail as a TABLE when it looks like a path (has an
    // extension or a slash); otherwise it stays in DETAIL only.
    if (table_out.empty() && (tail.find('.') != std::string::npos ||
                              tail.find('/') != std::string::npos ||
                              tail.find('\\') != std::string::npos)) {
        auto sp = tail.find(' ');
        table_out = (sp == std::string::npos) ? tail : tail.substr(0, sp);
    }
}

// --- Fixed-width text mirror (ads_err.log) -------------------------------

std::uint32_t current_pid() {
#ifdef _WIN32
    return static_cast<std::uint32_t>(_getpid());
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
}

std::string current_tid8() {
    std::uint64_t h = static_cast<std::uint64_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%08X",
                  static_cast<unsigned>(h & 0xFFFFFFFFu));
    return std::string(buf);
}

// Pad or truncate to exactly `width`. Numbers pass right=true.
std::string fix(const std::string& s, std::size_t width, bool right = false) {
    if (s.size() == width) return s;
    if (s.size() > width) return s.substr(0, width);
    if (right) return std::string(width - s.size(), ' ') + s;
    return s + std::string(width - s.size(), ' ');
}

// Single-line sanitise for fixed-width rows: no embedded newlines/tabs.
std::string flat(const std::string& s, bool pipe_nl = false) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        if (c == '\r' || c == '\n') {
            if (pipe_nl) o += " | ";
            else o += ' ';
        } else if (c == '\t') {
            o += ' ';
        } else {
            o += c;
        }
    }
    return o;
}

std::string text_header() {
    return fix("DATETIME", kWDatetime) + kSep +
           fix("CODE", kWCode, true) + kSep +
           fix("SOURCE", kWSource) + kSep +
           fix("LINE", kWLine, true) + kSep +
           fix("PID", kWPid, true) + kSep +
           fix("TID", kWTid) + kSep +
           fix("SESSION", kWSession, true) + kSep +
           fix("CLIENT", kWClient) + kSep +
           fix("OP", kWOp) + kSep +
           fix("TABLE", kWTable) + kSep + "DETAIL";
}

std::string text_line(const std::string& datetime, std::int32_t code,
                      const std::string& source, std::int32_t src_line,
                      std::uint32_t pid, const std::string& tid,
                      std::uint64_t session, const std::string& client,
                      const std::string& op, const std::string& table,
                      const std::string& detail) {
    return fix(datetime, kWDatetime) + kSep +
           fix(std::to_string(code), kWCode, true) + kSep +
           fix(flat(source), kWSource) + kSep +
           fix(std::to_string(src_line), kWLine, true) + kSep +
           fix(std::to_string(pid), kWPid, true) + kSep +
           fix(tid, kWTid) + kSep +
           fix(session == 0 ? "" : std::to_string(session), kWSession, true) + kSep +
           fix(flat(client), kWClient) + kSep +
           fix(flat(op), kWOp) + kSep +
           fix(flat(table), kWTable) + kSep + flat(detail, true);
}

// Append one line to ads_err.log with the same size-cap rotation as the
// DBF (drop oldest third of data lines). Caller holds mu_. Header is
// written on file creation; a foreign header-less file gets one prepended.
void append_text_locked(const fs::path& dir, const std::string& line,
                        std::uint32_t max_kb) {
    const fs::path p = dir / kTextName;
    const std::size_t max_bytes = static_cast<std::size_t>(max_kb) * 1024u;
    std::error_code ec;
    std::uint64_t sz = fs::exists(p, ec) ? fs::file_size(p, ec) : 0;
    if (ec) sz = 0;
    if (!fs::exists(p, ec)) {
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        if (!out.good()) return;
        const std::string h = text_header();
        out << h << "\n" << line << "\n";
        return;
    }
    if (sz + line.size() + 1 <= max_bytes) {
        std::ofstream out(p, std::ios::binary | std::ios::app);
        if (!out.good()) return;
        // A pre-existing file without our header (e.g. operator-created)
        // still gets one so columns stay self-describing.
        if (sz == 0) out << text_header() << "\n";
        out << line << "\n";
        return;
    }
    // Over cap: keep header + newest two thirds of data lines, then append.
    std::ifstream in(p, std::ios::binary);
    std::vector<std::string> data;
    std::string first, row;
    bool has_header = false;
    if (in.good() && std::getline(in, first)) {
        has_header = (first.rfind("DATETIME", 0) == 0);
        if (!has_header && !first.empty()) data.push_back(std::move(first));
        while (std::getline(in, row)) {
            if (!row.empty() && row.back() == '\r') row.pop_back();
            data.push_back(std::move(row));
        }
    }
    if (data.size() > 2) {
        data.erase(data.begin(),
                   data.begin() + static_cast<std::ptrdiff_t>(data.size() / 3));
    } else if (!data.empty()) {
        data.erase(data.begin());  // tiny file: drop oldest single line
    }
    data.push_back(line);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out.good()) return;
    out << text_header() << "\n";
    for (const auto& r : data) out << r << "\n";
    (void)has_header;
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

std::string ErrorLog::text_path() {
    std::lock_guard<std::mutex> lk(mu_);
    std::string d = resolve_dir_locked();
    if (d.empty()) return {};
    return (fs::path(d) / kTextName).string();
}

void ErrorLog::log(std::int32_t code, const std::string& source,
                   std::int32_t src_line, const std::string& detail) {
    // Pick up the thread's request context (set per wire frame by the
    // server); explicit log_ex args win when both are present.
    log_ex(code, source, src_line, detail, g_ctx.session, g_ctx.client,
           g_ctx.op, g_ctx.table);
}

void ErrorLog::log_ex(std::int32_t code, const std::string& source,
                      std::int32_t src_line, const std::string& detail,
                      std::uint64_t session, const std::string& client,
                      const std::string& op, const std::string& table) {
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

    // Fixed-width text mirror first (cheap append; never blocks the DBF).
    // Same timestamp, plus pid/tid captured here and the caller-supplied
    // session/client/op/table context. Detail keeps its full length.
    // When OP/TABLE are empty, derive them from "Op: path" details so
    // the existing NET error call sites gain columns with no churn.
    {
        char dt[20];
        std::snprintf(dt, sizeof(dt), "%04d-%02d-%02d %02d:%02d:%02d",
                      tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                      tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        std::string eff_op = op.empty() ? g_ctx.op : op;
        std::string eff_table = table.empty() ? g_ctx.table : table;
        std::string eff_session_client = client.empty() ? g_ctx.client : client;
        std::uint64_t eff_session = session != 0 ? session : g_ctx.session;
        std::string drv_op, drv_table;
        derive_op_table(detail, eff_op, eff_table, drv_op, drv_table);
        append_text_locked(fs::path(d),
            text_line(dt, code, source, src_line, current_pid(),
                      current_tid8(), eff_session, eff_session_client,
                      drv_op, drv_table, detail),
            max_kb_);
    }

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
