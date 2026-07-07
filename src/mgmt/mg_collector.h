#pragma once

#include "mgmt/mg_snapshot.h"
#include "mgmt/mg_stats.h"
#include "openads/ace.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace openads::engine { class DataDict; }

namespace openads::mgmt {

// Copy the live MgStats counters (uptime origin, cumulative comm
// totals, high-water marks) into a MgSnapshot. Called by whichever
// process builds the snapshot — server or local DLL — so the
// cumulative telemetry travels the wire alongside the live counts.
void capture_mg_stats(MgSnapshot& snap, const MgStats& stats);

// Formats a raw MgSnapshot into the SAP-canonical ADS_MGMT_* structs
// declared in include/openads/ace.h. Pure: holds a copy of the
// snapshot and never touches global state, so it is trivially
// unit-testable with a fabricated snapshot. The snapshot carries
// everything — live counts AND the captured MgStats values — so a
// remote caller formats the server's telemetry, not its own.
class MgCollector {
public:
    explicit MgCollector(MgSnapshot snapshot);

    ADS_MGMT_INSTALL_INFO   install_info() const;
    ADS_MGMT_ACTIVITY_INFO  activity_info() const;
    ADS_MGMT_COMM_STATS     comm_stats() const;
    ADS_MGMT_CONFIG_PARAMS  config_params() const;
    ADS_MGMT_CONFIG_MEMORY  config_memory() const;

    std::vector<ADS_MGMT_USER_INFO>       user_names() const;
    std::vector<ADS_MGMT_TABLE_INFO>      open_tables() const;
    std::vector<ADS_MGMT_INDEX_INFO>      open_indexes() const;
    std::vector<ADS_MGMT_LOCK_INFO>       locks() const;
    std::vector<ADS_MGMT_THREAD_ACTIVITY> worker_thread_activity() const;

    // Non-SAP extension: one average per-frame cost (microseconds) per
    // user, same order/count as user_names() (both walk the same
    // snapshot_.user_list) — there's no room in ADS_MGMT_USER_INFO's
    // fixed SAP layout for this, so it's exposed via a parallel array
    // instead of widening that struct. See AdsMgGetUserAvgCost.
    std::vector<std::uint32_t>            user_avg_costs() const;

    // On-demand detail for one Active Queries row (see AdsMgGetThreadSql):
    // the SQL text of the last ExecuteSQL frame `thread_no` processed, and
    // when it started (0 if that thread is unknown or never ran SQL).
    // A separate on-demand call rather than eagerly returned alongside
    // worker_thread_activity() because SQL text is unbounded-length and
    // usually only wanted for the one row the user clicked on.
    std::string thread_sql(std::uint32_t thread_no) const;
    std::chrono::system_clock::time_point thread_sql_at(std::uint32_t thread_no) const;

    // Returns the lock held on (conn-agnostic) `recno`; usConnNumber
    // is 0 and ulRecordNumber is 0 when no such lock exists.
    ADS_MGMT_LOCK_INFO lock_owner(std::uint32_t recno) const;

    std::uint16_t server_type() const { return snapshot_.server_type; }

    const MgSnapshot& snapshot() const { return snapshot_; }

private:
    MgSnapshot snapshot_;
};

// One session's identity, for attributing locks taken on its thread.
struct LockOwner {
    std::string   user;
    std::uint16_t conn_no = 0;
};

// Thread-local "who is running on this thread right now". Deep engine
// lock calls (LockMgr/Table) only see a raw Table*, not a user or
// connection — each network session sets this once, right after
// Connect, on its own dedicated thread, so AdsLockRecord/AdsLockTable's
// local branch (which also runs server-side for remote sessions — see
// the session.cpp opcode handlers that call it) can attribute a lock
// without threading a parameter through every call site. True local-mode
// (embedded DLL) callers never set this; lock attribution there falls
// back to the default LockOwner{}, resolved to "(local)"/1 by the caller,
// matching the rest of local-mode mgmt (see fetch_mg_snapshot in
// ace_exports.cpp).
void       set_current_lock_owner(const std::string& user, std::uint16_t conn_no);
LockOwner  current_lock_owner();

// Process-global registry of currently-held record/table locks, keyed by
// the owning Table's address (stable for the table's lifetime; entries
// are dropped by AdsCloseTable so a later allocation reusing the address
// can't inherit stale locks). ADS_MGMT_LOCK_INFO/MgLock carry no table
// name, matching SAP's own struct — only user/conn_no/recno.
class LockRegistry {
public:
    static LockRegistry& instance();

    void add_record_lock(const void* table_key, std::uint32_t recno);
    void remove_record_lock(const void* table_key, std::uint32_t recno);
    void add_table_lock(const void* table_key);
    void remove_table_lock(const void* table_key);
    // Safety net for abrupt disconnects that skip explicit unlocks.
    void remove_all_for_table(const void* table_key);

    std::uint32_t     count() const;
    std::vector<MgLock> snapshot() const;

private:
    struct RecKey {
        const void*   table;
        std::uint32_t recno;
        bool operator==(const RecKey& o) const noexcept {
            return table == o.table && recno == o.recno;
        }
    };
    struct RecKeyHash {
        std::size_t operator()(const RecKey& k) const noexcept {
            return std::hash<const void*>()(k.table) ^
                   (std::hash<std::uint32_t>()(k.recno) << 1);
        }
    };

    mutable std::mutex                                    mu_;
    std::unordered_map<RecKey, LockOwner, RecKeyHash>     record_locks_;
    std::unordered_map<const void*, LockOwner>            table_locks_;
};

// Reserved admin username exempted from the ADS_DD_LOGINS_DISABLED gate
// (see ace_exports.cpp's AdsConnect / session.cpp's Connect handler) so
// setting that flag is never a one-way door. "adssys" is ADS's own
// conventional administrator username, already used as the default admin
// user throughout this project's test dictionaries and config.
constexpr const char* kAdminBypassUser = "adssys";

// True if `user`/`pwd` are the reserved admin user's real credentials for
// `dd` — i.e. this connection should be let through even when
// ADS_DD_LOGINS_DISABLED is set. Case-insensitive on the username, to
// match how it's conventionally typed ("adssys", "AdsSys", ...).
bool is_admin_bypass(openads::engine::DataDict* dd,
                     const std::string& user, const std::string& pwd);

}  // namespace openads::mgmt
