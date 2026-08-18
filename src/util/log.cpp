#include "util/log.h"

#include "platform/time.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <ostream>
#include <string>
#include <unordered_set>

namespace openads::util {

namespace {

const char* level_name(LogLevel l) {
    switch (l) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

std::string lower(std::string_view s) {
    std::string out{s};
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return out;
}

bool env_truthy(const char* v) {
    if (v == nullptr || v[0] == '\0') return false;
    if (v[0] == '0' && v[1] == '\0') return false;
    std::string s = lower(v);
    return s != "0" && s != "false" && s != "off" && s != "no";
}

constexpr char kB36[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

std::ostream* g_console = nullptr;          // nullptr = stderr
std::ostream* g_file    = nullptr;          // nullptr = OPENADS_LOG_FILE
enum class Details { Auto, On, Off };
Details g_details = Details::Auto;
bool    g_file_sink_set = false;            // true when set_audit_file() used
std::mutex g_audit_mu;
std::atomic<std::uint32_t> g_next_conn{0};
std::atomic<std::uint32_t> g_next_seq{1};
std::atomic<bool> g_seeded{false};
std::uint32_t g_conn_seed = 0;
std::unordered_set<std::string> g_remote_asked;

std::string norm_audit_path(std::string_view p) {
    std::string s{p};
    for (char& ch : s) {
        if (ch == '\\') ch = '/';
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

void ensure_seed() {
    bool expected = false;
    if (g_seeded.compare_exchange_strong(expected, true)) {
        auto us = openads::platform::utc_unix_micros();
        g_conn_seed = static_cast<std::uint32_t>(us ^ (us >> 32));
    }
}

void write_console_line(const std::string& line) {
    if (g_console != nullptr) {
        (*g_console) << line;
        g_console->flush();
        return;
    }
    // No explicit sink: the DLL is embedded in someone else's process
    // (php, Apache, Harbour apps, the parity harness). SAP's ace64.dll
    // never writes to the host's streams, so the stderr echo is opt-in —
    // OPENADS_LOG / OPENADS_RESOLVE_VERBOSE, or set_audit_console().
    // OPENADS_LOG_FILE keeps working regardless via write_file_line.
    if (!env_truthy(std::getenv("OPENADS_RESOLVE_VERBOSE")) &&
        std::getenv("OPENADS_LOG") == nullptr) {
        return;
    }
    std::fputs(line.c_str(), stderr);
    std::fflush(stderr);
}

void write_file_line(const std::string& line) {
    if (g_file != nullptr) {
        (*g_file) << line;
        g_file->flush();
        return;
    }
    if (g_file_sink_set) return;  // tests explicitly disabled the file
    const char* path = std::getenv("OPENADS_LOG_FILE");
    if (path == nullptr || path[0] == '\0') return;
    std::FILE* f = std::fopen(path, "a");
    if (f == nullptr) return;
    std::fputs(line.c_str(), f);
    std::fflush(f);
    std::fclose(f);
}

} // namespace

void Log::write(LogLevel level, std::string_view message) noexcept {
    if (sink_ == nullptr) return;
    if (static_cast<int>(level) < static_cast<int>(threshold_)) return;
    (*sink_) << level_name(level) << ' ' << message << '\n';
}

LogLevel log_level_from_string(std::string_view s) noexcept {
    const std::string norm = lower(s);
    if (norm == "trace") return LogLevel::Trace;
    if (norm == "debug") return LogLevel::Debug;
    if (norm == "info")  return LogLevel::Info;
    if (norm == "warn")  return LogLevel::Warn;
    if (norm == "error") return LogLevel::Error;
    return LogLevel::Info;
}

std::string format_connection_serial(std::uint32_t n) {
    char out[7];
    out[6] = '\0';
    for (int i = 5; i >= 0; --i) {
        out[i] = kB36[n % 36u];
        n /= 36u;
    }
    return std::string(out, 6);
}

std::string format_entry_serial(std::uint32_t n) {
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%08u",
                  static_cast<unsigned>(n % 100000000u));
    return std::string(buf, 8);
}

std::string format_log_timestamp() {
    auto w = openads::platform::now_local();
    int ms = static_cast<int>(w.ms_of_day % 1000);
    if (ms < 0) ms = 0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s %s.%03d",
                  w.date.c_str(), w.time.c_str(), ms);
    return buf;
}

std::string format_log_prefix(std::string_view conn_serial,
                              std::uint32_t    entry_serial,
                              std::uint32_t    seq,
                              std::string_view timestamp) {
    std::string ts{timestamp};
    if (ts.empty()) ts = format_log_timestamp();
    std::string conn;
    if (conn_serial.size() >= 6) {
        conn.assign(conn_serial.data(), 6);
    } else {
        conn.assign(6 - conn_serial.size(), '0');
        conn.append(conn_serial);
    }
    return conn + ' ' + format_entry_serial(entry_serial) + ' ' +
           format_entry_serial(seq) + ' ' + ts;
}

std::uint32_t next_audit_seq() {
    return g_next_seq.fetch_add(1);
}

std::string make_connection_serial() {
    ensure_seed();
    std::uint32_t n = g_conn_seed + g_next_conn.fetch_add(1);
    return format_connection_serial(n);
}

bool audit_details_enabled() {
    if (g_details == Details::On)  return true;
    if (g_details == Details::Off) return false;
    if (env_truthy(std::getenv("OPENADS_RESOLVE_VERBOSE"))) return true;
    const char* lvl = std::getenv("OPENADS_LOG");
    if (lvl != nullptr) {
        const std::string n = lower(lvl);
        if (n == "debug" || n == "trace") return true;
    }
    return false;
}

void set_audit_console(std::ostream* sink) { g_console = sink; }

void set_audit_file(std::ostream* sink) {
    g_file = sink;
    g_file_sink_set = true;
}

void set_audit_details_enabled(bool on) {
    g_details = on ? Details::On : Details::Off;
}

void reset_audit_config() {
    g_console = nullptr;
    g_file = nullptr;
    g_file_sink_set = false;
    g_details = Details::Auto;
    g_remote_asked.clear();
    g_next_seq.store(1);
}

void write_audit(AuditKind       kind,
                 std::string_view conn_serial,
                 std::uint32_t    entry_serial,
                 std::uint32_t    seq,
                 std::string_view message,
                 std::string_view timestamp) {
    if (kind == AuditKind::Detail && !audit_details_enabled()) return;
    if (seq == 0) seq = next_audit_seq();
    std::string line =
        format_log_prefix(conn_serial, entry_serial, seq, timestamp);
    line.push_back(' ');
    line.append(message.data(), message.size());
    line.push_back('\n');
    std::lock_guard<std::mutex> lk(g_audit_mu);
    write_console_line(line);
    if (kind == AuditKind::Resolved) write_file_line(line);
}

void write_remote_open_audit(std::string_view asked_name) {
    static std::string id = make_connection_serial();
    static std::atomic<std::uint32_t> n{1};
    std::string key = norm_audit_path(asked_name);
    {
        std::lock_guard<std::mutex> lk(g_audit_mu);
        if (!g_remote_asked.insert(std::move(key)).second) return;
    }
    std::string msg = "RESOLVED=\"(remote)\" ASKED=\"";
    msg.append(asked_name.data(), asked_name.size());
    msg += "\" VIA=REMOTE";
    write_audit(AuditKind::Resolved, id, n.fetch_add(1),
                next_audit_seq(), msg);
}

} // namespace openads::util
