#include "mgmt/mg_collector.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace openads::mgmt {

namespace {

// Copy `s` into a fixed UNSIGNED8[cap] field, NUL-terminated and
// truncated. Trailing bytes are zeroed so the struct has no
// uninitialised tail.
void put_field(UNSIGNED8* dst, std::size_t cap, const std::string& s) {
    std::memset(dst, 0, cap);
    if (cap == 0) return;
    std::size_t n = std::min(s.size(), cap - 1);
    std::memcpy(dst, s.data(), n);
}

}  // namespace

void capture_mg_stats(MgSnapshot& snap, const MgStats& stats) {
    auto now  = std::chrono::system_clock::now();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                    now - stats.start_time).count();
    snap.uptime_seconds   = secs < 0 ? 0 :
        static_cast<std::uint64_t>(secs);
    snap.packets_in       = stats.packets_in.load();
    snap.packets_out      = stats.packets_out.load();
    snap.bytes_in         = stats.bytes_in.load();
    snap.bytes_out        = stats.bytes_out.load();
    snap.disconnects      = stats.disconnects.load();
    snap.partial_connects = stats.partial_connects.load();
    snap.operations       = stats.operations.load();
    snap.logged_errors    = stats.logged_errors.load();
    snap.max_users        = stats.max_users.load();
    snap.max_connections  = stats.max_connections.load();
    snap.max_workareas    = stats.max_workareas.load();
    snap.max_tables       = stats.max_tables.load();
    snap.max_indexes      = stats.max_indexes.load();
    snap.max_locks        = stats.max_locks.load();
}

MgCollector::MgCollector(MgSnapshot snapshot)
    : snapshot_(std::move(snapshot)) {}

ADS_MGMT_INSTALL_INFO MgCollector::install_info() const {
    ADS_MGMT_INSTALL_INFO info;
    std::memset(&info, 0, sizeof(info));
    info.ulUserOption = 0;
    put_field(info.aucRegisteredOwner, sizeof(info.aucRegisteredOwner),
              "OpenADS");
    put_field(info.aucVersionStr, sizeof(info.aucVersionStr),
              "OpenADS 1.0");
    // aucSerialNumber / aucEvalExpireDate intentionally left empty:
    // OpenADS is not serial-licensed.
    return info;
}

ADS_MGMT_ACTIVITY_INFO MgCollector::activity_info() const {
    ADS_MGMT_ACTIVITY_INFO a;
    std::memset(&a, 0, sizeof(a));

    a.ulOperations   = static_cast<UNSIGNED32>(snapshot_.operations);
    a.ulLoggedErrors = static_cast<UNSIGNED32>(snapshot_.logged_errors);

    long long up = static_cast<long long>(snapshot_.uptime_seconds);
    a.stUpTime.usDays    = static_cast<UNSIGNED16>(up / 86400);
    a.stUpTime.usHours   = static_cast<UNSIGNED16>((up % 86400) / 3600);
    a.stUpTime.usMinutes = static_cast<UNSIGNED16>((up % 3600) / 60);
    a.stUpTime.usSeconds = static_cast<UNSIGNED16>(up % 60);

    auto usage = [](UNSIGNED32 in_use, UNSIGNED32 max_used) {
        ADS_MGMT_USAGE_STRUCT u;
        u.ulInUse   = in_use;
        u.ulMaxUsed = max_used < in_use ? in_use : max_used;
        u.ulRejected = 0;
        return u;
    };

    UNSIGNED32 nusers = static_cast<UNSIGNED32>(snapshot_.user_list.size());
    a.stUsers        = usage(nusers, snapshot_.max_users);
    a.stConnections  = usage(snapshot_.connections, snapshot_.max_connections);
    a.stWorkAreas    = usage(snapshot_.workareas, snapshot_.max_workareas);
    a.stTables       = usage(snapshot_.tables, snapshot_.max_tables);
    a.stIndexes      = usage(snapshot_.indexes, snapshot_.max_indexes);
    a.stLocks        = usage(snapshot_.locks, snapshot_.max_locks);
    a.stWorkerThreads = usage(snapshot_.worker_threads, 0);
    // TPS* elem usage left zero — transaction-processing internals are
    // not exposed.
    return a;
}

