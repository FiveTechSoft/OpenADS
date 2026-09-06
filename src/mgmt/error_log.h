#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace openads::mgmt {

// One row of the error log, as surfaced by sp_mgGetErrorLog. The on-disk
// DBF has separate DATE/TIME fields; they are combined here, matching how
// SAP's sp_mgGetErrorLog folds ads_err.dbf entries into one DateTime.
// The text mirror (ads_err.log) carries the same row plus process/thread,
// session, client, op and table context for developers.
struct ErrorLogEntry {
    std::string   datetime;   // "YYYY-MM-DD HH:MM:SS"
    std::int32_t  code     = 0;
    std::string   source;     // subsystem tag ("SQL", "NET", "SERVER", ...)
    std::int32_t  src_line = 0;
    std::string   detail;     // FileName column: failing statement / message
    // Developer context (text log only; empty when the caller did not
    // supply it). Never read from the DBF — the DBF schema is frozen
    // for SAP compatibility.
    std::uint32_t pid     = 0;
    std::uint64_t tid     = 0;    // hashed std::this_thread::get_id
    std::uint64_t session = 0;    // server session id (0 = none/local)
    std::string   client;         // "ip:port" of the remote peer
    std::string   op;             // wire op / API that failed ("OpenIndex")
    std::string   table;          // table/index path involved
};

// SAP-ADS-style persistent error log (the ads_err.dbf variant — a plain
// DBF3 table readable by any DBF tool, exactly as the SAP docs describe)
// plus a developer-friendly fixed-width text mirror (ads_err.log) in the
// same directory.
//
// Text format: every leading column occupies a fixed width, columns are
// separated by exactly 3 spaces, rows are single lines:
//
//   DATETIME(19)   CODE(6,r)   SOURCE(8)   LINE(8,r)   PID(8,r)   TID(8)   SESSION(8,r)   CLIENT(21)   OP(12)   TABLE(24)   DETAIL...
//
// A header line with the same widths is written when the file is created
// so `tail`/`grep` output is self-describing. DETAIL is the tail column:
// single-line sanitised (\r\n\t -> " | "/space), uncapped so SQL text
// and messages are never silently cut (unlike the DBF's 200-char column).
// Rotation mirrors the DBF (drop oldest third, keep newest two thirds)
// under the same max_kbytes cap, so both files stay roughly in sync.
//
// Location: OPENADS_ERROR_LOG_PATH env var or set_directory() (serverd ini
// / sp_mgSetConfigValue ERROR_ASSERT_LOGS); otherwise the SAP defaults —
// the root of C: on Windows, /var/log/advantage on POSIX — falling back to
// a per-user/temp directory when those aren't writable. No drive-letter
// assumptions: every candidate goes through std::filesystem and is probed
// by actually opening the file.
//
// Size cap: max_kbytes (default 1000, matching SAP). When an append would
// push the file past the cap, the oldest third of the records is dropped
// and the file packed, per the documented SAP behavior.
//
// Writing uses raw file I/O only (never the engine's own table stack), so
// logging an engine error can never recurse into the engine.
class ErrorLog {
public:
    static ErrorLog& instance();

    // Override the directory (dynamic; next write goes to the new place).
    // Empty string re-enables the default resolution.
    void set_directory(const std::string& dir);
    void set_max_kbytes(std::uint32_t kb);

    std::string   directory();    // resolved directory (probes on demand)
    std::string   file_path();    // <directory>/ads_err.dbf
    std::string   text_path();    // <directory>/ads_err.log
    std::uint32_t max_kbytes();

    // Append one entry. code 0 = informational. Thread-safe; failures to
    // write are swallowed (the log must never take the engine down).
    // Writes BOTH files: the SAP-compatible DBF row and the fixed-width
    // text line. pid/tid are captured automatically; session/client/op/
    // table default to empty (use log_ex when the caller knows them).
    void log(std::int32_t code, const std::string& source,
             std::int32_t src_line, const std::string& detail);
    // Extended entry with developer context for the text mirror. The DBF
    // row is identical to log() (frozen SAP schema); the extra fields
    // appear only in ads_err.log.
    void log_ex(std::int32_t code, const std::string& source,
                std::int32_t src_line, const std::string& detail,
                std::uint64_t session, const std::string& client,
                const std::string& op, const std::string& table);

    // Newest `n` entries in chronological order (oldest of the n first).
    std::vector<ErrorLogEntry> read_last(std::size_t n);

    // Per-thread request context stamped onto the text mirror by log().
    // The server sets this per wire frame (session/peer/op/table); plain
    // log() callers (SQL engine, lifecycle) leave it empty. Cleared
    // automatically only by overwriting — set it fresh for each request.
    static void set_log_context(std::uint64_t session,
                                const std::string& client,
                                const std::string& op = "",
                                const std::string& table = "");

private:
    ErrorLog() = default;
    std::string resolve_dir_locked();

    std::mutex    mu_;
    std::string   override_dir_;
    std::string   resolved_dir_;
    std::uint32_t max_kb_ = 1000;
};

}  // namespace openads::mgmt
