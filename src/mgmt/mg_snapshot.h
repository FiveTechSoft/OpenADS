#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace openads::mgmt {

// One connected user / session.
struct MgUser {
    std::string                           name;
    std::string                           address;     // "ip:port"
    std::string                           os_login;
    std::uint16_t                         conn_no = 0;
    std::chrono::system_clock::time_point  connected_at{};
    // Running average wall-clock time to process one wire frame on this
    // session's thread, in microseconds. Not part of the SAP-compatible
    // ADS_MGMT_USER_INFO surface (which has no room for it) — exposed via
    // the separate AdsMgGetUserAvgCost export instead. 0 for local mode
    // (no per-session frame loop to time there).
    std::uint32_t                         avg_cost_micros = 0;
};

// One open table in a session.
struct MgTable {
    std::string   name;
    std::string   user;
    std::uint16_t conn_no   = 0;
    std::uint16_t open_mode = 0;   // 0 = shared, 1 = exclusive
    std::uint16_t lock_type = 0;   // ADS_MGMT_* lock constant
};

// One open index/order.
struct MgIndex {
    std::string name;
    std::string tag;
    std::string expression;
};

// One held lock.
struct MgLock {
    std::string   user;
    std::uint16_t conn_no = 0;
    std::uint32_t recno   = 0;
};

// One worker thread.
struct MgThread {
    std::uint32_t thread_no = 0;
    std::uint16_t opcode    = 0;
    std::string   user;
    std::uint16_t conn_no   = 0;
    std::string   os_login;
    // True while this session's thread is still inside dispatch() for
    // `opcode` at snapshot time — false means `opcode`/`sql` are the last
    // completed operation, not a currently-running one. Mirrors SAP ARC's
    // sp_GetSQLStatements() "Active" column (see mgtscrn.pas/.dfm in the
    // Advantage Data Architect source) — stale rows are expected/useful,
    // same as there.
    bool          active = false;
    // Text of the last ExecuteSQL frame this session processed, if any —
    // persists across later non-SQL frames so a completed query's text
    // stays visible ("Active" just goes false). Empty when this session
    // has never run one.
    std::string   sql;
    // When `sql` started running.
    std::chrono::system_clock::time_point sql_at{};
};

// Point-in-time raw telemetry. Built by collect_local_snapshot() in a
// DLL process or collect_server_snapshot(Server&) in the daemon, then
// formatted into ADS_MGMT_* structs by MgCollector.
struct MgSnapshot {
    std::uint32_t users           = 0;
    std::uint32_t connections     = 0;
    std::uint32_t workareas       = 0;
    std::uint32_t tables          = 0;
    std::uint32_t indexes         = 0;
    std::uint32_t locks           = 0;
    std::uint32_t worker_threads  = 0;
    std::uint16_t server_type     = 0;   // 0 = unknown/local
    std::uint64_t rss_bytes       = 0;   // RSS of the reporting process
    std::uint16_t server_port     = 0;   // listener port (0 = local)

    // Cumulative / historical telemetry — captured from the reporting
    // process's MgStats so it travels the wire with the live counts
    // (remote callers must not substitute their own MgStats).
    std::uint64_t uptime_seconds   = 0;
    std::uint64_t packets_in       = 0;
    std::uint64_t packets_out      = 0;
    std::uint64_t bytes_in         = 0;
    std::uint64_t bytes_out        = 0;
    std::uint64_t disconnects      = 0;
    std::uint64_t partial_connects = 0;
    std::uint64_t operations       = 0;
    std::uint64_t logged_errors    = 0;
    std::uint32_t max_users        = 0;
    std::uint32_t max_connections  = 0;
    std::uint32_t max_workareas    = 0;
    std::uint32_t max_tables       = 0;
    std::uint32_t max_indexes      = 0;
    std::uint32_t max_locks        = 0;

    std::vector<MgUser>   user_list;
    std::vector<MgTable>  table_list;
    std::vector<MgIndex>  index_list;
    std::vector<MgLock>   lock_list;
    std::vector<MgThread> thread_list;
};

}  // namespace openads::mgmt