ADS_MGMT_COMM_STATS MgCollector::comm_stats() const {
    ADS_MGMT_COMM_STATS s;
    std::memset(&s, 0, sizeof(s));
    s.ulTotalPackets      = static_cast<UNSIGNED32>(
        snapshot_.packets_in + snapshot_.packets_out);
    s.ulDisconnectedUsers = static_cast<UNSIGNED32>(snapshot_.disconnects);
    s.ulPartialConnects   = static_cast<UNSIGNED32>(snapshot_.partial_connects);
    // dPercentCheckSums, ulCheckSumFailures, ulRcvPktOutOfSeq,
    // ulRcvReqOutOfSeq, ulNotLoggedIn, ulInvalidPackets,
    // ulRecvFromErrors, ulSendToErrors — no analogue in OpenADS'
    // TCP framing; left as honest zeros.
    return s;
}

ADS_MGMT_CONFIG_PARAMS MgCollector::config_params() const {
    ADS_MGMT_CONFIG_PARAMS p;
    std::memset(&p, 0, sizeof(p));
    p.ulNumConnections   = snapshot_.connections;
    p.ulNumWorkAreas     = snapshot_.workareas;
    p.ulNumTables        = snapshot_.tables;
    p.ulNumIndexes       = snapshot_.indexes;
    p.ulNumLocks         = snapshot_.locks;
    p.usNumWorkerThreads = static_cast<UNSIGNED16>(
        snapshot_.worker_threads);
    p.usSendIPPort    = snapshot_.server_port;
    p.usReceiveIPPort = snapshot_.server_port;
    // ECB / burst-packet / TPS fields left zero — NetWare-era, no
    // analogue. Send/receive IP ports now carry the real listener
    // port; path strings genuinely remain empty (no analogue).
    return p;
}

ADS_MGMT_CONFIG_MEMORY MgCollector::config_memory() const {
    ADS_MGMT_CONFIG_MEMORY m;
    std::memset(&m, 0, sizeof(m));
    m.ulTotalConfigMem = static_cast<double>(snapshot_.rss_bytes);
    // Per-category fields remain 0 (no allocator instrumentation),
    // but ulTotalConfigMem now carries the real process RSS.
    return m;
}

std::vector<ADS_MGMT_USER_INFO> MgCollector::user_names() const {
    std::vector<ADS_MGMT_USER_INFO> out;
    out.reserve(snapshot_.user_list.size());
    for (const auto& u : snapshot_.user_list) {
        ADS_MGMT_USER_INFO i;
        std::memset(&i, 0, sizeof(i));
        put_field(i.aucUserName, sizeof(i.aucUserName), u.name);
        put_field(i.aucAddress, sizeof(i.aucAddress), u.address);
        put_field(i.aucOSUserLoginName,
                  sizeof(i.aucOSUserLoginName), u.os_login);
        put_field(i.aucAuthUserName,
                  sizeof(i.aucAuthUserName), u.name);
        i.usConnNumber = u.conn_no;
        out.push_back(i);
    }
    return out;
}

std::vector<ADS_MGMT_TABLE_INFO> MgCollector::open_tables() const {
    std::vector<ADS_MGMT_TABLE_INFO> out;
    out.reserve(snapshot_.table_list.size());
    for (const auto& t : snapshot_.table_list) {
        ADS_MGMT_TABLE_INFO i;
        std::memset(&i, 0, sizeof(i));
        put_field(i.aucTableName, sizeof(i.aucTableName), t.name);
        put_field(i.aucUserName, sizeof(i.aucUserName), t.user);
        i.usConnNumber = t.conn_no;
        i.usOpenMode   = t.open_mode;
        i.usLockType   = t.lock_type;
        out.push_back(i);
    }
    return out;
}

std::vector<ADS_MGMT_INDEX_INFO> MgCollector::open_indexes() const {
    std::vector<ADS_MGMT_INDEX_INFO> out;
    out.reserve(snapshot_.index_list.size());
    for (const auto& x : snapshot_.index_list) {
        ADS_MGMT_INDEX_INFO i;
        std::memset(&i, 0, sizeof(i));
        put_field(i.aucIndexName, sizeof(i.aucIndexName), x.name);
        put_field(i.aucTagName, sizeof(i.aucTagName), x.tag);
        put_field(i.aucExpression, sizeof(i.aucExpression),
                  x.expression);
        out.push_back(i);
    }
    return out;
}

