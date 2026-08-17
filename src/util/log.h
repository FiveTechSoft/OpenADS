#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

namespace openads::util {

enum class LogLevel { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4 };

class Log {
public:
    Log(LogLevel threshold, std::ostream* sink) noexcept
        : threshold_(threshold), sink_(sink) {}

    void write(LogLevel level, std::string_view message) noexcept;

    LogLevel threshold() const noexcept { return threshold_; }

private:
    LogLevel       threshold_;
    std::ostream*  sink_;
};

LogLevel log_level_from_string(std::string_view s) noexcept;

// ---------------------------------------------------------------------------
// Audit lines:  CONN(6) ENTRY(8) THR(3) A/U+SEQ(8) TIMESTAMP  message
// HYT673 00000001 001 A0000014 2026-08-16 22:12:13.826 RESOLVED="..." (sandboxed)
//
// CONN  — connection serial (base-36, restarts at 1 on each AdsConnect).
// ENTRY — per-connection sequence (restarts at 1 on each AdsConnect).
// THR   — 3-digit right-aligned thread id (lower bits of OS thread hash).
// A/U   — A = aliased resolve (via Data Dictionary), U = unaliased.
// SEQ   — process-wide RESOLVED sequence, never restarts.
//
// Detail lines of the same resolve reuse ENTRY, THR, A/U, and SEQ.
//
// Resolved lines go to the console and, if configured, the log file.
// Detail lines (input=, LEGACY remap, FALLBACK) are console-only and
// off by default (OPENADS_RESOLVE_VERBOSE=1 or OPENADS_LOG=debug|trace).
// The file never stores Detail — only RESOLVED.
// ---------------------------------------------------------------------------

enum class AuditKind { Resolved, Detail };

std::string format_connection_serial(std::uint32_t n);
std::string format_entry_serial(std::uint32_t n);
std::string format_log_timestamp();
std::string format_log_prefix(std::string_view conn_serial,
                              std::uint32_t    entry_serial,
                              std::uint64_t    thread_id,
                              bool             aliased,
                              std::uint32_t    seq,
                              std::string_view timestamp = {});
std::string make_connection_serial();
// Process-wide RESOLVED sequence. Starts at 1. reset_audit_config()
// puts it back to 1 (tests only).
std::uint32_t next_audit_seq();

void write_audit(AuditKind       kind,
                 std::string_view conn_serial,
                 std::uint32_t    entry_serial,
                 std::uint64_t    thread_id,
                 bool             aliased,
                 std::uint32_t    seq,
                 std::string_view message,
                 std::string_view timestamp = {});

// Client-side AdsOpenTable that went over the wire: no local file
// was resolved. Still written as a RESOLVED line so OPENADS_LOG_FILE
// shows the asked name (login investigations).
void write_remote_open_audit(std::string_view asked_name,
                             std::uint64_t    thread_id);

// Test hooks. nullptr console = stderr; nullptr file = OPENADS_LOG_FILE
// if that env var is set. reset_audit_config() restores both.
void set_audit_console(std::ostream* sink);
void set_audit_file(std::ostream* sink);
void set_audit_details_enabled(bool on);
void reset_audit_config();
bool audit_details_enabled();

// Prune RESOLVED lines older than OPENADS_LOG_RETENTION_DAYS from
// OPENADS_LOG_FILE. Called once at startup; no-op if env vars unset.
void prune_log_if_configured();

} // namespace openads::util
