#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace openads::mgmt {

// One row of the error log, as surfaced by sp_mgGetErrorLog. The on-disk
// DBF has separate DATE/TIME fields; they are combined here, matching how
// SAP's sp_mgGetErrorLog folds ads_err.dbf entries into one DateTime.
struct ErrorLogEntry {
    std::string   datetime;   // "YYYY-MM-DD HH:MM:SS"
    std::int32_t  code     = 0;
    std::string   source;     // subsystem tag ("SQL", "NET", "SERVER", ...)
    std::int32_t  src_line = 0;
    std::string   detail;     // FileName column: failing statement / message
};

// SAP-ADS-style persistent error log (the ads_err.dbf variant — a plain
// DBF3 table readable by any DBF tool, exactly as the SAP docs describe).
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
    std::uint32_t max_kbytes();

    // Append one entry. code 0 = informational. Thread-safe; failures to
    // write are swallowed (the log must never take the engine down).
    void log(std::int32_t code, const std::string& source,
             std::int32_t src_line, const std::string& detail);

    // Newest `n` entries in chronological order (oldest of the n first).
    std::vector<ErrorLogEntry> read_last(std::size_t n);

private:
    ErrorLog() = default;
    std::string resolve_dir_locked();

    std::mutex    mu_;
    std::string   override_dir_;
    std::string   resolved_dir_;
    std::uint32_t max_kb_ = 1000;
};

}  // namespace openads::mgmt