std::vector<ADS_MGMT_LOCK_INFO> MgCollector::locks() const {
    std::vector<ADS_MGMT_LOCK_INFO> out;
    out.reserve(snapshot_.lock_list.size());
    for (const auto& l : snapshot_.lock_list) {
        ADS_MGMT_LOCK_INFO i;
        std::memset(&i, 0, sizeof(i));
        put_field(i.aucUserName, sizeof(i.aucUserName), l.user);
        i.usConnNumber   = l.conn_no;
        i.ulRecordNumber = l.recno;
        out.push_back(i);
    }
    return out;
}

std::vector<ADS_MGMT_THREAD_ACTIVITY>
MgCollector::worker_thread_activity() const {
    std::vector<ADS_MGMT_THREAD_ACTIVITY> out;
    out.reserve(snapshot_.thread_list.size());
    for (const auto& t : snapshot_.thread_list) {
        ADS_MGMT_THREAD_ACTIVITY i;
        std::memset(&i, 0, sizeof(i));
        i.ulThreadNumber = t.thread_no;
        i.usOpCode       = t.opcode;
        i.usConnNumber   = t.conn_no;
        put_field(i.aucUserName, sizeof(i.aucUserName), t.user);
        put_field(i.aucOSUserLoginName,
                  sizeof(i.aucOSUserLoginName), t.os_login);
        out.push_back(i);
    }
    return out;
}

ADS_MGMT_LOCK_INFO MgCollector::lock_owner(std::uint32_t recno) const {
    ADS_MGMT_LOCK_INFO i;
    std::memset(&i, 0, sizeof(i));
    for (const auto& l : snapshot_.lock_list) {
        if (l.recno == recno) {
            put_field(i.aucUserName, sizeof(i.aucUserName), l.user);
            i.usConnNumber   = l.conn_no;
            i.ulRecordNumber = l.recno;
            break;
        }
    }
    return i;
}

// Process-global MgStats singleton. start_time is fixed the first
// time this is called; the server overwrites it at Server::start().
MgStats& process_mg_stats() {
    static MgStats g_stats;
    return g_stats;
}

namespace {
thread_local LockOwner g_current_lock_owner;
}  // namespace

void set_current_lock_owner(const std::string& user, std::uint16_t conn_no) {
    g_current_lock_owner.user    = user;
    g_current_lock_owner.conn_no = conn_no;
}

LockOwner current_lock_owner() { return g_current_lock_owner; }

LockRegistry& LockRegistry::instance() {
    static LockRegistry g_registry;
    return g_registry;
}

void LockRegistry::add_record_lock(const void* table_key, std::uint32_t recno) {
    std::lock_guard<std::mutex> lk(mu_);
    record_locks_[RecKey{table_key, recno}] = current_lock_owner();
}

void LockRegistry::remove_record_lock(const void* table_key, std::uint32_t recno) {
    std::lock_guard<std::mutex> lk(mu_);
    record_locks_.erase(RecKey{table_key, recno});
}

void LockRegistry::add_table_lock(const void* table_key) {
    std::lock_guard<std::mutex> lk(mu_);
    table_locks_[table_key] = current_lock_owner();
}

void LockRegistry::remove_table_lock(const void* table_key) {
    std::lock_guard<std::mutex> lk(mu_);
    table_locks_.erase(table_key);
}

void LockRegistry::remove_all_for_table(const void* table_key) {
    std::lock_guard<std::mutex> lk(mu_);
    table_locks_.erase(table_key);
    for (auto it = record_locks_.begin(); it != record_locks_.end(); ) {
        if (it->first.table == table_key) it = record_locks_.erase(it);
        else ++it;
    }
}

std::uint32_t LockRegistry::count() const {
    std::lock_guard<std::mutex> lk(mu_);
    return static_cast<std::uint32_t>(record_locks_.size() + table_locks_.size());
}

std::vector<MgLock> LockRegistry::snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<MgLock> out;
    out.reserve(record_locks_.size() + table_locks_.size());
    for (const auto& [key, owner] : record_locks_) {
        MgLock l;
        l.user   = owner.user;
        l.conn_no = owner.conn_no;
        l.recno  = key.recno;
        out.push_back(std::move(l));
    }
    for (const auto& [key, owner] : table_locks_) {
        (void)key;
        MgLock l;
        l.user    = owner.user;
        l.conn_no = owner.conn_no;
        l.recno   = 0;  // whole-table lock
        out.push_back(std::move(l));
    }
    return out;
}

}  // namespace openads::mgmt
