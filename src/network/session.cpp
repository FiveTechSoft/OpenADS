#include "network/session.h"

#include "openads_version.h"  // OPENADS_VERSION_STR (CMake-generated)

#include "engine/aof_eval.h"
#include "engine/index_expr.h"
#include "engine/aof_expr.h"
#include "engine/aggregate.h"
#include "engine/record_crc.h"
#include "engine/table.h"
#include "mgmt/error_log.h"
#include "mgmt/mg_collector.h"
#include "mgmt/mg_stats.h"
#include "network/mg_wire.h"
#include "network/mutex_manager.h"
#include "platform/proc.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "abi/lock_retry_policy.h"
#include "engine/server_fs.h"
#include "platform/fs_sandbox.h"
#include "platform/path.h"
#include "session/connection.h"
#include "sql_backend/enterprise_config.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openads::abi {
void set_connection_show_deleted(ADSHANDLE hConnect, bool visible);
void set_connection_legacy_paths(ADSHANDLE hConnect, bool on);
void set_connection_remote_server(ADSHANDLE hConnect, bool on);
void share_connection_resolve_log(ADSHANDLE                  hConnect,
                                  openads::session::Connection* src);
}

namespace openads::network {

static bool wire_trace_on() {
    static const bool on = std::getenv("OPENADS_WIRE_TRACE") != nullptr;
    return on;
}
#define WTRACE(...) do { if (wire_trace_on()) std::fprintf(stderr, __VA_ARGS__); } while (0)

namespace {

inline std::uint16_t read_u16_le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(p[0]) |
        (static_cast<std::uint16_t>(p[1]) << 8));
}

inline std::uint32_t read_u32_le(const std::uint8_t* p) {
    return  static_cast<std::uint32_t>(p[0])        |
           (static_cast<std::uint32_t>(p[1]) <<  8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

inline void write_u16_le(std::uint16_t v, std::vector<std::uint8_t>& out) {
    out.push_back(static_cast<std::uint8_t>( v       & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
}

inline void write_u32_le(std::uint32_t v, std::vector<std::uint8_t>& out) {
    out.push_back(static_cast<std::uint8_t>( v        & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >>  8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

// M12.29 — [u16 len][bytes] string helpers shared by the DD opcode handlers.
// Parses a [u16 len][bytes] field starting at `off`, advancing `off` past
// it. Returns false (payload too short) without touching `out` or `off`
// further than what was already consumed.
inline bool read_lstr16(const std::vector<std::uint8_t>& pl, std::size_t& off,
                        std::string& out) {
    if (off + 2 > pl.size()) return false;
    std::uint16_t len = read_u16_le(pl.data() + off);
    off += 2;
    if (off + len > pl.size()) return false;
    out.assign(reinterpret_cast<const char*>(pl.data() + off), len);
    off += len;
    return true;
}

inline void write_lstr16(const std::string& s,
                         std::vector<std::uint8_t>& out) {
    auto n = static_cast<std::uint16_t>(s.size());
    out.push_back(static_cast<std::uint8_t>(n & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFFu));
    out.insert(out.end(), s.begin(), s.end());
}

// Null-terminated UNSIGNED8 buffer for a std::string — the AdsDD* ABI takes
// mutable UNSIGNED8* name arguments (never written through) with no const
// C signature available.
inline std::vector<UNSIGNED8> to_cbuf(const std::string& s) {
    std::vector<UNSIGNED8> b(s.size() + 1, 0);
    if (!s.empty()) std::memcpy(b.data(), s.data(), s.size());
    return b;
}

// M12.29 — dispatch a DDGetProperty/DDSetProperty request to the matching
// existing local AdsDDGet*Property/AdsDDSet*Property ABI function. `hConn`
// is the session's server-side abi_conn_ (a real local Connection with the
// DD attached via Session::ensure_abi_conn()) — these calls already work
// correctly locally; this only marshals which one to call.
UNSIGNED32 dd_get_property_dispatch(ADSHANDLE hConn, DDObjectKind kind,
                                    const std::string& name,
                                    const std::string& subName,
                                    UNSIGNED16 propId,
                                    void* pBuf, UNSIGNED16* pusLen) {
    std::vector<UNSIGNED8> nameBuf(name.size() + 1, 0);
    if (!name.empty()) std::memcpy(nameBuf.data(), name.data(), name.size());
    std::vector<UNSIGNED8> subBuf(subName.size() + 1, 0);
    if (!subName.empty()) std::memcpy(subBuf.data(), subName.data(), subName.size());

    switch (kind) {
        case DDObjectKind::Database:
            return AdsDDGetDatabaseProperty(hConn, propId, pBuf, pusLen);
        case DDObjectKind::User:
            return AdsDDGetUserProperty(hConn, nameBuf.data(), propId, pBuf, pusLen);
        case DDObjectKind::Table:
            return AdsDDGetTableProperty(hConn, nameBuf.data(), propId, pBuf, pusLen);
        case DDObjectKind::Field:
            return AdsDDGetFieldProperty(hConn, nameBuf.data(), subBuf.data(),
                                         propId, pBuf, pusLen);
        case DDObjectKind::Trigger:
            return AdsDDGetTriggerProperty(hConn, nameBuf.data(), propId, pBuf, pusLen);
        case DDObjectKind::Proc:
            return AdsDDGetProcProperty(hConn, nameBuf.data(), propId, pBuf, pusLen);
        case DDObjectKind::Function:
            return AdsDDGetFunctionProperty(hConn, nameBuf.data(), propId, pBuf, pusLen);
        case DDObjectKind::View:
            return AdsDDGetViewProperty(hConn, nameBuf.data(), propId, pBuf, pusLen);
        case DDObjectKind::RefIntegrity:
            return AdsDDGetRefIntegrityProperty(hConn, nameBuf.data(), propId, pBuf, pusLen);
        case DDObjectKind::Index:
            return AdsDDGetIndexProperty(hConn, nameBuf.data(), subBuf.data(),
                                         propId, pBuf, pusLen);
        case DDObjectKind::UserTableRights: {
            // Underlying local function returns a raw UNSIGNED32, not a
            // property-id-keyed buffer — pack it into the same [value
            // bytes] wire slot everything else uses (4-byte LE) so this
            // reuses DDGetProperty instead of needing its own opcode.
            UNSIGNED32 level = 0;
            UNSIGNED32 rc = AdsDDGetUserTableRights(hConn, nameBuf.data(),
                                                    subBuf.data(), &level);
            if (rc != 0) return rc;
            if (pusLen != nullptr) {
                UNSIGNED16 cap = *pusLen;
                if (pBuf != nullptr && cap >= 4) {
                    auto* b = static_cast<std::uint8_t*>(pBuf);
                    b[0] = static_cast<std::uint8_t>( level        & 0xFFu);
                    b[1] = static_cast<std::uint8_t>((level >>  8) & 0xFFu);
                    b[2] = static_cast<std::uint8_t>((level >> 16) & 0xFFu);
                    b[3] = static_cast<std::uint8_t>((level >> 24) & 0xFFu);
                }
                *pusLen = 4;
            }
            return openads::AE_SUCCESS;
        }
    }
    return openads::AE_FUNCTION_NOT_AVAILABLE;
}

UNSIGNED32 dd_set_property_dispatch(ADSHANDLE hConn, DDObjectKind kind,
                                    const std::string& name,
                                    const std::string& subName,
                                    UNSIGNED16 propId,
                                    void* pBuf, UNSIGNED16 usLen) {
    std::vector<UNSIGNED8> nameBuf(name.size() + 1, 0);
    if (!name.empty()) std::memcpy(nameBuf.data(), name.data(), name.size());
    std::vector<UNSIGNED8> subBuf(subName.size() + 1, 0);
    if (!subName.empty()) std::memcpy(subBuf.data(), subName.data(), subName.size());

    switch (kind) {
        case DDObjectKind::Database:
            return AdsDDSetDatabaseProperty(hConn, propId, pBuf, usLen);
        case DDObjectKind::User:
            return AdsDDSetUserProperty(hConn, nameBuf.data(), propId, pBuf, usLen);
        case DDObjectKind::Table:
            return AdsDDSetTableProperty(hConn, nameBuf.data(), propId, pBuf, usLen);
        case DDObjectKind::Field:
            return AdsDDSetFieldProperty(hConn, nameBuf.data(), subBuf.data(),
                                         propId, pBuf, usLen);
        case DDObjectKind::Trigger:
            return AdsDDSetTriggerProperty(hConn, nameBuf.data(), propId, pBuf, usLen);
        case DDObjectKind::Proc:
            return AdsDDSetProcProperty(hConn, nameBuf.data(), propId, pBuf, usLen);
        case DDObjectKind::Function:
            return AdsDDSetFunctionProperty(hConn, nameBuf.data(), propId, pBuf, usLen);
        case DDObjectKind::View:
            return AdsDDSetViewProperty(hConn, nameBuf.data(), propId, pBuf, usLen);
        case DDObjectKind::RefIntegrity:
            return AdsDDSetRefIntegrityProperty(hConn, nameBuf.data(), propId, pBuf, usLen);
        case DDObjectKind::Index:
            // AdsDDSetIndexProperty is a permanent local stub regardless of
            // connection type — nothing to forward.
            return openads::AE_FUNCTION_NOT_AVAILABLE;
        case DDObjectKind::UserTableRights: {
            if (pBuf == nullptr || usLen < 4) {
                return openads::AE_INTERNAL_ERROR;
            }
            auto* b = static_cast<const std::uint8_t*>(pBuf);
            UNSIGNED32 level = static_cast<UNSIGNED32>(b[0]) |
                              (static_cast<UNSIGNED32>(b[1]) <<  8) |
                              (static_cast<UNSIGNED32>(b[2]) << 16) |
                              (static_cast<UNSIGNED32>(b[3]) << 24);
            return AdsDDSetUserTableRights(hConn, nameBuf.data(), subBuf.data(), level);
        }
    }
    return openads::AE_FUNCTION_NOT_AVAILABLE;
}

// M12.10 — Error frame payload:
//   [u32 LE ace_code][message bytes]
// Server-side handlers fold either a literal ACE code or a code
// pulled from a sub-result's util::Error into every Error frame
// they emit so the client can surface the right `Ads*` code.
Frame err(const std::string& msg,
          UNSIGNED32 code = openads::AE_INTERNAL_ERROR) {
    // Every error frame returned to a remote client also lands in the
    // SAP-style ads_err error log (mirrors ADS, which records errors the
    // server encounters serving client applications).
    openads::mgmt::ErrorLog::instance().log(
        static_cast<std::int32_t>(code), "NET", 0, msg);
    Frame f;
    f.opcode = Opcode::Error;
    f.payload.resize(4);
    f.payload[0] = static_cast<std::uint8_t>( code        & 0xFFu);
    f.payload[1] = static_cast<std::uint8_t>((code >>  8) & 0xFFu);
    f.payload[2] = static_cast<std::uint8_t>((code >> 16) & 0xFFu);
    f.payload[3] = static_cast<std::uint8_t>((code >> 24) & 0xFFu);
    f.payload.insert(f.payload.end(), msg.begin(), msg.end());
    return f;
}

} // namespace

Session::Session(Server& srv, Socket s, std::string default_data_dir,
                 std::uint16_t listener_port)
    : srv_(&srv), s_(s), sid_(0), default_data_dir_(std::move(default_data_dir)) {
    // studio.web.0.4 — register an entry in the live sessions
    // registry so the Studio "Sessions" tab can list this peer.
    Server::SessionInfo init;
    if (auto pa = socket_peer_addr(s_); pa) {
        init.peer_ip   = pa.value().ip;
        init.peer_port = pa.value().port;
    }
    init.listener_port = listener_port;
    init.connected_at  = std::chrono::system_clock::now();
    init.last_activity = init.connected_at;
    sid_ = srv_->register_session(init);
    srv_->install_session_socket(sid_, s_);
}

Session::~Session() {
    cleanup();
    srv_->erase_session_socket(sid_);
    srv_->unregister_session(sid_);
}

bool Session::process_frame(const Frame& f) {
    srv_->touch_session(sid_, true, false);
    srv_->set_session_opcode(sid_, static_cast<std::uint16_t>(f.opcode));
    {
        // M9.25 — inbound comm telemetry. +5 accounts for the 4-byte
        // length prefix + 1-byte opcode of the wire framing.
        auto& mgst = openads::mgmt::process_mg_stats();
        mgst.packets_in.fetch_add(1, std::memory_order_relaxed);
        mgst.bytes_in.fetch_add(f.payload.size() + 5,
                                std::memory_order_relaxed);
    }
    // Track the actual SQL text for the Active Queries "current query"
    // detail view — mirrors SAP ARC's sp_GetSQLStatements() (see
    // mgtscrn.pas/.dfm in the Advantage Data Architect source). The
    // ExecuteSQL frame's raw payload IS the SQL string.
    if (f.opcode == Opcode::ExecuteSQL) {
        // Iterator-based ctor: payload.data() may be nullptr when empty,
        // and std::string(nullptr, 0) is UB.
        std::string sql(f.payload.begin(), f.payload.end());
        srv_->set_session_sql(sid_, sql);
    }
    srv_->set_session_executing(sid_, true);
    auto t0 = std::chrono::steady_clock::now();
    auto res = dispatch(f);
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0).count();
    srv_->record_session_op_time(sid_, static_cast<std::uint64_t>(micros));
    srv_->set_session_executing(sid_, false);
    if (res.reply) {
        if (auto wr = write_frame(s_, *res.reply); !wr) return false;
        auto& mgst = openads::mgmt::process_mg_stats();
        mgst.packets_out.fetch_add(1, std::memory_order_relaxed);
        // RCB 07/14/2026: bytes_out was declared alongside packets_out but
        // never actually fed, so AdsMgGetCommStats / sp_mgGetCommStats have
        // always reported 0 bytes sent. Found it while trying to MEASURE the
        // read-ahead block size and getting zeroes back. Same +5 framing
        // overhead as bytes_in above.
        mgst.bytes_out.fetch_add(res.reply->payload.size() + 5,
                                 std::memory_order_relaxed);
        srv_->touch_session(sid_, false, true);
    }
    return !res.close_session;
}

bool Session::handle_readable() {
    // Read whatever a single recv yields — a partial frame, one frame, or
    // several — then reassemble and dispatch every complete frame. On a
    // non-blocking socket (reactor pool) an idle or stalled peer returns
    // would-block and we hand the worker straight back to its other
    // connections, so one slow client can't cause head-of-line blocking. On a
    // blocking socket (legacy thread-per-connection loop) recv just waits for
    // the next bytes, preserving the previous one-frame-at-a-time behavior.
    std::uint8_t buf[16384];
    auto r = sock_recv(s_, buf, sizeof(buf));
    if (!r) {
        if (socket_recv_would_block(r.error())) return true;  // nothing right now
        return false;                                         // peer reset / error
    }
    if (r.value() == 0) return false;                         // peer closed cleanly
    auto frames = reader_.feed(buf, r.value());
    if (!frames) return false;                                // malformed framing
    for (const auto& f : frames.value()) {
        if (!process_frame(f)) return false;
    }
    return true;
}

void Session::cleanup() {
    // Tear-down order matters for OS file handles on Windows (no
    // FILE_SHARE_DELETE): indexes first, then shadow ABI tables, then
    // engine tables, then low-level FsFile slots, then the ABI connection.
    // A prior version only closed the ABI shadow (tbls_h_) and left
    // engine tables + index_h_ + files_ to member destruction — that
    // worked for most paths, but a client killed mid-CreateIndex (or
    // after hundreds of open/close cycles) could leave the production
    // .cdx open until openads_serverd itself exited (Pritpal Bedi).

    // 1) Explicit AdsCloseIndex for every wire index handle. AdsCloseTable
    //    purges bindings too, but closing indexes first drops each tag's
    //    File + CDX write-lock join before the table goes away.
    for (auto& [iid, hidx] : index_h_) {
        (void)iid;
        if (hidx != 0) (void)AdsCloseIndex(hidx);
    }
    index_h_.clear();
    index_table_.clear();
    ordered_tables_.clear();
    abi_schema_.clear();
    prefetch_depth_.clear();
    prefetch_run_dir_.clear();

    // 2) SQL cursors (already ABI handles).
    for (auto& [id, h] : cursor_tbls_) {
        (void)id;
        if (h != 0) (void)AdsCloseTable(h);
    }
    cursor_tbls_.clear();

    // 3) Shadow ABI twins (production CDX auto-open + CreateIndex live here).
    for (auto& [id, h] : tbls_h_) {
        (void)id;
        if (h != 0) (void)AdsCloseTable(h);
    }
    tbls_h_.clear();

    // 4) Engine tables held by the session Connection. close_table erases
    //    the unique_ptr so the driver's File closes immediately — not after
    //    the Session member destructor runs (which is too late if anything
    //    else still references the path, and skips LockRegistry cleanup).
    if (sess_conn_) {
        for (auto& [id, h] : tbls_) {
            (void)id;
            if (auto* t = sess_conn_->lookup_table(h)) {
                (void)t->flush();
                openads::mgmt::LockRegistry::instance().remove_all_for_table(t);
            }
            sess_conn_->close_table(h);
            srv_->add_session_table(sid_, -1);
        }
    }
    tbls_.clear();
    tbl_open_paths_.clear();

    // 5) oads_FOpen / AdsFOpen files for this session.
    files_.clear();

    // 6) ABI SQL statement + connection (AdsDisconnect closes any leftover
    //    tables that somehow escaped the loops above).
    if (abi_stmt_ != 0) {
        (void)AdsCloseSQLStatement(abi_stmt_);
        abi_stmt_ = 0;
    }
    if (abi_conn_ != 0) {
        (void)AdsDisconnect(abi_conn_);
        abi_conn_ = 0;
    }
}

// M12.16 — lazy-init the per-session ABI connection (same one
// M12.7 SQL exec uses).
bool Session::ensure_abi_conn() {
    if (abi_conn_ != 0) return true;
    if (!sess_conn_) return false;
    // Prefer the full .add path so the ABI connection inherits the DD.
    const std::string& conn_path = sess_conn_->dd_path().empty()
        ? sess_conn_->data_dir() : sess_conn_->dd_path();
    std::vector<UNSIGNED8> srvbuf(conn_path.size() + 1);
    std::memcpy(srvbuf.data(), conn_path.c_str(), conn_path.size() + 1);
    // RCB 06/30/2026: The server creates this ABI handle lazily for remote
    // SQL/index operations. Reuse the original DD login so login-required
    // dictionaries do not fail after the first remote Connect succeeds.
    std::vector<UNSIGNED8> userbuf;
    std::vector<UNSIGNED8> pwbuf;
    auto make_arg = [](const std::string& s, std::vector<UNSIGNED8>& out)
        -> UNSIGNED8* {
        if (s.empty()) return nullptr;
        out.resize(s.size() + 1);
        std::memcpy(out.data(), s.c_str(), s.size() + 1);
        return out.data();
    };
    UNSIGNED8* user = make_arg(session_user_, userbuf);
    UNSIGNED8* pw = make_arg(session_password_, pwbuf);
    UNSIGNED32 rc = AdsConnect60(srvbuf.data(), ADS_LOCAL_SERVER,
                                 user, pw, 0, &abi_conn_);
    if (rc == 0 && abi_conn_ != 0) {
        // --legacy-paths: the ABI twin must fold client-absolute paths
        // (CreateTable/Reindex/SQL go through it) the same way the
        // session's engine Connection does.
        openads::abi::set_connection_legacy_paths(abi_conn_,
                                                  srv_->legacy_paths());
        openads::abi::set_connection_remote_server(abi_conn_, true);
        // One client session = one RESOLVED audit line per file: the
        // twin re-opens tables for index/SQL work, so dedup against the
        // engine connection's set, not its own (Pritpal Bedi, Aug 2026).
        openads::abi::share_connection_resolve_log(abi_conn_,
                                                   sess_conn_.get());
        // Force canonical YYYYMMDD for all date field reads performed
        // through ABI handles on the server (pack_row, GetField, etc).
        // This makes AdsGetJulian / date FieldGet return usable values
        // over remote and matches the engine's internal 8-digit strings.
        (void)AdsSetDateFormat((UNSIGNED8*)"YYYYMMDD");
        // M12.32 — a ShowDeleted opcode may have arrived before this
        // lazy connection existed; new connections default to "show".
        if (!show_deleted_) {
            openads::abi::set_connection_show_deleted(abi_conn_, false);
        }
    }
    return rc == 0;
}

// M12.16 — open a parallel ABI handle alongside the engine
// handle in tbls_[id], stash in tbls_h_[id]. The engine handle
// stays open so subsequent navigation handlers (GotoTop/Skip/…)
// keep their existing path; only index operations route
// through the ABI handle. After an index op that moves the
// cursor (Seek / SeekLast) the helper sync_engine_cursor
// re-positions the engine cursor to the same recno so the two
// states never drift. SQL cursor handles (cursor_tbls_) are
// returned as-is since they are already ABI-side.
ADSHANDLE Session::ensure_abi_handle(std::uint32_t id) {
    if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
        return cit->second;
    }
    if (auto hit = tbls_h_.find(id); hit != tbls_h_.end()) {
        return hit->second;
    }
    auto eit = tbls_.find(id);
    if (eit == tbls_.end() || !sess_conn_) return 0;
    if (!ensure_abi_conn()) return 0;
    auto* tbl = sess_conn_->lookup_table(eit->second);
    if (!tbl) return 0;
    // Reopen with the same name the client used (e.g. "orders/work.dbf"),
    // not basename-only — otherwise production CDX auto-open binds against
    // the wrong table when data files live in subdirectories.
    std::string open_name;
    if (auto pit = tbl_open_paths_.find(id); pit != tbl_open_paths_.end()) {
        open_name = pit->second;
    }
    if (open_name.empty()) {
        std::filesystem::path abs(tbl->path());
        std::filesystem::path base(sess_conn_->data_dir());
        std::error_code ec;
        auto rel = std::filesystem::relative(abs, base, ec);
        if (!ec && !rel.empty() && rel != ".") {
            open_name = rel.generic_string();
        } else {
            open_name = abs.filename().string();
        }
    }
    std::vector<UNSIGNED8> nb(open_name.size() + 1);
    std::memcpy(nb.data(), open_name.data(), open_name.size());
    // Match the engine table's share mode so the twin does not upgrade a
    // shared open to exclusive (or vice versa). map_open_mode(0) is Shared;
    // Exclusive/Read map from the engine OpenMode.
    UNSIGNED16 us_mode = ADS_SHARED;
    switch (tbl->open_mode()) {
        case openads::engine::OpenMode::Exclusive:
            us_mode = ADS_EXCLUSIVE; break;
        case openads::engine::OpenMode::Read:
            us_mode = ADS_READONLY; break;
        default:
            us_mode = ADS_SHARED; break;
    }
    ADSHANDLE h = 0;
    WTRACE("[wire] ensure_abi_handle id=%u open_name='%s' mode=%u eng_path='%s' eng_recs=%u\n",
           id, open_name.c_str(), (unsigned)us_mode,
           tbl->path().c_str(), (unsigned)tbl->record_count());
    UNSIGNED32 open_rrc = AdsOpenTable(abi_conn_, nb.data(), nullptr,
                     ADS_CDX, 0, 0, 0, us_mode, &h);
    if (open_rrc != 0) {
        WTRACE("[wire] ensure_abi_handle id=%u AdsOpenTable FAILED rrc=%u\n",
               id, (unsigned)open_rrc);
        return 0;
    }
    // Position the twin so pack_row_trailer sees a live cursor (not Limbo).
    UNSIGNED32 gt_rc = AdsGotoTop(h);
    UNSIGNED16 _b = 9, _e = 9;
    AdsAtBOF(h, &_b); AdsAtEOF(h, &_e);
    UNSIGNED32 twin_recs = 0;
    AdsGetRecordCount(h, 0, &twin_recs);
    WTRACE("[wire] ensure_abi_handle id=%u CDX open: bof=%u eof=%u twin_recs=%u\n",
           id, (unsigned)_b, (unsigned)_e, (unsigned)twin_recs);
    // Fallback: if GotoTop left the twin in Limbo (both bof and eof true),
    // the ACE CDX implementation may disagree with the engine's CDX on the
    // same file. Close and reopen as a plain DBF (no CDX), then try to
    // attach the index explicitly. If even that fails, we still have a
    // working cursor in natural record order — better than Limbo.
    if (_b && _e) {
        WTRACE("[wire] ensure_abi_handle id=%u Limbo after CDX open, retrying without CDX eng_recs=%u\n",
               id, (unsigned)tbl->record_count());
        AdsCloseTable(h);
        h = 0;
        open_rrc = AdsOpenTable(abi_conn_, nb.data(), nullptr,
                         ADS_TABLE, 0, 0, 0, us_mode, &h);
        if (open_rrc != 0) {
            WTRACE("[wire] ensure_abi_handle id=%u AdsOpenTable(ADS_TABLE) FAILED rrc=%u\n",
                   id, (unsigned)open_rrc);
            return 0;
        }
        // Try to attach the production index explicitly.
        ADSHANDLE hIdx = 0;
        UNSIGNED16 idxCount = 0;
        // Build the CDX path from the DBF path.
        std::string cdx_name;
        {
            std::filesystem::path p(open_name);
            cdx_name = p.replace_extension(".cdx").generic_string();
        }
        std::vector<UNSIGNED8> cdx_nb(cdx_name.size() + 1);
        std::memcpy(cdx_nb.data(), cdx_name.data(), cdx_name.size());
        UNSIGNED32 idx_rc = AdsOpenIndex(h, cdx_nb.data(), &hIdx, &idxCount);
        if (idx_rc == 0 && idxCount > 0) {
            WTRACE("[wire] ensure_abi_handle id=%u CDX attached via ADS_TABLE fallback idx=%u\n",
                   id, (unsigned)idxCount);
        } else {
            WTRACE("[wire] ensure_abi_handle id=%u no CDX (rc=%u count=%u), natural order\n",
                   id, (unsigned)idx_rc, (unsigned)idxCount);
        }
        gt_rc = AdsGotoTop(h);
        AdsAtBOF(h, &_b); AdsAtEOF(h, &_e);
        AdsGetRecordCount(h, 0, &twin_recs);
        WTRACE("[wire] ensure_abi_handle id=%u TABLE fallback: bof=%u eof=%u twin_recs=%u\n",
               id, (unsigned)_b, (unsigned)_e, (unsigned)twin_recs);
    }
    WTRACE("[wire] ensure_abi_handle id=%u opened h=%llu gt_rc=%u bof=%u eof=%u twin_recs=%u\n",
           id, (unsigned long long)h, (unsigned)gt_rc, (unsigned)_b, (unsigned)_e, (unsigned)twin_recs);
    tbls_h_[id] = h;
    return h;
}

// M12.18 — pack the current record (recno + deleted + per-field
// value bytes) onto the tail of any nav-op ack so the client
// populates RemoteTable's row cache in the same RTT as the nav
// itself.
// Pack one record's bytes into `dst` at the current cursor.
// Returns false on EoF / unread error so the caller can stop
// walking lookahead.
bool Session::pack_one_row_engine(std::vector<std::uint8_t>& dst,
                                  openads::engine::Table* tbl) {
    if (!tbl || tbl->eof() || tbl->bof() || tbl->recno() == 0) {
        return false;
    }
    auto write_u32_p = [&](std::uint32_t v) {
        dst.push_back(static_cast<std::uint8_t>( v        & 0xFFu));
        dst.push_back(static_cast<std::uint8_t>((v >>  8) & 0xFFu));
        dst.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
        dst.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
    };
    auto write_u16_p = [&](std::uint16_t v) {
        dst.push_back(static_cast<std::uint8_t>( v       & 0xFFu));
        dst.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    };
    write_u32_p(tbl->recno());
    dst.push_back(tbl->is_deleted() ? 1 : 0);
    auto nf = static_cast<std::uint16_t>(tbl->field_count());
    write_u16_p(nf);
    for (std::uint16_t i = 0; i < nf; ++i) {
        auto v = tbl->read_field(i);
        std::string vv = v ? v.value().as_string : std::string();
        write_u32_p(static_cast<std::uint32_t>(vv.size()));
        dst.insert(dst.end(), vv.begin(), vv.end());
    }
    return true;
}

// Field names + memo-ness for one open ABI handle, resolved once. The schema
// of an open table cannot change under us, so this is a pure win: it takes
// the two per-column ABI lookups (name, type) out of the per-row path, which
// a 64-row lookahead block walks 64 times.
const std::vector<Session::AbiField>&
Session::abi_schema_for(ADSHANDLE h_abi) {
    auto it = abi_schema_.find(h_abi);
    if (it != abi_schema_.end()) return it->second;

    std::vector<AbiField> cols;
    UNSIGNED16 nf = 0;
    AdsGetNumFields(h_abi, &nf);
    cols.reserve(nf);
    // RCB 07/15/2026: on `cap` and the memcpy — this is NOT the unbounded
    // "ACE writes back the required size" pattern that would overflow nm[64].
    // These AdsGetFieldName/AdsGetField calls run against a local server-side
    // handle and land in copy_to_caller() (src/abi/charset.cpp), which writes
    // back cap = min(name_len, buf-1) and never a value >= the buffer. So the
    // memcpy is bounded by construction. One record per column, always the full
    // count: do NOT rewrite this to skip a column on an (unreachable, i = 1..nf)
    // failure — cols.size() is sent as the row's field count in
    // pack_one_row_abi, so a short cols would desync the wire row.
    for (UNSIGNED16 i = 1; i <= nf; ++i) {
        AbiField fd;
        UNSIGNED8  nm[64] = {0};
        UNSIGNED16 cap = sizeof(nm);
        AdsGetFieldName(h_abi, i, nm, &cap);
        if (cap > sizeof(nm)) cap = sizeof(nm);   // belt-and-suspenders; see above
        fd.name.assign(cap + 1, 0);
        std::memcpy(fd.name.data(), nm, cap);
        UNSIGNED16 ftype = 0;
        AdsGetFieldType(h_abi, fd.name.data(), &ftype);
        fd.is_memo = (ftype == ADS_MEMO ||
                      ftype == ADS_BINARY ||
                      ftype == ADS_IMAGE);
        cols.push_back(std::move(fd));
    }
    return abi_schema_.emplace(h_abi, std::move(cols)).first->second;
}

bool Session::pack_one_row_abi(std::vector<std::uint8_t>& dst,
                               ADSHANDLE h_abi) {
    UNSIGNED16 atend = 0;
    AdsAtEOF(h_abi, &atend);
    if (atend) return false;
    auto write_u32_p = [&](std::uint32_t v) {
        dst.push_back(static_cast<std::uint8_t>( v        & 0xFFu));
        dst.push_back(static_cast<std::uint8_t>((v >>  8) & 0xFFu));
        dst.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
        dst.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
    };
    auto write_u16_p = [&](std::uint16_t v) {
        dst.push_back(static_cast<std::uint8_t>( v       & 0xFFu));
        dst.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    };
    UNSIGNED32 rn = 0;
    AdsGetRecordNum(h_abi, 0, &rn);
    write_u32_p(rn);
    UNSIGNED16 del = 0;
    AdsIsRecordDeleted(h_abi, &del);
    dst.push_back(del != 0 ? 1 : 0);
    const auto& cols = abi_schema_for(h_abi);
    write_u16_p(static_cast<std::uint16_t>(cols.size()));
    std::vector<UNSIGNED8> vbuf;
    for (const auto& fd : cols) {
        // Non-memo values fit the fixed DBF field width; 4096 covers every
        // scalar type the ABI hands back. Memos are sized from the engine.
        UNSIGNED32 vcap = 4096;
        if (fd.is_memo) {
            UNSIGNED32 mlen = 0;
            if (AdsGetMemoLength(h_abi,
                    const_cast<UNSIGNED8*>(fd.name.data()), &mlen) != 0) {
                mlen = 0;
            }
            vcap = mlen + 1;
        }
        vbuf.assign(vcap, 0);
        if (AdsGetField(h_abi, const_cast<UNSIGNED8*>(fd.name.data()),
                        vbuf.data(), &vcap, 0) != 0) {
            vcap = 0;
        }
        write_u32_p(vcap);
        if (vcap > 0) {
            dst.insert(dst.end(), vbuf.data(), vbuf.data() + vcap);
        }
    }
    return true;
}

void Session::pack_row_trailer(Frame& reply, std::uint32_t id,
                               std::uint16_t lookahead_n,
                               std::int8_t dir) {
    auto write_u16_p = [&](std::uint16_t v,
                            std::vector<std::uint8_t>& dst) {
        dst.push_back(static_cast<std::uint8_t>( v       & 0xFFu));
        dst.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    };
    // Resolve the table once; remember which path applies for
    // both the current row and the lookahead block below.
    openads::engine::Table* eng_tbl = nullptr;
    ADSHANDLE               h_abi   = 0;
    if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
        h_abi = cit->second;
    } else if (ordered_tables_.count(id) != 0) {
        h_abi = ensure_abi_handle(id);
        // Also resolve the engine table — we need it for fallback reads
        // when the ABI twin is in Limbo (ACE's CDX may disagree with the
        // engine's CDX on index state, leaving the twin at bof=eof=1).
        if (auto eit = tbls_.find(id); eit != tbls_.end() && sess_conn_) {
            eng_tbl = sess_conn_->lookup_table(eit->second);
        }
    } else if (auto eit = tbls_.find(id); eit != tbls_.end() && sess_conn_) {
        eng_tbl = sess_conn_->lookup_table(eit->second);
    } else if (auto hit = tbls_h_.find(id); hit != tbls_h_.end()) {
        h_abi = hit->second;
    }
    if (!eng_tbl && h_abi == 0) {
        reply.payload.push_back(0);
        return;
    }
    if (h_abi != 0) {
        UNSIGNED16 _b = 9, _e = 9; AdsAtBOF(h_abi, &_b); AdsAtEOF(h_abi, &_e);
        // Rescue: if the twin landed in Limbo (bof=1 eof=1) — e.g. from a
        // cached handle opened when the table was empty — reposition it now.
        if (_b && _e) {
            AdsGotoTop(h_abi);
            AdsAtBOF(h_abi, &_b); AdsAtEOF(h_abi, &_e);
            if (_b && _e) AdsGotoBottom(h_abi);
            AdsAtBOF(h_abi, &_b); AdsAtEOF(h_abi, &_e);
            WTRACE("[wire] pack enter id=%u Limbo rescue bof=%u eof=%u\n",
                   id, (unsigned)_b, (unsigned)_e);
        }
        WTRACE("[wire] pack enter id=%u twin bof=%u eof=%u\n", id, (unsigned)_b, (unsigned)_e);
    }
    // Pack the current row.
    bool current_packed = false;
    if (eng_tbl) {
        // If the ABI twin is in Limbo (bof=eof=1) but we have the engine
        // table, sync the engine to the ABI twin's recno and read from engine.
        // ACE's CDX may disagree with the engine's CDX on index state.
        if (h_abi != 0) {
            UNSIGNED16 _b2 = 9, _e2 = 9;
            AdsAtBOF(h_abi, &_b2); AdsAtEOF(h_abi, &_e2);
            if (_b2 && _e2) {
                UNSIGNED32 rn = 0;
                AdsGetRecordNum(h_abi, 0, &rn);
                if (rn > 0 && eng_tbl->record_count() >= rn) {
                    eng_tbl->goto_record(rn);
                    WTRACE("[wire] pack enter id=%u Limbo fallback engine recno=%u\n",
                           id, (unsigned)rn);
                } else {
                    // Twin recno unavailable (Limbo); use engine's current pos.
                    WTRACE("[wire] pack enter id=%u Limbo, engine recno=%u\n",
                           id, (unsigned)eng_tbl->recno());
                }
            }
        }
        if (eng_tbl->eof() || eng_tbl->bof() || eng_tbl->recno() == 0) {
            reply.payload.push_back(0);
        } else {
            reply.payload.push_back(1);
            std::vector<std::uint8_t> tmp;
            if (pack_one_row_engine(tmp, eng_tbl)) {
                reply.payload.insert(reply.payload.end(),
                    tmp.begin(), tmp.end());
                current_packed = true;
            }
        }
    } else {
        // BOF must report has_row=0 just like EOF: with a row on the
        // trailer the client treats the BOF landing as a valid position,
        // and its boundary detection (which relies on a pristine
        // row_valid_before) then reports Bof()=.F. on the first backward
        // skip at the top — xBrowse counts one extra row above and paints
        // a phantom duplicate (Tim Stone, 1-record scope). It also lets
        // the lookahead walk run at BOF, whose restore step clears the
        // twin's BOF flag. Field reads at BOF still work: they miss the
        // cache and cost one round trip, exactly like EOF.
        UNSIGNED16 atend = 0, atbeg = 0;
        AdsAtEOF(h_abi, &atend);
        AdsAtBOF(h_abi, &atbeg);
        if (atend || atbeg) {
            reply.payload.push_back(0);
        } else {
            reply.payload.push_back(1);
            std::vector<std::uint8_t> tmp;
            if (pack_one_row_abi(tmp, h_abi)) {
                reply.payload.insert(reply.payload.end(),
                    tmp.begin(), tmp.end());
                current_packed = true;
            }
        }
    }
    // M12.21 lookahead block. Walk Skip(dir) up to lookahead_n times capturing
    // each row, then Skip(-dir * advance) back so the cursor lands at the same
    // spot the lone Skip the caller actually issued would have produced.
    // RCB 07/15/2026: M12.25 — `dir` is +1 for a forward block, -1 for a
    // backward (PgUp) one. The walk and the restore are symmetric in it.
    if (!current_packed || lookahead_n == 0 || dir == 0) {
        // No lookahead either way — emit a count of 0 so the
        // wire format always carries the field. Old clients
        // (M12.18) ignore the extra bytes.
        write_u16_p(0, reply.payload);
        return;
    }
    const SIGNED32 walk = dir;             // +1 or -1, one step per iteration
    std::vector<std::vector<std::uint8_t>> rows;
    rows.reserve(lookahead_n);
    std::uint16_t taken = 0;
    // Track cursor moves separately from rows packed: a Skip that lands on
    // EoF/BoF still moves the cursor, even though no row gets packed. The
    // restore step at the end has to undo every cursor advance, packed-row or
    // not, otherwise the caller-visible cursor lands a row past where the lone
    // Skip it issued would have produced.
    int cursor_advance = 0;
    // RCB 07/14/2026: the row count is only half the bound — see
    // kPrefetchMaxBytes. Stop as soon as the block reaches the byte budget so a
    // wide table cannot turn a 64-row lookahead into a multi-hundred-KB frame.
    // Checked AFTER packing each row on purpose: that way we always send at
    // least one lookahead row even when a single row is bigger than the whole
    // budget, instead of silently degrading to no read-ahead on exactly the
    // tables where a round-trip costs the most.
    std::size_t block_bytes = 0;
    for (std::uint16_t i = 0; i < lookahead_n; ++i) {
        if (eng_tbl) {
            auto sk = eng_tbl->skip(walk);
            if (!sk) break;
            ++cursor_advance;
            std::vector<std::uint8_t> row;
            if (!pack_one_row_engine(row, eng_tbl)) break;
            block_bytes += row.size();
            rows.push_back(std::move(row));
            ++taken;
        } else {
            if (AdsSkip(h_abi, walk) != 0) break;
            ++cursor_advance;
            // A backward walk stops at BoF, a forward one at EoF; pack_one_row_*
            // already refuses to pack an off-record cursor, but check the
            // relevant boundary first so we don't pack a phantom row.
            UNSIGNED16 atend = 0;
            if (dir > 0) AdsAtEOF(h_abi, &atend); else AdsAtBOF(h_abi, &atend);
            if (atend) break;
            std::vector<std::uint8_t> row;
            if (!pack_one_row_abi(row, h_abi)) break;
            block_bytes += row.size();
            rows.push_back(std::move(row));
            ++taken;
        }
        if (block_bytes >= kPrefetchMaxBytes) break;
    }
    if (cursor_advance > 0) {
        // Undo every advance: we stepped `walk` cursor_advance times, so step
        // back by -walk * cursor_advance.
        const SIGNED32 restore =
            -walk * static_cast<SIGNED32>(cursor_advance);
        if (eng_tbl) {
            (void)eng_tbl->skip(restore);
        } else {
            (void)AdsSkip(h_abi, restore);
        }
    }
    write_u16_p(taken, reply.payload);
    for (auto& r : rows) {
        reply.payload.insert(reply.payload.end(), r.begin(), r.end());
    }
}

// M12.22/M12.23 — read-ahead depth for one forward Skip.
//
// `hint` is what the client asked for via AdsCacheRecords, or
// kPrefetchDepthAuto when it never called it (and for any pre-M12.23 client,
// whose Skip frame carries no depth field at all).
//
//   * explicit hint: honour it. The caller knows something we cannot infer
//     from the access pattern — SAP's documented cases are "0 or 1 turns
//     read-ahead off" (a batch loop that edits most records it visits, where
//     every edit would dump the block anyway) and "aggressive = 100" (a
//     one-directional sweep). Capped at kPrefetchDepthMax so one request
//     can't become an unbounded server-side scan.
//   * auto: ramp. One step per consecutive forward Skip on the table,
//     8 -> 16 -> 32 -> 64, then held. Anything that breaks the sequential run
//     (reposition, write, order change) erases the entry, so the next run
//     starts at the floor again — which is what keeps a one-off "seek a
//     record and read it" from dragging a full block it will never look at.
std::uint16_t Session::next_lookahead(std::uint32_t id, std::uint16_t hint,
                                      std::int8_t dir) {
    if (!client_prefetch_ok_) return 0;
    if (hint != kPrefetchDepthAuto) {
        // SAP: "A usRecords value of 0 (or 1) effectively turns read-ahead
        // record caching off." 1 means "just the current row", which is
        // exactly a zero-length lookahead block.
        if (hint <= 1) return 0;
        return hint > kPrefetchDepthMax ? kPrefetchDepthMax : hint;
    }
    // RCB 07/15/2026: M12.25 — a direction reversal (PgDn then PgUp) is a fresh
    // run, so restart the ramp at the floor. Otherwise the first backward block
    // after a long forward scan would arrive at the ceiling and over-fetch the
    // same way an unramped forward scan did.
    if (auto dit = prefetch_run_dir_.find(id);
        dit != prefetch_run_dir_.end() && dit->second != dir) {
        prefetch_depth_.erase(id);
    }
    prefetch_run_dir_[id] = dir;
    auto [it, fresh] = prefetch_depth_.try_emplace(id, kPrefetchFloor);
    if (!fresh) {
        std::uint32_t grown = static_cast<std::uint32_t>(it->second) * 2u;
        it->second = static_cast<std::uint16_t>(
            grown > kPrefetchCeil ? kPrefetchCeil : grown);
    }
    return it->second;
}

void Session::reset_lookahead(std::uint32_t id) {
    prefetch_depth_.erase(id);
    prefetch_run_dir_.erase(id);
}

namespace {

// RCB 07/14/2026: opcodes that end a sequential read-ahead run on the table
// they name. All of them either move the cursor somewhere the block was not
// read from, or change what the rows mean (order / scope / filter), or write.
//
// Why a central list instead of a reset_lookahead() call inside each handler:
// dispatch() can break the run in ONE place rather than in twenty handlers, and
// twenty hand-placed calls is twenty chances to forget one when a new opcode
// lands. The leading u32 of the payload names the affected object — but see
// break_key_is_index_id() below: for the index-scoped ops that u32 is an INDEX
// id, not a table id, and has to be resolved before it means anything to
// prefetch_depth_ (which is keyed by table).
//
// Forgetting an opcode here (or mis-keying one) is cheap by construction, which
// is the property that makes the central approach safe: it is a TUNING bug,
// never a correctness bug. The lookahead rows are always walked fresh from the
// post-op cursor, so the worst case is a Skip that carries a deeper block than
// it should have.
bool breaks_prefetch_run(Opcode op) {
    switch (op) {
        case Opcode::GotoTop:
        case Opcode::GotoBottom:
        case Opcode::GotoRecord:
        case Opcode::RefreshRecord:
        case Opcode::Seek:
        case Opcode::SeekLast:
        case Opcode::SkipUnique:
        case Opcode::SetOrder:
        case Opcode::SetOrderByName:
        case Opcode::SetScope:
        case Opcode::ClearScope:
        case Opcode::SetAOF:
        case Opcode::CustomizeAOF:
        case Opcode::SetField:
        case Opcode::SetRecord:
        case Opcode::AppendBlank:
        case Opcode::DeleteRecord:
        case Opcode::RecallRecord:
        case Opcode::PackTable:
        case Opcode::ZapTable:
        case Opcode::CloseTable:
            return true;
        default:
            return false;
    }
}

// RCB 07/15/2026: of the run-enders above, these carry an INDEX id as their
// leading u32, not a table id — their handlers resolve it through index_h_ /
// index_table_ (see the Seek / SkipUnique / SetScope / ClearScope cases). The
// ramp map prefetch_depth_ is keyed by TABLE, so dispatch() must translate
// index->table before resetting, or the reset silently misses: an index id is
// never a live table id, so reset_lookahead() would just erase an absent key
// and the real table's ramp would keep climbing across a seek — defeating the
// very "seek then read one, don't over-fetch" case the ramp exists for.
// SetOrder / SetOrderByName are absent on purpose: those DO lead with a table
// id (see their handlers).
bool break_key_is_index_id(Opcode op) {
    switch (op) {
        case Opcode::Seek:
        case Opcode::SeekLast:
        case Opcode::SkipUnique:
        case Opcode::SetScope:
        case Opcode::ClearScope:
            return true;
        default:
            return false;
    }
}

} // namespace

// M12.16 — re-position the engine cursor to match the ABI
// cursor after an index op that moves it (Seek / SeekLast).
// No-op when the table is a cursor or has no engine handle.
void Session::sync_engine_cursor(std::uint32_t id) {
    WTRACE("[wire] sync_engine_cursor id=%u enter\n", id);
    if (cursor_tbls_.count(id)) return;
    auto eit = tbls_.find(id);
    if (eit == tbls_.end() || !sess_conn_) return;
    auto* tbl = sess_conn_->lookup_table(eit->second);
    if (!tbl) return;
    ADSHANDLE h = 0;
    if (auto hit = tbls_h_.find(id); hit != tbls_h_.end()) {
        h = hit->second;
    } else { return; }
    // Appends / writes often go through the parallel ABI handle (where
    // CreateIndex / OpenIndex bound the bag). Re-read the header so the
    // engine Table sees the new record_count before GotoRecord.
    if (auto* drv = tbl->driver()) {
        drv->refresh_record_count_from_disk();
        drv->invalidate_read_cache();
    }
    UNSIGNED32 rn = 0;
    AdsGetRecordNum(h, 0, &rn);
    if (rn == 0) return;
    // A twin in Limbo (stale/empty order) reports a phantom recno
    // (record_count + 1). Dragging the engine cursor there poisons every
    // later pack_row_trailer fallback ("engine recno=16" on a 15-record
    // table). Skip the sync: the engine keeps its last known-good position.
    // NOTE: recno == record_count+1 with eof-only is a LEGITIMATE past-end
    // position (scope end) and must still sync — only Limbo (bof&&eof) is
    // the poison case.
    {
        UNSIGNED16 lb = 0, le = 0;
        AdsAtBOF(h, &lb); AdsAtEOF(h, &le);
        if (lb && le) {
            WTRACE("[wire] sync_engine_cursor id=%u twin in Limbo, skip\n", id);
            return;
        }
    }
    (void)tbl->goto_record(rn);
}

DispatchResult Session::dispatch(const Frame& f) {
    Frame reply;
    WTRACE("[wire] op=%u\n", (unsigned)f.opcode);
    if (wire_trace_on() && f.payload.size() >= 4) {
        std::uint32_t tid0 = read_u32_le(f.payload.data());
        if (ordered_tables_.count(tid0)) {
            if (auto hit0 = tbls_h_.find(tid0); hit0 != tbls_h_.end()) {
                UNSIGNED16 b0 = 9, e0 = 9;
                AdsAtBOF(hit0->second, &b0); AdsAtEOF(hit0->second, &e0);
                WTRACE("[wire] op=%u id=%u twin-in(bof=%u eof=%u)\n",
                       (unsigned)f.opcode, tid0, (unsigned)b0, (unsigned)e0);
            }
        }
    }
    // M12.22 — break the read-ahead run before the handler runs, in one place
    // instead of a reset call in each of ~20 handlers.
    //
    // RCB 07/15/2026: the leading u32 is the affected object's id, but for the
    // index-scoped ops (break_key_is_index_id) that is an INDEX id and has to be
    // resolved to its table first — prefetch_depth_ is keyed by table. Without
    // this, a Seek/SetScope/ClearScope on a table left its ramp climbing (an
    // index id is never a live table id, so the reset hit an absent key and did
    // nothing). If an index id can't be resolved, there is no table to reset.
    if (breaks_prefetch_run(f.opcode) && f.payload.size() >= 4) {
        std::uint32_t id = read_u32_le(f.payload.data());
        if (break_key_is_index_id(f.opcode)) {
            auto it = index_table_.find(id);
            id = (it != index_table_.end()) ? it->second : 0;
        }
        if (id != 0) reset_lookahead(id);
    }
    switch (f.opcode) {
        case Opcode::Hello: {
            reply.opcode = Opcode::HelloAck;
            // Real build version, not a hardcoded protocol string: this is
            // the only way a client (or a support engineer) can prove which
            // serverd binary is actually answering — "the fix didn't help"
            // reports keep turning out to be an old serverd still running.
            // Any pre-1.8.14 server answers the literal "openads/0.3.2".
#ifdef OPENADS_VERSION_STR
            std::string v = "openads/" OPENADS_VERSION_STR;
#else
            std::string v = "openads/unknown";
#endif
            reply.payload.assign(v.begin(), v.end());
            break;
        }
        case Opcode::Connect: {
            // M12.9 — Connect payload format:
            //   [u16 dlen][dir][u16 ulen][user][u16 plen][password]
            // All three lengths are required; user/password may be
            // empty when the server doesn't require auth.
            const auto& pl = f.payload;
            std::string dir, user, pw;
            std::size_t p = 0;
            auto readlen = [&](std::uint16_t& out)->bool {
                if (p + 2 > pl.size()) return false;
                out = static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(pl[p]) |
                    (static_cast<std::uint16_t>(pl[p+1]) << 8));
                p += 2; return true;
            };
            auto readstr = [&](std::string& out, std::uint16_t n)->bool {
                if (p + n > pl.size()) return false;
                out.assign(reinterpret_cast<const char*>(pl.data() + p), n);
                p += n; return true;
            };
            std::uint16_t dl=0, ul=0, pwl=0;
            if (!readlen(dl) || !readstr(dir, dl) ||
                !readlen(ul) || !readstr(user, ul) ||
                !readlen(pwl) || !readstr(pw, pwl)) {
                reply = err("Connect: bad payload");
                break;
            }
            // M12.21 option C — optional trailing [u32 LE caps].
            // Absent for pre-M12.21 clients (p == pl.size()).
            if (p + 4 <= pl.size()) {
                std::uint32_t caps =
                    static_cast<std::uint32_t>(pl[p]) |
                    (static_cast<std::uint32_t>(pl[p + 1]) <<  8) |
                    (static_cast<std::uint32_t>(pl[p + 2]) << 16) |
                    (static_cast<std::uint32_t>(pl[p + 3]) << 24);
                client_prefetch_ok_ =
                    (caps & openads::network::kCapPrefetchConsume) != 0;
                // RCB 07/15/2026: M12.25 — backward (PgUp) prefetch is a
                // separate opt-in; a client that only set kCapPrefetchConsume
                // cannot drain a backward block (see kCapPrefetchBackward).
                client_prefetch_back_ok_ =
                    (caps & openads::network::kCapPrefetchBackward) != 0;
                // M12.x — client sends [u16 mode] prefix on OpenTable.
                client_open_table_mode_ok_ =
                    (caps & openads::network::kCapOpenTableMode) != 0;
            }
            if (srv_->require_auth()) {
                std::lock_guard<std::mutex> clk(srv_->creds_mu_);
                auto cit = srv_->creds_.find(user);
                if (cit == srv_->creds_.end() || cit->second != pw) {
                    reply = err("Connect: authentication failed",
                                openads::AE_LOGIN_FAILED);
                    break;
                }
            }
            // Resolve client paths under one of the server's data roots and
            // reject traversal attempts (e.g. "../../outside"). --data (or
            // the ini `data=` key) may list several roots separated by ';'
            // so one server can serve DDs living under different
            // drives/shares; the client path just has to fall under any one
            // of them.
            //
            // Multi-port: when the client connected on an extra port, use
            // that port's data_dir instead of the global one.
            //
            // --legacy-paths: route through platform::resolve_client_path
            // instead — case-insensitive, drive-letter-ignoring prefix
            // strip ("C:/TEMP" maps onto root "c:\temp"), drive fold for
            // foreign absolute paths ("E:\CLIENT" -> "<root>/CLIENT"), and
            // an empty/drive-root dir maps to the first root itself, so a
            // legacy ERP connect string needs no server-side spelling.
            const std::string& effective_data_dir =
                default_data_dir_.empty() ? srv_->data_dir_ : default_data_dir_;
            std::string resolved = dir;
            if (!effective_data_dir.empty()) {
                auto roots = openads::platform::split_data_roots(effective_data_dir);
                std::optional<std::string> jail;
                if (srv_->legacy_paths()) {
                    jail = openads::platform::resolve_client_path(roots, dir);
                } else {
                    jail = openads::platform::resolve_under_any_root(roots, dir);
                }
                if (!jail) {
                    reply = err("Connect: path outside data directory",
                                openads::AE_ACCESS_DENIED);
                    break;
                }
                resolved = *jail;
            }
            auto co = openads::session::Connection::open(resolved);
            if (!co) {
                reply = err("Connect: connection open failed",
                            static_cast<UNSIGNED32>(co.error().code));
                break;
            }
            // RCB 06/30/2026: Remote Connect opens the engine Connection
            // directly, bypassing local AdsConnect60. Mirror DD login handling
            // here so the session username, permissions, and later lazy ABI
            // connection all represent the same authenticated DD user.
            if (co.value().has_dd()) {
                auto* dd = co.value().dd();
                // Logins-disabled: a stricter, all-connections-rejected gate
                // than LOG_IN_REQUIRED below — even a valid user/password
                // normally can't connect while this is set. Reads the STABLE
                // storage key "prop_16" (SP_MODIFYDATABASE numbering); the
                // SAP ABI id is ADS_DD_LOGINS_DISABLED (113), translated by
                // ace_exports' db_prop_storage_key.
                //
                // Admin bypass: without this, setting the flag would be a
                // one-way door — nobody, not even the admin trying to undo
                // it, could ever reconnect to flip it back off. See
                // mgmt::is_admin_bypass / mgmt::kAdminBypassUser, mirrored
                // in ace_exports.cpp's AdsConnect for local connections.
                std::string logins_disabled = dd->get_db_property("prop_16");
                bool disabled_raw_zero = (logins_disabled.size() >= 2 &&
                    static_cast<unsigned char>(logins_disabled[0]) == 0 &&
                    static_cast<unsigned char>(logins_disabled[1]) == 0);
                bool logins_are_disabled = (!logins_disabled.empty() &&
                    logins_disabled != "0" && logins_disabled != "False" &&
                    !disabled_raw_zero);
                if (logins_are_disabled &&
                    !openads::mgmt::is_admin_bypass(dd, user, pw)) {
                    reply = err("Connect: logins are disabled for this dictionary",
                                openads::AE_LOGIN_FAILED);
                    break;
                }

                std::string login_req = dd->get_db_property("prop_5");
                bool is_raw_zero = (login_req.size() >= 2 &&
                    static_cast<unsigned char>(login_req[0]) == 0 &&
                    static_cast<unsigned char>(login_req[1]) == 0);
                bool require_login = (!login_req.empty() &&
                    login_req != "0" && login_req != "False" && !is_raw_zero);
                if (require_login) {
                    if (user.empty()) {
                        reply = err("Connect: login required but no username supplied",
                                    openads::AE_LOGIN_FAILED);
                        break;
                    }
                    if (!dd->has_user(user)) {
                        reply = err("Connect: unknown user",
                                    openads::AE_LOGIN_FAILED);
                        break;
                    }
                    std::string stored = dd->get_user_property(user, "prop_1101");
                    if (stored != pw) {
                        reply = err("Connect: invalid password",
                                    openads::AE_LOGIN_FAILED);
                        break;
                    }
                }
                if (!user.empty()) {
                    co.value().set_username(user);
                    if (dd->has_any_acl()) dd->build_perm_cache(user);
                }
            }
            sess_conn_ = std::make_unique<openads::session::Connection>(
                std::move(co).value());
            // M12.34 — propagate server identity for replication loop prevention.
            sess_conn_->set_origin_id(srv_->server_id());
            // --legacy-paths: the session's table opens must resolve
            // client-absolute paths the same way the Connect jail above
            // accepted the connect dir.
            sess_conn_->set_legacy_paths(srv_->legacy_paths());
            sess_conn_->set_remote_server(true);
            session_user_ = user;
            session_password_ = pw;
            srv_->set_session_user(sid_, user, dir);
            reply.opcode = Opcode::ConnectAck;
            std::string ackmsg = "connected:" + dir;
            reply.payload.assign(ackmsg.begin(), ackmsg.end());
            break;
        }
        case Opcode::Disconnect: {
            cleanup();
            return { std::nullopt, true };
        }
        case Opcode::BeginTransaction: {
            if (!sess_conn_) {
                reply = err("BeginTransaction: not connected",
                            openads::AE_NO_CONNECTION);
                break;
            }
            auto r = sess_conn_->begin_tx();
            if (!r) {
                reply = err("BeginTransaction: " + r.error().message,
                            static_cast<UNSIGNED32>(r.error().code));
                break;
            }
            reply.opcode = Opcode::BeginTransactionAck;
            break;
        }
        case Opcode::CommitTransaction: {
            if (!sess_conn_) {
                reply = err("CommitTransaction: not connected",
                            openads::AE_NO_CONNECTION);
                break;
            }
            auto r = sess_conn_->commit_tx();
            if (!r) {
                reply = err("CommitTransaction: " + r.error().message,
                            static_cast<UNSIGNED32>(r.error().code));
                break;
            }
            reply.opcode = Opcode::CommitTransactionAck;
            break;
        }
        case Opcode::RollbackTransaction: {
            if (!sess_conn_) {
                reply = err("RollbackTransaction: not connected",
                            openads::AE_NO_CONNECTION);
                break;
            }
            auto r = sess_conn_->rollback_tx();
            if (!r) {
                reply = err("RollbackTransaction: " + r.error().message,
                            static_cast<UNSIGNED32>(r.error().code));
                break;
            }
            reply.opcode = Opcode::RollbackTransactionAck;
            break;
        }
        case Opcode::FindRecord: {
            if (!sess_conn_) {
                reply = err("FindRecord: not connected",
                            openads::AE_NO_CONNECTION);
                break;
            }
            if (f.payload.size() < 6) {
                reply = err("FindRecord: bad payload"); break;
            }
            {
                std::uint32_t id = read_u32_le(f.payload.data());
                std::uint16_t nident = static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(f.payload[4]) |
                    (static_cast<std::uint16_t>(f.payload[5]) << 8));
                auto it = tbls_.find(id);
                if (it == tbls_.end()) {
                    reply = err("FindRecord: bad table id"); break;
                }
                auto* tbl = sess_conn_->lookup_table(it->second);
                if (!tbl) {
                    reply = err("FindRecord: lookup failed"); break;
                }
                std::size_t off = 6;
                std::vector<std::pair<std::string, std::string>> ident;
                for (std::uint16_t i = 0; i < nident; ++i) {
                    if (off + 4 > f.payload.size()) {
                        reply = err("FindRecord: truncated identity"); break;
                    }
                    std::uint16_t nlen = static_cast<std::uint16_t>(
                        static_cast<std::uint16_t>(f.payload[off]) |
                        (static_cast<std::uint16_t>(f.payload[off + 1]) << 8));
                    off += 2;
                    if (off + nlen > f.payload.size()) {
                        reply = err("FindRecord: truncated name"); break;
                    }
                    std::string name(f.payload.begin() +
                                         static_cast<std::ptrdiff_t>(off),
                                     f.payload.begin() +
                                         static_cast<std::ptrdiff_t>(off + nlen));
                    off += nlen;
                    if (off + 2 > f.payload.size()) {
                        reply = err("FindRecord: truncated vlen"); break;
                    }
                    std::uint16_t vlen = static_cast<std::uint16_t>(
                        static_cast<std::uint16_t>(f.payload[off]) |
                        (static_cast<std::uint16_t>(f.payload[off + 1]) << 8));
                    off += 2;
                    if (off + vlen > f.payload.size()) {
                        reply = err("FindRecord: truncated value"); break;
                    }
                    std::string val(f.payload.begin() + static_cast<std::ptrdiff_t>(off),
                                    f.payload.begin() + static_cast<std::ptrdiff_t>(off + vlen));
                    off += vlen;
                    ident.emplace_back(std::move(name), std::move(val));
                }
                if (reply.opcode == Opcode::Error) break;
                // Sequential scan matching local find_by_identity logic.
                if (auto gr = tbl->goto_top(); !gr) {
                    reply.opcode = Opcode::FindRecordAck;
                    reply.payload.assign(4, 0);
                    break;
                }
                std::uint32_t found = 0;
                while (!tbl->eof()) {
                    bool match = true;
                    for (auto& [n, v] : ident) {
                        auto idx = tbl->field_index(n);
                        if (idx < 0) { match = false; break; }
                        auto val = tbl->read_field(static_cast<std::uint16_t>(idx));
                        if (!val) { match = false; break; }
                        if (val.value().as_string != v) { match = false; break; }
                    }
                    if (match) { found = tbl->recno(); break; }
                    if (auto sr = tbl->skip(1); !sr) break;
                }
                reply.opcode = Opcode::FindRecordAck;
                reply.payload.resize(4);
                reply.payload[0] = static_cast<std::uint8_t>( found        & 0xFFu);
                reply.payload[1] = static_cast<std::uint8_t>((found >>  8) & 0xFFu);
                reply.payload[2] = static_cast<std::uint8_t>((found >> 16) & 0xFFu);
                reply.payload[3] = static_cast<std::uint8_t>((found >> 24) & 0xFFu);
            }
            break;
        }
        case Opcode::OpenTable: {
            if (!sess_conn_) {
                reply = err("OpenTable: not connected",
                            openads::AE_NO_CONNECTION);
                break;
            }
            // Iterator-based ctor: payload.data() may be nullptr when empty,
            // and std::string(nullptr, 0) is UB.
            std::string rel;
            auto open_mode = openads::engine::OpenMode::Shared;
            if (client_open_table_mode_ok_ && f.payload.size() >= 2) {
                // M12.x extended payload: [u16 LE mode][table_name_bytes]
                std::uint16_t mode_u16 = static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(f.payload[0]) |
                    (static_cast<std::uint16_t>(f.payload[1]) << 8));
                open_mode = static_cast<openads::engine::OpenMode>(mode_u16);
                rel.assign(f.payload.begin() + 2, f.payload.end());
            } else {
                rel.assign(f.payload.begin(), f.payload.end());
            }
            // M12.33 — strip a full tcp:// URI prefix that legacy Delphi
            // TAdsTable components embed in the table name.  arc32 sends
            // "tcp://host:port/C:/data/dir\table.dbf" instead of bare
            // "table.dbf"; the server must strip the URI to resolve
            // relative to its data root.
            if (rel.size() > 8 &&
                (rel.rfind("tcp://", 0) == 0 || rel.rfind("TCP://", 0) == 0)) {
                auto last_sep = rel.find_last_of("/\\");
                if (last_sep != std::string::npos && last_sep > 6) {
                    std::string stripped = rel.substr(last_sep + 1);
                    if (!stripped.empty()) rel = std::move(stripped);
                }
            }
            auto th = sess_conn_->open_table(rel,
                openads::engine::TableType::Cdx,
                open_mode);
            if (!th) {
                std::fprintf(stderr, "[srv] OpenTable FAILED rel='%s' code=%d msg='%s'\n",
                             rel.c_str(), th.error().code,
                             th.error().message.c_str());
                reply = err("OpenTable: open failed",
                            static_cast<UNSIGNED32>(th.error().code));
                break;
            }
            std::uint32_t id = next_id_++;
            tbls_.emplace(id, th.value());
            tbl_open_paths_.emplace(id, rel);
            srv_->add_session_table(sid_, +1, rel);
            reply.opcode = Opcode::OpenTableAck;
            write_u32_le(id, reply.payload);
            // M-AOF.6 mirror: detect the production CDX/ADI on the server
            // filesystem and include it in the ack so the client can skip
            // the speculative AdsOpenIndex round-trip.  We only STAT the
            // file here — no ABI calls that could deadlock in the
            // embedded-server case (the client holds s.mu while waiting
            // for this ack).
            {
                auto* tbl = sess_conn_->lookup_table(th.value());
                if (tbl) {
                    std::filesystem::path tp(tbl->path());
                    std::string ext = tp.extension().string();
                    for (auto& c : ext)
                        c = static_cast<char>(std::tolower(
                                static_cast<unsigned char>(c)));
                    std::string bag_leaf;
                    if (ext == ".dbf")      bag_leaf = tp.stem().string() + ".cdx";
                    else if (ext == ".adt") bag_leaf = tp.stem().string() + ".adi";
                    if (!bag_leaf.empty()) {
                        std::error_code ec;
                        std::filesystem::path bagp = tp.parent_path() / bag_leaf;
                        if (!std::filesystem::exists(bagp, ec)) {
                            // Case-insensitive fallback.
                            std::string ci = openads::platform::
                                resolve_case_insensitive(bagp.string());
                            if (!ci.empty())
                                bagp = std::filesystem::path(ci);
                        }
                        if (std::filesystem::exists(bagp, ec)) {
                            // Send the bag path relative to the connection
                            // root so subdirectory tables round-trip the
                            // correct production index (basename-only made
                            // AdsOpenIndex miss when table_dir != data root).
                            std::string bag = bag_leaf;
                            std::filesystem::path base(sess_conn_->data_dir());
                            auto bag_rel = std::filesystem::relative(bagp, base, ec);
                            if (!ec && !bag_rel.empty() && bag_rel != ".") {
                                bag = bag_rel.generic_string();
                            }
                            // Append: [u16 bag_len][bag_bytes]
                            auto bn = static_cast<std::uint16_t>(bag.size());
                            reply.payload.push_back(
                                static_cast<std::uint8_t>( bn       & 0xFFu));
                            reply.payload.push_back(
                                static_cast<std::uint8_t>((bn >> 8) & 0xFFu));
                            reply.payload.insert(reply.payload.end(),
                                                 bag.begin(), bag.end());
                        }
                    }
                }
            }
            break;
        }
        case Opcode::CloseTable: {
            if (f.payload.size() < 4) { reply = err("CloseTable: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            auto cit = cursor_tbls_.find(id);
            if (cit != cursor_tbls_.end()) {
                // Drop the cached schema with the handle: AdsCloseTable frees
                // the ADSHANDLE, and a later AdsOpenTable can hand the same
                // value back for a different table.
                abi_schema_.erase(cit->second);
                (void)AdsCloseTable(cit->second);
                cursor_tbls_.erase(cit);
                srv_->add_session_table(sid_, -1);
                reply.opcode = Opcode::CloseTableAck;
                break;
            }
            auto it = tbls_.find(id);
            if (it != tbls_.end()) {
                sess_conn_->close_table(it->second);
                tbls_.erase(it);
                std::string tname;
                if (auto pit = tbl_open_paths_.find(id); pit != tbl_open_paths_.end())
                    tname = pit->second;
                srv_->add_session_table(sid_, -1, tname);
            }
            tbl_open_paths_.erase(id);
            if (auto hit = tbls_h_.find(id); hit != tbls_h_.end()) {
                abi_schema_.erase(hit->second);
                // Close the shadow ABI handle, not just the map entry.
                // Erasing alone leaks a full local open of the same
                // .dbf/.cdx/.fpt until the session disconnects, so the
                // server kept the files open after the client closed the
                // table — any app that erases/renames/reopens-exclusive
                // its work files right after closing them then blocks on
                // files "in use" for as long as the connection lives.
                (void)AdsCloseTable(hit->second);
                tbls_h_.erase(hit);
            }
            // Index ids resolved through the shadow handle died with it
            // (AdsCloseTable purges the table's index bindings); drop the
            // session's entries so a stale id can't be re-used.
            for (auto iit = index_table_.begin(); iit != index_table_.end(); ) {
                if (iit->second == id) {
                    index_h_.erase(iit->first);
                    iit = index_table_.erase(iit);
                } else {
                    ++iit;
                }
            }
            ordered_tables_.erase(id);
            reply.opcode = Opcode::CloseTableAck;
            break;
        }
        case Opcode::GotoTop: {
            if (f.payload.size() < 4) { reply = err("GotoTop: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                (void)AdsGotoTop(cit->second);
                reply.opcode = Opcode::GotoTopAck;
                pack_row_trailer(reply, id);
                break;
            }
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("GotoTop: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("GotoTop: lookup failed"); break; }
            // Ordered: navigate the ABI handle (it carries the order) and
            // mirror the position onto the engine cursor pack_row_trailer
            // reads from. Natural order: engine table directly.
            ADSHANDLE hord =
                ordered_tables_.count(id) ? ensure_abi_handle(id) : 0;
            WTRACE("[wire] GotoTop id=%u hord=%llu\n", id, (unsigned long long)hord);
            if (hord != 0) {
                (void)AdsGotoTop(hord);
            } else {
                (void)tbl->goto_top();
            }
            reply.opcode = Opcode::GotoTopAck;
            // RCB 07/14/2026: M12.24 — warm the first page. A GotoTop is about
            // as strong a "I am about to walk this table" signal as exists (a
            // browse painting its first screen, or a scan loop starting), yet
            // its ack used to carry the current row and nothing else, so the
            // very first Skip after it was always cold and always cost a
            // round-trip. Send the block with the row.
            //
            // Only GotoTop, deliberately. GotoBottom gets nothing: the block is
            // a FORWARD walk and there is nothing after the last record — a
            // warm GotoBottom needs BACKWARD lookahead, which is its own piece
            // of work (P5). GotoRecord/Seek get nothing either, because
            // relation navigation drives them once per parent row and would
            // drag a child block over the wire every time for nothing.
            //
            // Depth comes from next_lookahead() like any other block, so it
            // starts at the floor and honours AdsCacheRecords (an app that
            // turned read-ahead off must not get a block dumped on it here).
            // dispatch() has already reset the run for this table (GotoTop is
            // in breaks_prefetch_run), so this is a fresh run at the floor and
            // the following Skips ramp up from it.
            std::uint16_t gt_hint = kPrefetchDepthAuto;
            if (f.payload.size() >= 6) {
                gt_hint = static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(f.payload[4]) |
                    (static_cast<std::uint16_t>(f.payload[5]) << 8));
            }
            pack_row_trailer(reply, id, next_lookahead(id, gt_hint));
            // Sync AFTER packing — pack_row_trailer walks the ABI cursor
            // through the block and restores it, so the engine cursor has to be
            // anchored to where the ABI cursor finally lands (same reason as
            // the ordered Skip path).
            if (hord != 0) sync_engine_cursor(id);
            break;
        }
        case Opcode::Skip: {
            if (f.payload.size() < 8) { reply = err("Skip: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            std::int32_t step = static_cast<std::int32_t>(
                read_u32_le(f.payload.data() + 4));
            // M12.21 option C — a forward Skip from a prefetch-capable client
            // piggybacks a lookahead block; the client serves those rows
            // locally and folds the consumed count back into the next wire
            // step, so the server cursor never desyncs (the bug that shelved
            // option B). Non-capable clients and non-forward steps get no
            // lookahead, preserving the old behavior.
            //
            // M12.22 — the depth is no longer a flat 64. next_lookahead()
            // ramps it per consecutive forward Skip on this table and any
            // reposition/write resets the run (see breaks_prefetch_run), so a
            // one-off Skip pays for a handful of rows instead of a full block.
            //
            // M12.23 — ...unless the client named a depth via AdsCacheRecords,
            // which rides along as an OPTIONAL trailing [u16]. Absent (any
            // pre-M12.23 client) reads as kPrefetchDepthAuto = "you decide".
            std::uint16_t depth_hint = kPrefetchDepthAuto;
            if (f.payload.size() >= 10) {
                depth_hint = static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(f.payload[8]) |
                    (static_cast<std::uint16_t>(f.payload[9]) << 8));
            }
            // RCB 07/15/2026: M12.25 — direction from the step sign. Forward
            // (>= 1) needs kCapPrefetchConsume; backward (<= -1) additionally
            // needs kCapPrefetchBackward, because a forward-only client would
            // mis-drain a backward block. step == 0 (a settle) gets no block.
            const std::int8_t dir = (step >= 1) ? 1 : (step <= -1) ? -1 : 0;
            WTRACE("[wire] Skip id=%u step=%d\n", id, (int)step);
            const bool want_lookahead =
                (dir == 1 && client_prefetch_ok_) ||
                (dir == -1 && client_prefetch_ok_ && client_prefetch_back_ok_);
            const std::uint16_t lookahead =
                want_lookahead ? next_lookahead(id, depth_hint, dir) : 0;
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                (void)AdsSkip(cit->second, step);
                reply.opcode = Opcode::SkipAck;
                pack_row_trailer(reply, id, lookahead, dir);
                break;
            }
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("Skip: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("Skip: lookup failed"); break; }
            // Ordered: skip on the ABI handle, which is where the order and
            // any scope live. M12.22 — the lookahead block now rides along
            // here too: pack_row_trailer resolves an ordered table to the same
            // ABI handle, so it walks the INDEX, not natural record order.
            // That was the one thing blocking read-ahead on the browse that
            // actually matters (rddads skips on hOrdCurrent when an order is
            // set), and it needed no wire change.
            ADSHANDLE hord =
                ordered_tables_.count(id) ? ensure_abi_handle(id) : 0;
            if (hord != 0) {
                (void)AdsSkip(hord, step);
                { UNSIGNED16 _b = 9, _e = 9; AdsAtBOF(hord, &_b); AdsAtEOF(hord, &_e);
                  WTRACE("[wire] Skip twin hord bof=%u eof=%u\n", (unsigned)_b, (unsigned)_e); }
                reply.opcode = Opcode::SkipAck;
                pack_row_trailer(reply, id, lookahead, dir);
                // RCB 07/14/2026: sync AFTER packing, not before (this call
                // used to sit above the pack). pack_row_trailer walks the ABI
                // cursor through the lookahead block and then restores it, so
                // the engine cursor has to be re-anchored to where the ABI
                // cursor FINALLY lands. Syncing first would anchor it to a
                // position the pack is about to move away from.
                sync_engine_cursor(id);
                { UNSIGNED16 _b = 9, _e = 9; AdsAtBOF(hord, &_b); AdsAtEOF(hord, &_e);
                  WTRACE("[wire] Skip end id=%u twin bof=%u eof=%u\n", id, (unsigned)_b, (unsigned)_e); }
                break;
            }
            (void)tbl->skip(step);
            reply.opcode = Opcode::SkipAck;
            pack_row_trailer(reply, id, lookahead, dir);
            break;
        }
        case Opcode::GetField: {
            if (f.payload.size() < 5) { reply = err("GetField: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            std::string fname(reinterpret_cast<const char*>(
                                  f.payload.data() + 4),
                              f.payload.size() - 4);
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                UNSIGNED8  fbuf[64] = {0};
                UNSIGNED8  out [4096] = {0};
                UNSIGNED32 cap = sizeof(out);
                std::size_t n = std::min<std::size_t>(fname.size(),
                                                      sizeof(fbuf) - 1);
                std::memcpy(fbuf, fname.data(), n);
                fbuf[n] = 0;
                UNSIGNED32 rrc = AdsGetField(cit->second, fbuf, out, &cap, 0);
                if (rrc != 0) { reply = err("GetField: cursor read failed"); break; }
                reply.opcode = Opcode::GetFieldAck;
                reply.payload.assign(out, out + cap);
                break;
            }
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("GetField: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("GetField: lookup failed"); break; }
            std::int32_t fi = tbl->field_index(fname);
            if (fi < 0) { reply = err("GetField: column not found"); break; }
            auto v = tbl->read_field(static_cast<std::uint16_t>(fi));
            if (!v) {
                if (v.error().code ==
                        static_cast<std::int32_t>(openads::AE_NO_CURRENT_RECORD)) {
                    const auto& fd =
                        tbl->field_descriptor(static_cast<std::uint16_t>(fi));
                    std::string blank;
                    using FT = openads::drivers::DbfFieldType;
                    switch (fd.type) {
                        case FT::Logical:
                            blank = "F";
                            break;
                        default:
                            blank.assign(fd.length > 0 ? fd.length : 1, ' ');
                            break;
                    }
                    reply.opcode = Opcode::GetFieldAck;
                    reply.payload.assign(blank.begin(), blank.end());
                    break;
                }
                reply = err("GetField: read failed"); break;
            }
            reply.opcode = Opcode::GetFieldAck;
            auto& sval = v.value().as_string;
            reply.payload.assign(sval.begin(), sval.end());
            break;
        }
        case Opcode::GetRecordCount: {
            if (f.payload.size() < 4) { reply = err("GetRecordCount: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                UNSIGNED32 rc = 0;
                AdsGetRecordCount(cit->second, 0, &rc);
                reply.opcode = Opcode::GetRecordCountAck;
                write_u32_le(rc, reply.payload);
                break;
            }
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("GetRecordCount: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("GetRecordCount: lookup failed"); break; }
            // Refresh the on-disk record count so concurrent appends by
            // other connections are visible (multiuser coherence).
            tbl->refresh_record_count_from_disk();
            std::uint32_t rc = tbl->record_count();
            reply.opcode = Opcode::GetRecordCountAck;
            write_u32_le(rc, reply.payload);
            break;
        }
        case Opcode::GetKeyCount: {
            if (f.payload.size() < 4) { reply = err("GetKeyCount: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                UNSIGNED32 kc = 0;
                AdsGetRecordCount(cit->second, 0, &kc);
                reply.opcode = Opcode::GetKeyCountAck;
                write_u32_le(kc, reply.payload);
                break;
            }
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("GetKeyCount: bad table id"); break; }
            ADSHANDLE ht = ensure_abi_handle(id);
            if (ht != 0 && ordered_tables_.count(id)) {
                UNSIGNED32 kc = 0;
                UNSIGNED32 rrc = AdsGetKeyCount(ht, 0, &kc);
                if (rrc == 0) {
                    reply.opcode = Opcode::GetKeyCountAck;
                    write_u32_le(kc, reply.payload);
                    break;
                }
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("GetKeyCount: lookup failed"); break; }
            std::uint32_t kc = tbl->record_count();
            reply.opcode = Opcode::GetKeyCountAck;
            write_u32_le(kc, reply.payload);
            break;
        }
        // M12.29 — server-side key number: position of current record in
        // the active order's walk. Uses pos_of_recno_cached() on CDX → O(1),
        // eliminating the O(n) remote_measure_keyno client-side walk that
        // made TXBrowse:Refresh() take ~22 sec for 9500 records.
        case Opcode::GetKeyNum: {
            if (f.payload.size() < 4) { reply = err("GetKeyNum: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("GetKeyNum: bad table id"); break; }
            ADSHANDLE ht = ensure_abi_handle(id);
            if (ht != 0) {
                UNSIGNED32 kn = 0;
                UNSIGNED32 rrc = AdsGetKeyNum(ht, 0, &kn);
                if (rrc == 0) {
                    reply.opcode = Opcode::GetKeyNumAck;
                    write_u32_le(kn, reply.payload);
                    break;
                }
            }
            // Fallback: not positioned or no order → key number 0.
            reply.opcode = Opcode::GetKeyNumAck;
            write_u32_le(0u, reply.payload);
            break;
        }
        case Opcode::AtEOF: {
            if (f.payload.size() < 4) { reply = err("AtEOF: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                UNSIGNED16 v = 0;
                AdsAtEOF(cit->second, &v);
                WTRACE("[wire] AtEOF id=%u via twin -> %u\n", id, (unsigned)v);
                reply.opcode = Opcode::AtEOFAck;
                reply.payload.push_back(v != 0 ? 1 : 0);
                break;
            }
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("AtEOF: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("AtEOF: lookup failed"); break; }
            // See AtBOF: ordered tables must answer from the ABI twin.
            ADSHANDLE hord_eof =
                ordered_tables_.count(id) ? ensure_abi_handle(id) : 0;
            if (hord_eof != 0) {
                UNSIGNED16 v = 0, v2 = 0;
                AdsAtBOF(hord_eof, &v2);
                AdsAtEOF(hord_eof, &v);
                // Limbo rescue: if both bof and eof are true, reposition
                // the twin to break the deadlock (same logic as pack_row_trailer).
                if (v && v2) {
                    AdsGotoTop(hord_eof);
                    AdsAtBOF(hord_eof, &v2); AdsAtEOF(hord_eof, &v);
                    if (v && v2) AdsGotoBottom(hord_eof);
                    AdsAtEOF(hord_eof, &v);
                    WTRACE("[wire] AtEOF id=%u Limbo rescue -> %u\n", id, (unsigned)v);
                }
                WTRACE("[wire] AtEOF id=%u via ordered twin -> %u\n", id, (unsigned)v);
                reply.opcode = Opcode::AtEOFAck;
                reply.payload.push_back(v != 0 ? 1 : 0);
                break;
            }
            WTRACE("[wire] AtEOF id=%u engine eof=%d bof=%d\n", id, (int)tbl->eof(), (int)tbl->bof());
            reply.opcode = Opcode::AtEOFAck;
            reply.payload.push_back(tbl->eof() ? 1 : 0);
            break;
        }
        // M12.14 — DescribeTable: serialize the schema in one
        // round-trip so rddads' adsOpen field-iteration loop
        // doesn't generate 5 × num_fields hops.
        case Opcode::DescribeTable: {
            if (f.payload.size() < 4) {
                reply = err("DescribeTable: bad payload"); break;
            }
            std::uint32_t id = read_u32_le(f.payload.data());
            ADSHANDLE       cur_h  = 0;
            openads::engine::Table* tbl = nullptr;
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                cur_h = cit->second;
            } else {
                auto it = tbls_.find(id);
                if (it == tbls_.end() || !sess_conn_) {
                    reply = err("DescribeTable: bad table id"); break;
                }
                tbl = sess_conn_->lookup_table(it->second);
                if (!tbl) {
                    reply = err("DescribeTable: lookup failed"); break;
                }
            }
            reply.opcode = Opcode::DescribeTableAck;
            if (cur_h != 0) {
                UNSIGNED16 nf = 0;
                AdsGetNumFields(cur_h, &nf);
                reply.payload.push_back(static_cast<std::uint8_t>(nf & 0xFFu));
                reply.payload.push_back(static_cast<std::uint8_t>((nf >> 8) & 0xFFu));
                for (UNSIGNED16 i = 1; i <= nf; ++i) {
                    UNSIGNED8  nm[64] = {0};
                    UNSIGNED16 cap = sizeof(nm);
                    AdsGetFieldName(cur_h, i, nm, &cap);
                    std::vector<UNSIGNED8> nbuf(cap + 1, 0);
                    std::memcpy(nbuf.data(), nm, cap);
                    UNSIGNED16 ftype = 0;
                    UNSIGNED32 flen  = 0;
                    UNSIGNED16 fdec  = 0;
                    AdsGetFieldType    (cur_h, nbuf.data(), &ftype);
                    AdsGetFieldLength  (cur_h, nbuf.data(), &flen);
                    AdsGetFieldDecimals(cur_h, nbuf.data(), &fdec);
                    reply.payload.push_back(static_cast<std::uint8_t>(cap));
                    reply.payload.insert(reply.payload.end(),
                        nm, nm + cap);
                    reply.payload.push_back(static_cast<std::uint8_t>( ftype       & 0xFFu));
                    reply.payload.push_back(static_cast<std::uint8_t>((ftype >> 8) & 0xFFu));
                    write_u32_le(flen, reply.payload);
                    reply.payload.push_back(static_cast<std::uint8_t>( fdec       & 0xFFu));
                    reply.payload.push_back(static_cast<std::uint8_t>((fdec >> 8) & 0xFFu));
                }
            } else {
                // Mirror the ABI map_field_type() table so the wire
                // payload reports ADS_* type codes (4 = STRING, 2 =
                // NUMERIC, 11 = INTEGER, …) regardless of which
                // server-side branch we took.
                auto map_type = [](openads::drivers::DbfFieldType t) -> std::uint16_t {
                    using T = openads::drivers::DbfFieldType;
                    switch (t) {
                        case T::Character:    return ADS_STRING;
                        case T::Numeric:
                        case T::Float:        return ADS_NUMERIC;
                        case T::Logical:      return ADS_LOGICAL;
                        case T::Date:
                        case T::AdtDate:      return ADS_DATE;
                        case T::DateTime:
                        case T::AdtTimestamp: return ADS_TIMESTAMP;
                        case T::Memo:         return ADS_MEMO;
                        case T::Integer:
                        case T::ShortInt:
                        case T::AutoInc:      return ADS_INTEGER;
                        case T::Currency:
                        case T::AdtMoney:     return ADS_MONEY;
                        case T::Double:       return ADS_DOUBLE;
                        case T::Varchar:
                        case T::CiCharacter:  return ADS_STRING;
                        case T::Varbinary:
                        case T::Binary:       return ADS_RAW;
                        case T::Time:         return ADS_TIME;
                        case T::Unknown:
                        default:              return ADS_FIELD_TYPE_UNKNOWN;
                    }
                };
                auto nf = static_cast<std::uint16_t>(tbl->field_count());
                reply.payload.push_back(static_cast<std::uint8_t>(nf & 0xFFu));
                reply.payload.push_back(static_cast<std::uint8_t>((nf >> 8) & 0xFFu));
                for (std::uint16_t i = 0; i < nf; ++i) {
                    const auto& fd = tbl->field_descriptor(i);
                    std::uint8_t name_len =
                        static_cast<std::uint8_t>(fd.name.size() & 0xFFu);
                    std::uint16_t ftype = map_type(fd.type);
                    std::uint32_t flen = fd.length;
                    std::uint16_t fdec = fd.decimals;
                    reply.payload.push_back(name_len);
                    reply.payload.insert(reply.payload.end(),
                        fd.name.begin(),
                        fd.name.begin() + name_len);
                    reply.payload.push_back(static_cast<std::uint8_t>( ftype       & 0xFFu));
                    reply.payload.push_back(static_cast<std::uint8_t>((ftype >> 8) & 0xFFu));
                    write_u32_le(flen, reply.payload);
                    reply.payload.push_back(static_cast<std::uint8_t>( fdec       & 0xFFu));
                    reply.payload.push_back(static_cast<std::uint8_t>((fdec >> 8) & 0xFFu));
                }
            }
            break;
        }
        case Opcode::AtBOF: {
            if (f.payload.size() < 4) { reply = err("AtBOF: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            WTRACE("[wire] AtBOF id=%u\n", id);
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                UNSIGNED16 v = 0;
                AdsAtBOF(cit->second, &v);
                WTRACE("[wire] AtBOF id=%u via twin -> %u\n", id, (unsigned)v);
                reply.opcode = Opcode::AtBOFAck;
                reply.payload.push_back(v != 0 ? 1 : 0);
                break;
            }
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("AtBOF: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("AtBOF: lookup failed"); break; }
            // Ordered tables navigate on the ABI twin (it carries the order
            // and any scope); the engine cursor is only a mirrored recno and
            // can sit with bof+eof both set after a scope-end sync — answer
            // from the twin, like the GotoTop/Skip handlers do. Fixes the
            // BOF+EOF-both-true answer that made rddads' DBOI_POSITION
            // (OrdKeyGoto past the scoped key count) report Bof()=.T. and
            // xBrowse paint a phantom duplicate row (Tim Stone, 1-record
            // scope).
            ADSHANDLE hord_bof =
                ordered_tables_.count(id) ? ensure_abi_handle(id) : 0;
            if (hord_bof != 0) {
                UNSIGNED16 v = 0, v2 = 0;
                AdsAtBOF(hord_bof, &v);
                AdsAtEOF(hord_bof, &v2);
                // Limbo rescue: if both bof and eof are true, reposition
                // the twin to break the deadlock (same logic as pack_row_trailer).
                if (v && v2) {
                    AdsGotoTop(hord_bof);
                    AdsAtBOF(hord_bof, &v); AdsAtEOF(hord_bof, &v2);
                    if (v && v2) AdsGotoBottom(hord_bof);
                    AdsAtBOF(hord_bof, &v);
                    WTRACE("[wire] AtBOF id=%u Limbo rescue -> %u\n", id, (unsigned)v);
                }
                WTRACE("[wire] AtBOF id=%u via ordered twin -> %u\n", id, (unsigned)v);
                reply.opcode = Opcode::AtBOFAck;
                reply.payload.push_back(v != 0 ? 1 : 0);
                break;
            }
            WTRACE("[wire] AtBOF id=%u engine bof=%d eof=%d\n", id, (int)tbl->bof(), (int)tbl->eof());
            reply.opcode = Opcode::AtBOFAck;
            reply.payload.push_back(tbl->bof() ? 1 : 0);
            break;
        }
        case Opcode::GetRecordNum: {
            if (f.payload.size() < 4) { reply = err("GetRecordNum: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                UNSIGNED32 rn = 0;
                AdsGetRecordNum(cit->second, 0, &rn);
                reply.opcode = Opcode::GetRecordNumAck;
                write_u32_le(rn, reply.payload);
                break;
            }
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("GetRecordNum: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("GetRecordNum: lookup failed"); break; }
            reply.opcode = Opcode::GetRecordNumAck;
            write_u32_le(tbl->recno(), reply.payload);
            break;
        }
        case Opcode::IsRecordDeleted: {
            if (f.payload.size() < 4) { reply = err("IsRecordDeleted: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                UNSIGNED16 v = 0;
                AdsIsRecordDeleted(cit->second, &v);
                reply.opcode = Opcode::IsRecordDeletedAck;
                reply.payload.push_back(v != 0 ? 1 : 0);
                break;
            }
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("IsRecordDeleted: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("IsRecordDeleted: lookup failed"); break; }
            reply.opcode = Opcode::IsRecordDeletedAck;
            reply.payload.push_back(tbl->is_deleted() ? 1 : 0);
            break;
        }
        case Opcode::GotoBottom: {
            if (f.payload.size() < 4) { reply = err("GotoBottom: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                AdsGotoBottom(cit->second);
                reply.opcode = Opcode::GotoBottomAck;
                pack_row_trailer(reply, id);
                break;
            }
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("GotoBottom: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("GotoBottom: lookup failed"); break; }
            ADSHANDLE hord =
                ordered_tables_.count(id) ? ensure_abi_handle(id) : 0;
            if (hord != 0) {
                (void)AdsGotoBottom(hord);
                sync_engine_cursor(id);
            } else {
                auto rb = tbl->goto_bottom();
                if (!rb) {
                    reply = err("GotoBottom: " + rb.error().message);
                    break;
                }
            }
            reply.opcode = Opcode::GotoBottomAck;
            pack_row_trailer(reply, id);
            break;
        }
        // M12.15 — info / lock / maintenance / AOF.
        //
        // All these handlers share the same shape:
        //   payload[0..3]  = table id (client-side)
        //   reply payload  = answer (or empty for void)
        // For the local-table branch (sess_conn_-owned engine
        // Tables) the call lands on Table::* methods directly.
        // Cursor handles (from ExecuteSQL) route through the
        // matching ABI entry point, preserving the wire/ABI
        // symmetry.
        case Opcode::IsFound: {
            if (f.payload.size() < 4) { reply = err("IsFound: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                UNSIGNED16 v = 0;
                AdsIsFound(cit->second, &v);
                reply.opcode = Opcode::IsFoundAck;
                reply.payload.push_back(v != 0 ? 1 : 0);
                break;
            }
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("IsFound: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("IsFound: lookup failed"); break; }
            reply.opcode = Opcode::IsFoundAck;
            reply.payload.push_back(tbl->last_seek_found() ? 1 : 0);
            break;
        }
        case Opcode::RefreshRecord: {
            if (f.payload.size() < 4) { reply = err("RefreshRecord: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                AdsRefreshRecord(cit->second);
                reply.opcode = Opcode::RefreshRecordAck;
                break;
            }
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("RefreshRecord: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("RefreshRecord: lookup failed"); break; }
            // Force a re-load from disk by re-positioning to the
            // current recno.
            auto rb = tbl->goto_record(tbl->recno());
            if (!rb) { reply = err("RefreshRecord: " + rb.error().message); break; }
            reply.opcode = Opcode::RefreshRecordAck;
            break;
        }
        case Opcode::GetTableType: {
            if (f.payload.size() < 4) { reply = err("GetTableType: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                UNSIGNED16 v = 0;
                AdsGetTableType(cit->second, &v);
                reply.opcode = Opcode::GetTableTypeAck;
                reply.payload.push_back(static_cast<std::uint8_t>( v       & 0xFFu));
                reply.payload.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
                break;
            }
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("GetTableType: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("GetTableType: lookup failed"); break; }
            std::uint16_t v = ADS_CDX;
            std::filesystem::path p(tbl->path());
            std::string ext = p.extension().string();
            for (auto& c : ext) c = static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
            if      (ext == ".adt") v = ADS_ADT;
            reply.opcode = Opcode::GetTableTypeAck;
            reply.payload.push_back(static_cast<std::uint8_t>( v       & 0xFFu));
            reply.payload.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
            break;
        }
        case Opcode::GetRecordLength: {
            if (f.payload.size() < 4) { reply = err("GetRecordLength: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                UNSIGNED32 v = 0;
                AdsGetRecordLength(cit->second, &v);
                reply.opcode = Opcode::GetRecordLengthAck;
                write_u32_le(v, reply.payload);
                break;
            }
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("GetRecordLength: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("GetRecordLength: lookup failed"); break; }
            std::uint32_t rl = tbl->driver()
                ? tbl->driver()->record_length() : 0;
            reply.opcode = Opcode::GetRecordLengthAck;
            write_u32_le(rl, reply.payload);
            break;
        }
        case Opcode::GetNumIndexes: {
            if (f.payload.size() < 4) { reply = err("GetNumIndexes: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                UNSIGNED16 v = 0;
                AdsGetNumIndexes(cit->second, &v);
                reply.opcode = Opcode::GetNumIndexesAck;
                reply.payload.push_back(static_cast<std::uint8_t>( v       & 0xFFu));
                reply.payload.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
                break;
            }
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("GetNumIndexes: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("GetNumIndexes: lookup failed"); break; }
            // Indexes are opened on the ABI handle (incl. the production
            // CDX auto-opened there), not the engine table — count via the
            // ABI handle so OrdCount() / DBOI_ORDERCOUNT is non-zero
            // remotely. Falls back to the engine table's view if no ABI
            // handle has been promoted yet.
            std::uint16_t n = 0;
            if (ADSHANDLE ht = ensure_abi_handle(id); ht != 0) {
                (void)AdsGetNumIndexes(ht, &n);
            } else {
                n = static_cast<std::uint16_t>(tbl->all_indexes().size());
            }
            reply.opcode = Opcode::GetNumIndexesAck;
            reply.payload.push_back(static_cast<std::uint8_t>( n       & 0xFFu));
            reply.payload.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFFu));
            break;
        }
        case Opcode::GetLastAutoinc: {
            if (f.payload.size() < 4) { reply = err("GetLastAutoinc: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            std::uint32_t v = 0;
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                AdsGetLastAutoinc(cit->second, &v);
            }
            // Local-table path: not exposed at engine level;
            // return 0. Same as the local AdsGetLastAutoinc
            // fallback when the column isn't autoinc.
            reply.opcode = Opcode::GetLastAutoincAck;
            write_u32_le(v, reply.payload);
            break;
        }
        case Opcode::GetLastTableUpdate: {
            if (f.payload.size() < 4) { reply = err("GetLastTableUpdate: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            std::uint32_t packed = 0;
            if (cursor_tbls_.find(id) == cursor_tbls_.end()) {
                auto it = tbls_.find(id);
                if (it == tbls_.end() || !sess_conn_) {
                    reply = err("GetLastTableUpdate: bad table id"); break;
                }
                auto* tbl = sess_conn_->lookup_table(it->second);
                if (!tbl) { reply = err("GetLastTableUpdate: lookup failed"); break; }
                if (tbl->driver()) {
                    std::uint8_t b[3] = {0, 0, 0};
                    auto got = tbl->driver()->file().read_at(1, b, sizeof(b));
                    if (got && got.value() >= sizeof(b)) {
                        packed = (static_cast<std::uint32_t>(1900 + b[0]) << 16) |
                                 (static_cast<std::uint32_t>(b[1]) << 8) |
                                  static_cast<std::uint32_t>(b[2]);
                    }
                }
            }
            reply.opcode = Opcode::GetLastTableUpdateAck;
            write_u32_le(packed, reply.payload);
            break;
        }
        // Record locks route through the parallel ABI handle when one
        // exists (M12.16 dual-handle) so they land on the same Table
        // instance that appends/writes go through; otherwise they use
        // the engine table from tbls_ with a non-blocking retry loop.
        case Opcode::LockRecord:
        case Opcode::UnlockRecord: {
            if (f.payload.size() < 8) { reply = err("Lock: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            std::uint32_t rn = read_u32_le(f.payload.data() + 4);
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("Lock: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("Lock: lookup failed"); break; }
            // ACE convention: recno 0 = the current record. The engine
            // cursor is the authoritative position (write opcodes sync the
            // ABI twin to it before guarding), so translate here or the
            // lock lands on nonexistent record 0 and the write guard
            // correctly rejects the later write with 5035.
            if (rn == 0) rn = tbl->recno();
            openads::mgmt::set_current_lock_owner(
                session_user_.empty() ? "(anonymous)" : session_user_,
                srv_->conn_no_for_session(sid_));
            // M12.16 dual-handle: appends go through the parallel ABI
            // handle (see AppendBlank), and AdsAppendRecord auto-locks the
            // new record on THAT Table instance. Routing the client's
            // RLock() to the engine Table made every lock after APPEND
            // BLANK burn the full lock-retry budget (~1 s per record —
            // Pritpal's "9 records in 10 seconds", 31/07/2026) and the
            // matching UNLOCK never released the ABI-side auto-lock,
            // leaking it until session cleanup and stalling other
            // stations. Route lock/unlock through the same ABI handle
            // whenever it exists.
            if (auto hit = tbls_h_.find(id); hit != tbls_h_.end()) {
                UNSIGNED32 rrc = (f.opcode == Opcode::LockRecord)
                    ? AdsLockRecord(hit->second, rn)
                    : AdsUnlockRecord(hit->second, rn);
                // Also release any stale engine-side lock taken before
                // the ABI handle existed (best-effort; not holding the
                // lock is fine).
                if (f.opcode == Opcode::UnlockRecord)
                    (void)tbl->unlock_record(rn);
                if (rrc != 0) { reply = err("Lock: failed", rrc); break; }
            } else if (f.opcode == Opcode::LockRecord) {
                // Use non-blocking try + retry loop (same semantics as
                // the ABI lock_with_retry).  Blocking lock_record_excl
                // would freeze the entire server until the OS grants the
                // lock — unacceptable for concurrent clients.
                auto policy = openads::abi::lock_retry_policy();
                bool got = false;
                for (std::uint16_t i = 0; ; ++i) {
                    auto r = tbl->try_lock_record_excl(rn);
                    if (r) { got = true; break; }
                    if (i >= policy.retry_count) break;
                    if (policy.cycle_ms > 0) {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(policy.cycle_ms));
                    }
                }
                if (!got) {
                    reply = err("LockRecord: failed",
                        openads::AE_LOCKED);
                    break;
                }
            } else {
                auto r = tbl->unlock_record(rn);
                if (!r) { reply = err("UnlockRecord: failed",
                    static_cast<UNSIGNED32>(r.error().code)); break; }
            }
            reply.opcode = (f.opcode == Opcode::LockRecord)
                ? Opcode::LockRecordAck
                : Opcode::UnlockRecordAck;
            break;
        }
        // M12.36 — lock introspection. Same dual-handle routing as
        // LockRecord above: when the parallel ABI handle exists it owns
        // the locks (appends auto-lock there), so query it through the
        // local ACE calls; otherwise walk the engine Table's held list.
        case Opcode::IsRecordLocked: {
            if (f.payload.size() < 8) { reply = err("IsRecordLocked: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            std::uint32_t rn = read_u32_le(f.payload.data() + 4);
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("IsRecordLocked: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("IsRecordLocked: lookup failed"); break; }
            // ACE convention: recno 0 = the current record.
            if (rn == 0) rn = tbl->recno();
            std::uint16_t locked = 0;
            if (auto hit = tbls_h_.find(id); hit != tbls_h_.end()) {
                UNSIGNED16 b = 0;
                if (AdsIsRecordLocked(hit->second, rn, &b) != 0) {
                    reply = err("IsRecordLocked: failed"); break;
                }
                locked = b;
            } else {
                for (std::uint32_t held : tbl->held_record_locks()) {
                    if (held == rn) { locked = 1; break; }
                }
            }
            reply.opcode = Opcode::IsRecordLockedAck;
            write_u16_le(locked, reply.payload);
            break;
        }
        case Opcode::GetAllLocks: {
            if (f.payload.size() < 4) { reply = err("GetAllLocks: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("GetAllLocks: bad table id"); break;
            }
            std::vector<std::uint32_t> recs;
            if (auto hit = tbls_h_.find(id); hit != tbls_h_.end()) {
                UNSIGNED16 count = 0;
                if (AdsGetAllLocks(hit->second, nullptr, &count) != 0) {
                    reply = err("GetAllLocks: failed"); break;
                }
                recs.resize(count);
                if (count > 0 &&
                    AdsGetAllLocks(hit->second, recs.data(), &count) != 0) {
                    reply = err("GetAllLocks: failed"); break;
                }
                recs.resize(count);
            } else {
                auto* tbl = sess_conn_->lookup_table(it->second);
                if (!tbl) { reply = err("GetAllLocks: lookup failed"); break; }
                recs = tbl->held_record_locks();
            }
            reply.opcode = Opcode::GetAllLocksAck;
            write_u16_le(static_cast<std::uint16_t>(recs.size()),
                         reply.payload);
            for (std::uint32_t rn : recs) write_u32_le(rn, reply.payload);
            break;
        }
        case Opcode::LockTable:
        case Opcode::UnlockTable: {
            if (f.payload.size() < 4) { reply = err("Lock: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("LockTable: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("LockTable: lookup failed"); break; }
            openads::mgmt::set_current_lock_owner(
                session_user_.empty() ? "(anonymous)" : session_user_,
                srv_->conn_no_for_session(sid_));
            if (f.opcode == Opcode::LockTable) {
                auto policy = openads::abi::lock_retry_policy();
                bool got = false;
                for (std::uint16_t i = 0; ; ++i) {
                    auto r = tbl->try_lock_table_excl();
                    if (r) { got = true; break; }
                    if (i >= policy.retry_count) break;
                    if (policy.cycle_ms > 0) {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(policy.cycle_ms));
                    }
                }
                if (!got) {
                    reply = err("LockTable: failed",
                        openads::AE_LOCKED);
                    break;
                }
            } else {
                // M12.16 dual-handle: route through the ABI shadow handle
                // when it exists (same as LockRecord/UnlockRecord) so the
                // ABI-side Table's LockMgr releases its own locks.
                if (auto hit = tbls_h_.find(id); hit != tbls_h_.end()) {
                    UNSIGNED32 rrc = AdsUnlockTable(hit->second);
                    // Also release engine-side locks (best-effort cleanup).
                    (void)tbl->unlock_table();
                    if (rrc != 0) {
                        reply = err("UnlockTable: failed", rrc); break;
                    }
                } else {
                    auto r = tbl->unlock_table();
                    if (!r) { reply = err("UnlockTable: failed",
                        static_cast<UNSIGNED32>(r.error().code)); break; }
                }
            }
            reply.opcode = (f.opcode == Opcode::LockTable)
                ? Opcode::LockTableAck
                : Opcode::UnlockTableAck;
            break;
        }
        case Opcode::PackTable:
        case Opcode::ZapTable:
        case Opcode::FlushFileBuffers:
        case Opcode::CloseAllIndexes: {
            if (f.payload.size() < 4) { reply = err("Maintenance: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            Opcode ack_op =
                  (f.opcode == Opcode::PackTable)        ? Opcode::PackTableAck
                : (f.opcode == Opcode::ZapTable)         ? Opcode::ZapTableAck
                : (f.opcode == Opcode::FlushFileBuffers) ? Opcode::FlushFileBuffersAck
                :                                          Opcode::CloseAllIndexesAck;
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                if      (f.opcode == Opcode::PackTable)        AdsPackTable(cit->second);
                else if (f.opcode == Opcode::ZapTable)         AdsZapTable(cit->second);
                else if (f.opcode == Opcode::FlushFileBuffers) AdsFlushFileBuffers(cit->second);
                else                                            AdsCloseAllIndexes(cit->second);
                reply.opcode = ack_op;
                break;
            }
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("Maintenance: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("Maintenance: lookup failed"); break; }
            util::Result<void> rb = util::Result<void>{};
            if      (f.opcode == Opcode::PackTable)        rb = tbl->pack();
            else if (f.opcode == Opcode::ZapTable)         rb = tbl->zap();
            else if (f.opcode == Opcode::FlushFileBuffers) rb = tbl->flush();
            else {
                // CloseAllIndexes: drop both active order +
                // every parked extra view in lockstep.
                tbl->clear_order();
                tbl->clear_extra_index_views();
            }
            if (!rb) { reply = err("Maintenance: " + rb.error().message); break; }
            // Engine-side pack/zap rewrites recnos (pack) or empties the file
            // (zap); bags bound on the ABI twin now point at phantom recnos.
            // Rebuild them so the next ordered nav doesn't walk ghosts.
            // AdsGotoTop first: it refreshes the twin's cached record_count
            // from disk, which the reindex scan relies on.
            if (f.opcode == Opcode::PackTable || f.opcode == Opcode::ZapTable) {
                if (auto hit = tbls_h_.find(id); hit != tbls_h_.end()) {
                    (void)AdsGotoTop(hit->second);
                    (void)AdsReindex(hit->second);
                }
            }
            reply.opcode = ack_op;
            break;
        }
        case Opcode::SetAOF: {
            if (f.payload.size() < 4) { reply = err("SetAOF: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            std::string cond(reinterpret_cast<const char*>(
                                 f.payload.data() + 4),
                             f.payload.size() - 4);
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                std::vector<UNSIGNED8> b(cond.size() + 1);
                std::memcpy(b.data(), cond.data(), cond.size());
                UNSIGNED32 rrc = AdsSetAOF(cit->second, b.data(), 0);
                if (rrc != 0) { reply = err("SetAOF: parse failed", rrc); break; }
                reply.opcode = Opcode::SetAOFAck;
                break;
            }
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("SetAOF: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("SetAOF: lookup failed"); break; }
            auto ast = openads::engine::aof::parse(cond);
            if (!ast) {
                // Expression outside the optimisable AOF subset
                // (e.g. Empty(NAME)) — not an error. Drop any
                // prior AOF and ack; the client RDD filters.
                tbl->clear_filter();
                reply.opcode = Opcode::SetAOFAck;
                break;
            }
            auto rep = openads::engine::aof::evaluate_optimised(*ast.value(), *tbl);
            if (!rep) { reply = err("SetAOF: " + rep.error().message); break; }
            tbl->install_aof_bitmap(std::move(rep.value().bm));
            tbl->set_aof_expr(cond);
            int lvl = ADS_OPTIMIZED_NONE;
            switch (rep.value().level) {
                case openads::engine::aof::OptLevel::None: lvl = ADS_OPTIMIZED_NONE; break;
                case openads::engine::aof::OptLevel::Part: lvl = ADS_OPTIMIZED_PART; break;
                case openads::engine::aof::OptLevel::Full: lvl = ADS_OPTIMIZED_FULL; break;
            }
            tbl->set_aof_opt_level(lvl);
            reply.opcode = Opcode::SetAOFAck;
            break;
        }
        case Opcode::ClearAOFRemote: {
            if (f.payload.size() < 4) { reply = err("ClearAOF: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                AdsClearAOF(cit->second);
                reply.opcode = Opcode::ClearAOFRemoteAck;
                break;
            }
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("ClearAOF: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("ClearAOF: lookup failed"); break; }
            tbl->clear_filter();
            reply.opcode = Opcode::ClearAOFRemoteAck;
            break;
        }
        case Opcode::GetAOFOptLevel: {
            if (f.payload.size() < 4) { reply = err("GetAOFOptLevel: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            std::uint16_t v = ADS_OPTIMIZED_NONE;
            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                UNSIGNED16 lvl = 0; UNSIGNED16 buflen = 0;
                AdsGetAOFOptLevel(cit->second, &lvl, nullptr, &buflen);
                v = lvl;
            } else if (auto it = tbls_.find(id); it != tbls_.end() && sess_conn_) {
                if (auto* tbl = sess_conn_->lookup_table(it->second)) {
                    if (tbl->aof_active()) {
                        v = static_cast<std::uint16_t>(tbl->aof_opt_level());
                    }
                }
            }
            reply.opcode = Opcode::GetAOFOptLevelAck;
            reply.payload.push_back(static_cast<std::uint8_t>( v       & 0xFFu));
            reply.payload.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
            break;
        }
        // M12.16 — remote index handle subsystem. All ops route
        // through the ABI handle on tbls_h_ (lazy-promoted via
        // ensure_abi_handle) so AdsOpenIndex / AdsSetOrder /
        // AdsSeek work end-to-end over the wire.
        case Opcode::OpenIndex: {
            if (f.payload.size() < 4) { reply = err("OpenIndex: bad payload"); break; }
            std::uint32_t tid = read_u32_le(f.payload.data());
            std::string path(reinterpret_cast<const char*>(
                                 f.payload.data() + 4),
                             f.payload.size() - 4);
            ADSHANDLE ht = ensure_abi_handle(tid);
            if (ht == 0) { reply = err("OpenIndex: bad table id"); break; }
            std::vector<UNSIGNED8> pb(path.size() + 1);
            std::memcpy(pb.data(), path.data(), path.size());
            ADSHANDLE arr[64] = {0};
            UNSIGNED16 alen = 64;
            UNSIGNED32 rrc = AdsOpenIndex(ht, pb.data(), arr, &alen);
            if (rrc != 0) { reply = err("OpenIndex: " + path, rrc); break; }
            reply.opcode = Opcode::OpenIndexAck;
            reply.payload.push_back(static_cast<std::uint8_t>( alen       & 0xFFu));
            reply.payload.push_back(static_cast<std::uint8_t>((alen >> 8) & 0xFFu));
            for (std::uint16_t i = 0; i < alen; ++i) {
                std::uint32_t iid = next_id_++;
                index_h_[iid] = arr[i];
                index_table_[iid] = tid;
                write_u32_le(iid, reply.payload);
                std::string tag;
                UNSIGNED8 tbuf[256] = {0};
                UNSIGNED16 tlen = static_cast<UNSIGNED16>(sizeof(tbuf) - 1);
                if (AdsGetIndexName(arr[i], tbuf, &tlen) == 0) {
                    tag.assign(reinterpret_cast<char*>(tbuf), tlen);
                    while (!tag.empty() && tag.back() == ' ') tag.pop_back();
                }
                auto tn = static_cast<std::uint16_t>(tag.size());
                reply.payload.push_back(static_cast<std::uint8_t>( tn       & 0xFFu));
                reply.payload.push_back(static_cast<std::uint8_t>((tn >> 8) & 0xFFu));
                reply.payload.insert(reply.payload.end(),
                                     tag.begin(), tag.end());
            }
            // Append the production bag path so the client can serve
            // AdsGetIndexFilename / OrdBagName without a wire round-trip.
            // All tags in this OpenIndex response come from the same bag
            // (the first tag handle is enough to query the bag path).
            {
                UNSIGNED8 bbuf[512] = {0};
                UNSIGNED16 blen = static_cast<UNSIGNED16>(sizeof(bbuf) - 1);
                std::string bag;
                if (alen > 0 &&
                    AdsGetIndexFilename(arr[0], 0, bbuf, &blen) == 0 &&
                    blen > 0) {
                    bag.assign(reinterpret_cast<char*>(bbuf), blen);
                }
                auto bn = static_cast<std::uint16_t>(bag.size());
                reply.payload.push_back(static_cast<std::uint8_t>( bn       & 0xFFu));
                reply.payload.push_back(static_cast<std::uint8_t>((bn >> 8) & 0xFFu));
                reply.payload.insert(reply.payload.end(),
                                     bag.begin(), bag.end());
            }
            // Extended per-tag metadata (expression, unique, descending),
            // one triple per tag in the same order as the tag loop above.
            // Lets remote AdsGetIndexExpr / AdsIsIndexUnique /
            // AdsIsIndexDescending answer from the cached RemoteIndex
            // instead of a lookup that only ever resolves local handles.
            for (std::uint16_t i = 0; i < alen; ++i) {
                UNSIGNED8 ebuf[1024] = {0};
                UNSIGNED16 elen = static_cast<UNSIGNED16>(sizeof(ebuf) - 1);
                std::string expr;
                if (AdsGetIndexExpr(arr[i], ebuf, &elen) == 0 && elen > 0) {
                    expr.assign(reinterpret_cast<char*>(ebuf), elen);
                }
                auto en = static_cast<std::uint16_t>(expr.size());
                reply.payload.push_back(static_cast<std::uint8_t>( en       & 0xFFu));
                reply.payload.push_back(static_cast<std::uint8_t>((en >> 8) & 0xFFu));
                reply.payload.insert(reply.payload.end(),
                                     expr.begin(), expr.end());

                UNSIGNED16 uniq = 0, desc = 0;
                AdsIsIndexUnique(arr[i], &uniq);
                AdsIsIndexDescending(arr[i], &desc);
                reply.payload.push_back(uniq != 0 ? 1 : 0);
                reply.payload.push_back(desc != 0 ? 1 : 0);
            }
            break;
        }
        case Opcode::CloseIndex: {
            if (f.payload.size() < 4) { reply = err("CloseIndex: bad payload"); break; }
            std::uint32_t iid = read_u32_le(f.payload.data());
            auto iit = index_h_.find(iid);
            if (iit != index_h_.end()) {
                AdsCloseIndex(iit->second);
                index_h_.erase(iit);
            }
            index_table_.erase(iid);
            reply.opcode = Opcode::CloseIndexAck;
            break;
        }
        case Opcode::SetOrder: {
            if (f.payload.size() < 8) { reply = err("SetOrder: bad payload"); break; }
            std::uint32_t tid = read_u32_le(f.payload.data());
            std::uint32_t iid = read_u32_le(f.payload.data() + 4);
            ADSHANDLE ht = ensure_abi_handle(tid);
            if (ht == 0) { reply = err("SetOrder: bad table id"); break; }
            auto iit = index_h_.find(iid);
            if (iit == index_h_.end()) { reply = err("SetOrder: bad index id"); break; }
            UNSIGNED32 rrc = AdsSetIndexOrderByHandle(ht, iit->second);
            if (rrc != 0) { reply = err("SetOrder", rrc); break; }
            ordered_tables_.insert(tid);
            sync_engine_cursor(tid);
            reply.opcode = Opcode::SetOrderAck;
            break;
        }
        case Opcode::SetOrderByName: {
            if (f.payload.size() < 4) { reply = err("SetOrderByName: bad payload"); break; }
            std::uint32_t tid = read_u32_le(f.payload.data());
            std::string tag(reinterpret_cast<const char*>(
                                f.payload.data() + 4),
                            f.payload.size() - 4);
            ADSHANDLE ht = ensure_abi_handle(tid);
            if (ht == 0) { reply = err("SetOrderByName: bad table id"); break; }
            std::vector<UNSIGNED8> tb(tag.size() + 1);
            std::memcpy(tb.data(), tag.data(), tag.size());
            UNSIGNED32 rrc = AdsSetIndexOrder(ht,
                tag.empty() ? nullptr : tb.data());
            if (rrc != 0) { reply = err("SetOrderByName: " + tag, rrc); break; }
            if (tag.empty()) ordered_tables_.erase(tid);
            else             ordered_tables_.insert(tid);
            sync_engine_cursor(tid);
            reply.opcode = Opcode::SetOrderByNameAck;
            break;
        }
        case Opcode::Seek:
        case Opcode::SeekLast: {
            // payload: u32 index_id, u8 soft, key bytes.
            if (f.payload.size() < 5) { reply = err("Seek: bad payload"); break; }
            std::uint32_t iid  = read_u32_le(f.payload.data());
            std::uint8_t  soft = f.payload[4];
            std::string key(reinterpret_cast<const char*>(
                                f.payload.data() + 5),
                            f.payload.size() - 5);
            auto iit = index_h_.find(iid);
            if (iit == index_h_.end()) { reply = err("Seek: bad index id"); break; }
            std::vector<UNSIGNED8> kb(key.size() + 1);
            std::memcpy(kb.data(), key.data(), key.size());
            UNSIGNED16 found = 0;
            UNSIGNED32 rrc = (f.opcode == Opcode::SeekLast)
                ? AdsSeekLast(iit->second, kb.data(),
                              static_cast<UNSIGNED16>(key.size()),
                              ADS_STRINGKEY,
                              &found)
                : AdsSeek    (iit->second, kb.data(),
                              static_cast<UNSIGNED16>(key.size()),
                              ADS_STRINGKEY,
                              static_cast<UNSIGNED16>(soft),
                              &found);
            if (rrc != 0) { reply = err("Seek", rrc); break; }
            // Read recno via the parent table's ABI handle; that
            // also lets sync_engine_cursor reflect the new
            // position on the engine cursor we keep alive
            // alongside.
            UNSIGNED32 rn = 0;
            std::uint32_t parent_tid = 0;
            if (auto tit = index_table_.find(iid); tit != index_table_.end()) {
                parent_tid = tit->second;
                if (ADSHANDLE ht = ensure_abi_handle(parent_tid); ht != 0) {
                    AdsGetRecordNum(ht, 0, &rn);
                }
            }
            if (parent_tid != 0) sync_engine_cursor(parent_tid);
            reply.opcode = (f.opcode == Opcode::SeekLast)
                ? Opcode::SeekLastAck : Opcode::SeekAck;
            reply.payload.push_back(static_cast<std::uint8_t>(found != 0 ? 1 : 0));
            write_u32_le(rn, reply.payload);
            // RCB 07/14/2026: M12.24 — append the row the seek landed on.
            //
            // The ack used to be just [u8 found][u32 recno], which meant the
            // client knew WHERE it was but not WHAT was there: row_valid stayed
            // false, so the first AdsGetField after a seek paid a whole extra
            // FetchCurrentRow round-trip. Seek-then-read is the single most
            // common thing a business app does, and it was costing 2 RTTs.
            // Worse, the relation code (seek_remote_child_relation) papered
            // over it by firing a GotoRecord straight after every seek purely
            // to pull the row down — so a parent browse with a child relation
            // paid 2 RTTs per parent ROW. The trailer kills both.
            //
            // Deliberately NO lookahead block here (depth 0). A relation
            // re-seeks the child on every single parent row, so a block would
            // drag N child rows over the wire per parent row and almost never
            // be read. SAP draws the same line: read-ahead triggers on "a skip
            // operation after ... any other movement operation", i.e. the skip
            // earns the block, not the seek.
            //
            // Wire-safe both ways, no capability bit: an old client requires
            // size() >= 5 and ignores trailing bytes; a new client against an
            // old server sees a 5-byte ack, parses no trailer, and falls back
            // to the FetchCurrentRow path exactly as before.
            if (parent_tid != 0) pack_row_trailer(reply, parent_tid, 0);
            break;
        }
        // CreateIndex / SkipUnique / SetScope / ClearScope —
        // remote bridges for the remaining index ops. CreateIndex
        // takes the full AdsCreateIndex61 input and returns a
        // single index id (multi-tag CDX additions are supported
        // via repeated calls). SkipUnique walks distinct keys via
        // the active order; SetScope / ClearScope manage top /
        // bottom range bounds on an existing hIndex.
        case Opcode::CreateIndex: {
            if (f.payload.size() < 4 + 4 + 2) {
                reply = err("CreateIndex: bad payload"); break;
            }
            std::size_t pos = 0;
            std::uint32_t tid     = read_u32_le(f.payload.data() + pos); pos += 4;
            std::uint32_t options = read_u32_le(f.payload.data() + pos); pos += 4;
            std::uint16_t pgsize  = read_u16_le(f.payload.data() + pos); pos += 2;
            auto pop_str = [&](std::string& out) -> bool {
                if (pos + 2 > f.payload.size()) return false;
                std::uint16_t n = read_u16_le(f.payload.data() + pos);
                pos += 2;
                if (pos + n > f.payload.size()) return false;
                out.assign(reinterpret_cast<const char*>(
                               f.payload.data() + pos), n);
                pos += n;
                return true;
            };
            std::string path, tag, expr, cond, key_filter;
            if (!pop_str(path) || !pop_str(tag) || !pop_str(expr) ||
                !pop_str(cond) || !pop_str(key_filter)) {
                reply = err("CreateIndex: short payload"); break;
            }
            // Honor a directory-qualified bag name ("folder/Indexes/x.Z01")
            // server-side; a bare filename keeps the table-folder fallback.
            path = resolve_index_bag_path(path);
            ADSHANDLE ht = ensure_abi_handle(tid);
            if (ht == 0) { reply = err("CreateIndex: bad table id"); break; }
            std::vector<UNSIGNED8> pb (path .size() + 1);  std::memcpy(pb .data(), path .data(), path .size());
            std::vector<UNSIGNED8> tb (tag  .size() + 1);  std::memcpy(tb .data(), tag  .data(), tag  .size());
            std::vector<UNSIGNED8> eb (expr .size() + 1);  std::memcpy(eb .data(), expr .data(), expr .size());
            std::vector<UNSIGNED8> cb (cond .size() + 1);  std::memcpy(cb .data(), cond .data(), cond .size());
            std::vector<UNSIGNED8> kfb(key_filter.size() + 1);
            std::memcpy(kfb.data(), key_filter.data(), key_filter.size());
            ADSHANDLE hidx = 0;
            UNSIGNED32 rrc = AdsCreateIndex61(
                ht, pb.data(), tb.data(), eb.data(),
                cond.empty() ? nullptr : cb.data(),
                key_filter.empty() ? nullptr : kfb.data(),
                options, pgsize, &hidx);
            if (rrc != 0) { reply = err("CreateIndex", rrc); break; }
            std::uint32_t iid = next_id_++;
            index_h_[iid]     = hidx;
            index_table_[iid] = tid;
            reply.opcode = Opcode::CreateIndexAck;
            write_u32_le(iid, reply.payload);
            break;
        }
        // Remote AdsCreateTable: write the free table under the session
        // data directory via the lazy ABI connection, then close it so
        // the client can re-open through OpenTable (prod bag auto-open).
        case Opcode::CreateTable: {
            if (!sess_conn_) {
                reply = err("CreateTable: not connected",
                            openads::AE_NO_CONNECTION);
                break;
            }
            if (f.payload.size() < 6) {
                reply = err("CreateTable: bad payload"); break;
            }
            std::size_t pos = 0;
            std::uint16_t table_type = read_u16_le(f.payload.data() + pos);
            pos += 2;
            std::uint16_t char_type  = read_u16_le(f.payload.data() + pos);
            pos += 2;
            std::uint16_t memo_bs    = read_u16_le(f.payload.data() + pos);
            pos += 2;
            std::string name, fields;
            if (!read_lstr16(f.payload, pos, name) ||
                !read_lstr16(f.payload, pos, fields)) {
                reply = err("CreateTable: short payload"); break;
            }
            if (!ensure_abi_conn()) {
                reply = err("CreateTable: no ABI connection"); break;
            }
            auto nb = to_cbuf(name);
            auto fb = to_cbuf(fields);
            ADSHANDLE hTable = 0;
            UNSIGNED32 rrc = AdsCreateTable(
                abi_conn_, nb.data(), nullptr,
                table_type, char_type, 0, 0, memo_bs,
                fb.data(), &hTable);
            if (rrc != 0) {
                reply = err("CreateTable", rrc); break;
            }
            // Files are on disk under the data dir; release the local
            // handle so the client's subsequent OpenTable can take Shared.
            if (hTable != 0) (void)AdsCloseTable(hTable);
            reply.opcode = Opcode::CreateTableAck;
            break;
        }
        case Opcode::DropTable: {
            if (!sess_conn_) {
                reply = err("DropTable: not connected",
                            openads::AE_NO_CONNECTION);
                break;
            }
            std::size_t pos = 0;
            std::string name;
            if (!read_lstr16(f.payload, pos, name)) {
                reply = err("DropTable: short payload"); break;
            }
            std::uint16_t delete_files = 0;
            if (pos + 2 <= f.payload.size()) {
                delete_files = read_u16_le(f.payload.data() + pos);
            }
            if (!ensure_abi_conn()) {
                reply = err("DropTable: no ABI connection"); break;
            }
            auto nb = to_cbuf(name);
            UNSIGNED32 rrc = AdsDropTable(abi_conn_, nb.data(), delete_files);
            if (rrc != 0) {
                reply = err("DropTable", rrc); break;
            }
            reply.opcode = Opcode::DropTableAck;
            break;
        }
        // ── Server filesystem (EnableFileFunc) ──────────────────────────
        // M12.33 — remote table enumeration. NOT gated by EnableFileFunc
        // because listing tables is a core database operation.
        case Opcode::FindTables: {
            if (!sess_conn_) {
                reply = err("FindTables: not connected",
                            openads::AE_NO_CONNECTION);
                break;
            }
            std::size_t pos = 0;
            std::string mask;
            if (!read_lstr16(f.payload, pos, mask)) {
                reply = err("FindTables: short payload"); break;
            }
            if (mask.empty()) mask = "*.dbf";
            auto r = sess_conn_->find_tables(mask);
            if (!r) {
                reply = err("FindTables",
                    static_cast<UNSIGNED32>(r.error().code));
                break;
            }
            auto& matches = r.value();
            reply.opcode = Opcode::FindTablesAck;
            write_u32_le(static_cast<std::uint32_t>(matches.size()),
                         reply.payload);
            for (const auto& name : matches) {
                write_lstr16(name, reply.payload);
            }
            break;
        }

        case Opcode::FileExists:
        case Opcode::FileErase:
        case Opcode::FileRename:
        case Opcode::FileSize:
        case Opcode::FileMTime:
        case Opcode::Directory:
        case Opcode::DirExist:
        case Opcode::DirMake:
        case Opcode::DirRemove:
        case Opcode::FOpen:
        case Opcode::FCreate:
        case Opcode::FClose:
        case Opcode::FRead:
        case Opcode::FWrite:
        case Opcode::FSeek: {
            if (!srv_->enable_file_func()) {
                reply = err("file functions disabled",
                            openads::AE_ACCESS_DENIED);
                break;
            }
            if (!sess_conn_) {
                reply = err("fs: not connected", openads::AE_NO_CONNECTION);
                break;
            }
            auto deny_path = [&](const std::string&) {
                reply = err("path outside data directory",
                            openads::AE_ACCESS_DENIED);
            };
            if (f.opcode == Opcode::FileExists) {
                std::size_t pos = 0;
                std::string path;
                if (!read_lstr16(f.payload, pos, path)) {
                    reply = err("FileExists: short payload"); break;
                }
                auto abs = resolve_fs_client_path(path);
                if (!abs) { deny_path(path); break; }
                auto ex = openads::engine::fs_exists(*abs);
                if (!ex) {
                    reply = err("FileExists",
                                static_cast<UNSIGNED32>(ex.error().code));
                    break;
                }
                reply.opcode = Opcode::FileExistsAck;
                reply.payload.push_back(ex.value() ? 1 : 0);
            } else if (f.opcode == Opcode::FileErase) {
                std::size_t pos = 0;
                std::string path;
                if (!read_lstr16(f.payload, pos, path)) {
                    reply = err("FileErase: short payload"); break;
                }
                auto abs = resolve_fs_client_path(path);
                if (!abs) { deny_path(path); break; }
                auto r = openads::engine::fs_erase(*abs);
                if (!r) {
                    reply = err("FileErase",
                                static_cast<UNSIGNED32>(r.error().code));
                    break;
                }
                reply.opcode = Opcode::FileEraseAck;
            } else if (f.opcode == Opcode::FileRename) {
                std::size_t pos = 0;
                std::string old_p, new_p;
                if (!read_lstr16(f.payload, pos, old_p) ||
                    !read_lstr16(f.payload, pos, new_p)) {
                    reply = err("FileRename: short payload"); break;
                }
                auto a = resolve_fs_client_path(old_p);
                auto b = resolve_fs_client_path(new_p);
                if (!a || !b) { deny_path(old_p); break; }
                auto r = openads::engine::fs_rename(*a, *b);
                if (!r) {
                    reply = err("FileRename",
                                static_cast<UNSIGNED32>(r.error().code));
                    break;
                }
                reply.opcode = Opcode::FileRenameAck;
            } else if (f.opcode == Opcode::FileSize) {
                std::size_t pos = 0;
                std::string path;
                if (!read_lstr16(f.payload, pos, path)) {
                    reply = err("FileSize: short payload"); break;
                }
                auto abs = resolve_fs_client_path(path);
                if (!abs) { deny_path(path); break; }
                auto sz = openads::engine::fs_size(*abs);
                if (!sz) {
                    reply = err("FileSize",
                                static_cast<UNSIGNED32>(sz.error().code));
                    break;
                }
                reply.opcode = Opcode::FileSizeAck;
                std::uint64_t v = sz.value();
                for (int i = 0; i < 8; ++i)
                    reply.payload.push_back(
                        static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
            } else if (f.opcode == Opcode::FileMTime) {
                std::size_t pos = 0;
                std::string path;
                if (!read_lstr16(f.payload, pos, path)) {
                    reply = err("FileMTime: short payload"); break;
                }
                auto abs = resolve_fs_client_path(path);
                if (!abs) { deny_path(path); break; }
                auto st = openads::engine::fs_stat_entry(*abs);
                if (!st) {
                    reply = err("FileMTime",
                                static_cast<UNSIGNED32>(st.error().code));
                    break;
                }
                const auto& se = st.value();
                reply.opcode = Opcode::FileMTimeAck;
                reply.payload.push_back(
                    static_cast<std::uint8_t>(se.year & 0xFF));
                reply.payload.push_back(
                    static_cast<std::uint8_t>((se.year >> 8) & 0xFF));
                reply.payload.push_back(se.mon);
                reply.payload.push_back(se.day);
                reply.payload.push_back(se.hh);
                reply.payload.push_back(se.mm);
                reply.payload.push_back(se.ss);
            } else if (f.opcode == Opcode::Directory) {
                std::size_t pos = 0;
                std::string mask;
                if (!read_lstr16(f.payload, pos, mask)) {
                    reply = err("Directory: short payload"); break;
                }
                // Split "subdir/*.dbf" and jail-resolve the directory part.
                std::string dir_part = ".";
                std::string pat = mask.empty() ? "*" : mask;
                auto slash = pat.find_last_of("/\\");
                if (slash != std::string::npos) {
                    dir_part = pat.substr(0, slash);
                    if (dir_part.empty()) dir_part = ".";
                    pat = pat.substr(slash + 1);
                    if (pat.empty()) pat = "*";
                }
                auto abs_dir = resolve_fs_client_path(dir_part);
                if (!abs_dir) { deny_path(dir_part); break; }
                auto entries =
                    openads::engine::fs_directory(*abs_dir, pat);
                if (!entries) {
                    reply = err("Directory",
                        static_cast<UNSIGNED32>(entries.error().code));
                    break;
                }
                const auto& elist = entries.value();
                reply.opcode = Opcode::DirectoryAck;
                write_u32_le(static_cast<std::uint32_t>(elist.size()),
                             reply.payload);
                for (const auto& e : elist)
                    openads::engine::pack_dir_entry(e, reply.payload);
            } else if (f.opcode == Opcode::DirExist) {
                std::size_t pos = 0;
                std::string path;
                if (!read_lstr16(f.payload, pos, path)) {
                    reply = err("DirExist: short payload"); break;
                }
                auto abs = resolve_fs_client_path(path);
                if (!abs) { deny_path(path); break; }
                auto ex = openads::engine::fs_dir_exist(*abs);
                if (!ex) {
                    reply = err("DirExist",
                                static_cast<UNSIGNED32>(ex.error().code));
                    break;
                }
                reply.opcode = Opcode::DirExistAck;
                reply.payload.push_back(ex.value() ? 1 : 0);
            } else if (f.opcode == Opcode::DirMake) {
                std::size_t pos = 0;
                std::string path;
                if (!read_lstr16(f.payload, pos, path)) {
                    reply = err("DirMake: short payload"); break;
                }
                auto abs = resolve_fs_client_path(path);
                if (!abs) { deny_path(path); break; }
                auto r = openads::engine::fs_dir_make(*abs);
                if (!r) {
                    reply = err("DirMake",
                                static_cast<UNSIGNED32>(r.error().code));
                    break;
                }
                reply.opcode = Opcode::DirMakeAck;
            } else if (f.opcode == Opcode::DirRemove) {
                std::size_t pos = 0;
                std::string path;
                if (!read_lstr16(f.payload, pos, path)) {
                    reply = err("DirRemove: short payload"); break;
                }
                auto abs = resolve_fs_client_path(path);
                if (!abs) { deny_path(path); break; }
                auto r = openads::engine::fs_dir_remove(*abs);
                if (!r) {
                    reply = err("DirRemove",
                                static_cast<UNSIGNED32>(r.error().code));
                    break;
                }
                reply.opcode = Opcode::DirRemoveAck;
            } else if (f.opcode == Opcode::FOpen ||
                       f.opcode == Opcode::FCreate) {
                std::size_t pos = 0;
                std::string path;
                if (!read_lstr16(f.payload, pos, path)) {
                    reply = err("FOpen: short payload"); break;
                }
                std::uint16_t mode = 0;
                if (pos + 2 <= f.payload.size()) {
                    mode = read_u16_le(f.payload.data() + pos);
                }
                if (files_.size() >= openads::engine::kMaxSessionFiles) {
                    reply = err("too many open files",
                                openads::AE_INTERNAL_ERROR);
                    break;
                }
                auto abs = resolve_fs_client_path(path);
                if (!abs) { deny_path(path); break; }
                bool create = (f.opcode == Opcode::FCreate);
                auto fo = openads::engine::fs_open(
                    *abs, mode, create);
                if (!fo) {
                    reply = err(create ? "FCreate" : "FOpen",
                                static_cast<UNSIGNED32>(fo.error().code));
                    break;
                }
                std::uint32_t id = next_file_id_++;
                files_[id] = std::move(fo.value());
                reply.opcode = create ? Opcode::FCreateAck : Opcode::FOpenAck;
                write_u32_le(id, reply.payload);
            } else if (f.opcode == Opcode::FClose) {
                if (f.payload.size() < 4) {
                    reply = err("FClose: short payload"); break;
                }
                std::uint32_t id = read_u32_le(f.payload.data());
                if (files_.erase(id) == 0) {
                    reply = err("FClose: bad handle",
                                openads::AE_INTERNAL_ERROR);
                    break;
                }
                reply.opcode = Opcode::FCloseAck;
            } else if (f.opcode == Opcode::FRead) {
                if (f.payload.size() < 8) {
                    reply = err("FRead: short payload"); break;
                }
                std::uint32_t id = read_u32_le(f.payload.data());
                std::uint32_t n  = read_u32_le(f.payload.data() + 4);
                if (n > openads::engine::kMaxFsIoChunk)
                    n = openads::engine::kMaxFsIoChunk;
                auto it = files_.find(id);
                if (it == files_.end()) {
                    reply = err("FRead: bad handle",
                                openads::AE_INTERNAL_ERROR);
                    break;
                }
                std::vector<char> buf(n);
                it->second->stream.read(buf.data(),
                                        static_cast<std::streamsize>(n));
                auto got = static_cast<std::uint32_t>(
                    it->second->stream.gcount());
                reply.opcode = Opcode::FReadAck;
                write_u32_le(got, reply.payload);
                reply.payload.insert(
                    reply.payload.end(), buf.begin(),
                    buf.begin() + static_cast<std::ptrdiff_t>(got));
            } else if (f.opcode == Opcode::FWrite) {
                if (f.payload.size() < 8) {
                    reply = err("FWrite: short payload"); break;
                }
                std::uint32_t id = read_u32_le(f.payload.data());
                std::uint32_t n  = read_u32_le(f.payload.data() + 4);
                if (n > openads::engine::kMaxFsIoChunk)
                    n = openads::engine::kMaxFsIoChunk;
                if (8u + n > f.payload.size()) {
                    reply = err("FWrite: short data"); break;
                }
                auto it = files_.find(id);
                if (it == files_.end()) {
                    reply = err("FWrite: bad handle",
                                openads::AE_INTERNAL_ERROR);
                    break;
                }
                it->second->stream.write(
                    reinterpret_cast<const char*>(f.payload.data() + 8),
                    static_cast<std::streamsize>(n));
                it->second->stream.flush();
                if (!it->second->stream) {
                    reply = err("FWrite: io error",
                                openads::AE_INTERNAL_ERROR);
                    break;
                }
                reply.opcode = Opcode::FWriteAck;
                write_u32_le(n, reply.payload);
            } else if (f.opcode == Opcode::FSeek) {
                if (f.payload.size() < 9) {
                    reply = err("FSeek: short payload"); break;
                }
                std::uint32_t id = read_u32_le(f.payload.data());
                std::int32_t off = static_cast<std::int32_t>(
                    read_u32_le(f.payload.data() + 4));
                std::uint8_t origin = f.payload[8];
                auto it = files_.find(id);
                if (it == files_.end()) {
                    reply = err("FSeek: bad handle",
                                openads::AE_INTERNAL_ERROR);
                    break;
                }
                std::ios::seekdir way = std::ios::beg;
                if (origin == 1) way = std::ios::cur;
                else if (origin == 2) way = std::ios::end;
                it->second->stream.clear();
                // ONE shared fstream position — a second seekp with
                // ios::cur would apply a relative offset twice.
                it->second->stream.seekg(off, way);
                auto pos = static_cast<std::uint32_t>(
                    it->second->stream.tellg());
                reply.opcode = Opcode::FSeekAck;
                write_u32_le(pos, reply.payload);
            }
            break;
        }
        case Opcode::SkipUnique: {
            if (f.payload.size() < 8) { reply = err("SkipUnique: bad payload"); break; }
            std::uint32_t iid = read_u32_le(f.payload.data());
            std::int32_t  dir = static_cast<std::int32_t>(
                read_u32_le(f.payload.data() + 4));
            auto iit = index_h_.find(iid);
            if (iit == index_h_.end()) { reply = err("SkipUnique: bad index id"); break; }
            UNSIGNED32 rrc = AdsSkipUnique(iit->second, dir);
            if (rrc != 0) { reply = err("SkipUnique", rrc); break; }
            if (auto tit = index_table_.find(iid); tit != index_table_.end()) {
                sync_engine_cursor(tit->second);
            }
            reply.opcode = Opcode::SkipUniqueAck;
            break;
        }
        case Opcode::SetScope: {
            // Payload: u32 index_id | u16 which | u16 data_type | bytes key.
            if (f.payload.size() < 8) { reply = err("SetScope: bad payload"); break; }
            std::uint32_t iid    = read_u32_le(f.payload.data());
            std::uint16_t which  = read_u16_le(f.payload.data() + 4);
            std::uint16_t dtype  = read_u16_le(f.payload.data() + 6);
            std::size_t   klen   = f.payload.size() - 8;
            auto iit = index_h_.find(iid);
            if (iit == index_h_.end()) { reply = err("SetScope: bad index id"); break; }
            std::vector<UNSIGNED8> kb(klen + 1);
            if (klen > 0) std::memcpy(kb.data(), f.payload.data() + 8, klen);
            UNSIGNED32 rrc = AdsSetScope(
                iit->second, which, kb.data(),
                static_cast<UNSIGNED16>(klen), dtype);
            if (rrc != 0) { reply = err("SetScope", rrc); break; }
            // Scope is stored on the ABI table's active order (via
            // table_for_index inside AdsSetScope). GotoTop/Skip only
            // honour index scope when they route through that ABI
            // handle (ordered_tables_), not the parallel engine Table
            // in tbls_. OrdScope / scoped-relation flows often set
            // scope without a preceding SetOrder wire op — the client
            // may already track active_index_id from auto-opened
            // production CDX tags while ordered_tables_ is still
            // empty, which made remote navigation ignore scope.
            if (auto tit = index_table_.find(iid); tit != index_table_.end()) {
                ordered_tables_.insert(tit->second);
                if (ADSHANDLE ht = ensure_abi_handle(tit->second); ht != 0) {
                    (void)AdsSetIndexOrderByHandle(ht, iit->second);
                    sync_engine_cursor(tit->second);
                }
            }
            reply.opcode = Opcode::SetScopeAck;
            break;
        }
        case Opcode::GetKeyType: {
            // M12.35 — return the key expression result type for a remote
            // index. Payload: [u32 index_id]. Reply: [u16 key_type LE].
            if (f.payload.size() < 4) {
                reply = err("GetKeyType: bad payload"); break;
            }
            std::uint32_t iid = read_u32_le(f.payload.data());
            auto iit = index_h_.find(iid);
            if (iit == index_h_.end()) {
                reply = err("GetKeyType: bad index id"); break;
            }
            UNSIGNED16 kt = 0;
            UNSIGNED32 rrc = AdsGetKeyType(iit->second, &kt);
            if (rrc != 0) { reply = err("GetKeyType", rrc); break; }
            reply.opcode = Opcode::GetKeyTypeAck;
            reply.payload.push_back(static_cast<std::uint8_t>(kt & 0xFF));
            reply.payload.push_back(static_cast<std::uint8_t>((kt >> 8) & 0xFF));
            break;
        }
        case Opcode::ShowDeleted: {
            if (f.payload.size() < 1) {
                reply = err("ShowDeleted: bad payload"); break;
            }
            const bool visible = f.payload[0] != 0;
            show_deleted_ = visible;
            openads::engine::set_show_deleted(visible);
            if (sess_conn_) sess_conn_->set_show_deleted(visible);
            if (abi_conn_ != 0) {
                openads::abi::set_connection_show_deleted(abi_conn_, visible);
            }
            // RCB 07/14/2026: row visibility just changed for EVERY table on
            // this session, so end the read-ahead run on all of them and let
            // the depth ramp back up from the floor.
            //
            // This cannot go through breaks_prefetch_run() like the other
            // run-enders: that mechanism reads the table id from the leading
            // u32 of the payload, and ShowDeleted's payload is a single byte
            // with no table id in it at all. Hence the explicit clear here.
            prefetch_depth_.clear();
            reply.opcode = Opcode::ShowDeletedAck;
            break;
        }
        case Opcode::ClearScope: {
            if (f.payload.size() < 6) { reply = err("ClearScope: bad payload"); break; }
            std::uint32_t iid   = read_u32_le(f.payload.data());
            std::uint16_t which = read_u16_le(f.payload.data() + 4);
            auto iit = index_h_.find(iid);
            if (iit == index_h_.end()) { reply = err("ClearScope: bad index id"); break; }
            UNSIGNED32 rrc = AdsClearScope(iit->second, which);
            if (rrc != 0) { reply = err("ClearScope", rrc); break; }
            reply.opcode = Opcode::ClearScopeAck;
            break;
        }
        // M12.17 — single-frame whole-record read. The server
        // walks every column once with AdsGetField against a
        // suitably-sized buffer (memo lengths queried up-front)
        // and packs each value into the ack so the client can
        // serve subsequent FieldGet calls from cache without
        // another RTT per cell.
        case Opcode::FetchCurrentRow: {
            if (f.payload.size() < 4) { reply = err("FetchCurrentRow: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            reply.opcode = Opcode::FetchCurrentRowAck;
            pack_row_trailer(reply, id);
            break;
        }
        // M12.7 — remote SQL exec. Lazy-creates a parallel ABI
        // connection on the server side; cursor handles returned
        // by AdsExecuteSQLDirect get wrapped in cursor_tbls_ so the
        // existing read-side ops can serve them through the same
        // wire opcodes.
        case Opcode::ExecuteSQL: {
            if (!sess_conn_) { reply = err("ExecuteSQL: not connected"); break; }
            if (abi_conn_ == 0) {
                if (!ensure_abi_conn()) {
                    reply = err("ExecuteSQL: AdsConnect60 failed");
                    break;
                }
                if (AdsCreateSQLStatement(abi_conn_, &abi_stmt_) != 0) {
                    reply = err("ExecuteSQL: AdsCreateSQLStatement failed");
                    break;
                }
            }
            std::vector<UNSIGNED8> sqlbuf(f.payload.size() + 1);
            if (!f.payload.empty()) {
                std::memcpy(sqlbuf.data(), f.payload.data(),
                            f.payload.size());
            }
            sqlbuf[f.payload.size()] = 0;
            ADSHANDLE hCur = 0;
            UNSIGNED32 rrc = AdsExecuteSQLDirect(abi_stmt_,
                                                 sqlbuf.data(), &hCur);
            if (rrc != 0) {
                reply = err("ExecuteSQL: server-side exec failed", rrc);
                break;
            }
            std::uint32_t id = 0;
            if (hCur != 0) {
                id = next_id_++;
                cursor_tbls_.emplace(id, hCur);
            }
            reply.opcode = Opcode::ExecuteSQLAck;
            write_u32_le(id, reply.payload);
            break;
        }
        case Opcode::AppendBlank: {
            if (f.payload.size() < 4) { reply = err("AppendBlank: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("AppendBlank: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("AppendBlank: lookup failed"); break; }
            // M12.16 dual-handle: CreateIndex/OpenIndex bind bags on the
            // parallel ABI Table (tbls_h_), not on the engine Table used by
            // the historical write path. Writing only through the engine
            // left production CDX bags empty after remote APPEND (Pritpal
            // TestIndex: 36 rows / 0 keys). Prefer the ABI handle when it
            // exists so sync_all_indexes_ updates every bound tag.
            if (auto hit = tbls_h_.find(id); hit != tbls_h_.end()) {
                UNSIGNED32 rrc = AdsAppendRecord(hit->second);
                if (rrc != 0) { reply = err("AppendBlank", rrc); break; }
                // Persist dirty index pages so a later reopen of the bag
                // (or another station) sees the new keys.
                (void)AdsFlushFileBuffers(hit->second);
                sync_engine_cursor(id);
                tbl->set_pending_append(true);
            } else {
                auto r = tbl->append_record();
                if (!r) { reply = err("AppendBlank: append_record failed"); break; }
                tbl->set_pending_append(true);
            }
            reply.opcode = Opcode::AppendBlankAck;
            break;
        }
        case Opcode::SetField: {
            // payload: [u32 tid][u16 name_len][name bytes][value bytes...]
            if (f.payload.size() < 6) { reply = err("SetField: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            std::uint16_t nlen = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(f.payload[4]) |
                (static_cast<std::uint16_t>(f.payload[5]) << 8));
            if (f.payload.size() < 6u + nlen) {
                reply = err("SetField: truncated"); break;
            }
            std::string fname(reinterpret_cast<const char*>(
                                  f.payload.data() + 6),
                              nlen);
            std::string val(reinterpret_cast<const char*>(
                                f.payload.data() + 6 + nlen),
                            f.payload.size() - 6 - nlen);

            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("SetField: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("SetField: lookup failed"); break; }

            // When indexes live on the parallel ABI handle, write through
            // AdsSetString so the bag is maintained (see AppendBlank).
            if (auto hit = tbls_h_.find(id); hit != tbls_h_.end()) {
                // The engine cursor is authoritative: navigation opcodes
                // (GotoRecord / unordered Skip) move only the engine table,
                // the twin is synced lazily for ordered walks. Land the
                // twin on the same row before writing through it —
                // otherwise the 5035 write guard checks the lock of
                // whatever record the twin happens to sit on (e.g. a fresh
                // auto-locked append) and the write lands on the WRONG row.
                UNSIGNED32 cur = 0;
                AdsGetRecordNum(hit->second, 0, &cur);
                UNSIGNED32 eng = tbl->recno();
                if (eng != 0 && cur != eng) {
                    (void)AdsGotoRecord(hit->second, eng);
                }
                std::vector<UNSIGNED8> fn(fname.begin(), fname.end());
                fn.push_back(0);
                std::vector<UNSIGNED8> vv(val.begin(), val.end());
                // AdsSetString takes length; do not require NUL in value.
                UNSIGNED32 rrc = AdsSetString(
                    hit->second, fn.data(),
                    vv.empty() ? reinterpret_cast<UNSIGNED8*>(const_cast<char*>(""))
                               : vv.data(),
                    static_cast<UNSIGNED32>(val.size()));
                if (rrc != 0) {
                    reply = err("SetField: write failed", rrc);
                    break;
                }
                (void)AdsFlushFileBuffers(hit->second);
                sync_engine_cursor(id);
                reply.opcode = Opcode::SetFieldAck;
                break;
            }

            std::int32_t fi = tbl->field_index(fname);
            if (fi < 0) { reply = err("SetField: column not found"); break; }
            const auto& fdesc =
                tbl->field_descriptor(static_cast<std::uint16_t>(fi));
            util::Result<void> r;
            auto fi_u = static_cast<std::uint16_t>(fi);
            switch (fdesc.type) {
                case drivers::DbfFieldType::Logical: {
                    bool lv = !val.empty() &&
                        (val[0] == '1' || val[0] == 'T' || val[0] == 't' ||
                         val[0] == 'Y' || val[0] == 'y');
                    r = tbl->set_field(fi_u, lv);
                    break;
                }
                case drivers::DbfFieldType::Integer:
                case drivers::DbfFieldType::AutoInc:
                case drivers::DbfFieldType::Double:
                case drivers::DbfFieldType::ShortInt:
                case drivers::DbfFieldType::Currency:
                case drivers::DbfFieldType::AdtMoney:
                case drivers::DbfFieldType::Time:
                case drivers::DbfFieldType::Numeric:
                    try {
                        r = tbl->set_field(fi_u, std::stod(val));
                    } catch (...) {
                        r = tbl->set_field(fi_u, val);
                    }
                    break;
                default:
                    r = tbl->set_field(fi_u, val);
                    break;
            }
            if (!r) { reply = err("SetField: write failed", 5035); break; }
            reply.opcode = Opcode::SetFieldAck;
            break;
        }
        case Opcode::DeleteRecord: {
            if (f.payload.size() < 4) { reply = err("DeleteRecord: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("DeleteRecord: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("DeleteRecord: lookup failed"); break; }
            if (auto hit = tbls_h_.find(id); hit != tbls_h_.end()) {
                // Same twin-cursor sync as SetField: the delete must hit the
                // row the engine cursor sits on, and the write guard must
                // evaluate that row's lock.
                UNSIGNED32 cur = 0;
                AdsGetRecordNum(hit->second, 0, &cur);
                UNSIGNED32 eng = tbl->recno();
                if (eng != 0 && cur != eng) {
                    (void)AdsGotoRecord(hit->second, eng);
                }
                UNSIGNED32 rrc = AdsDeleteRecord(hit->second);
                if (rrc != 0) { reply = err("DeleteRecord", rrc); break; }
                sync_engine_cursor(id);
            } else {
                auto r = tbl->mark_deleted();
                if (!r) {
                    reply = err("DeleteRecord: mark_deleted failed",
                                static_cast<UNSIGNED32>(r.error().code));
                    break;
                }
            }
            reply.opcode = Opcode::DeleteRecordAck;
            break;
        }
        case Opcode::RecallRecord: {
            if (f.payload.size() < 4) { reply = err("RecallRecord: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("RecallRecord: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("RecallRecord: lookup failed"); break; }
            if (auto hit = tbls_h_.find(id); hit != tbls_h_.end()) {
                // Mirror DeleteRecord: locks taken via LockRecord land on
                // the ABI twin, so recall must go through it too (with the
                // same cursor sync) or the engine-side guard sees no lock.
                UNSIGNED32 cur = 0;
                AdsGetRecordNum(hit->second, 0, &cur);
                UNSIGNED32 eng = tbl->recno();
                if (eng != 0 && cur != eng) {
                    (void)AdsGotoRecord(hit->second, eng);
                }
                UNSIGNED32 rrc = AdsRecallRecord(hit->second);
                if (rrc != 0) { reply = err("RecallRecord", rrc); break; }
                sync_engine_cursor(id);
            } else {
                auto r = tbl->recall_deleted();
                if (!r) {
                    reply = err("RecallRecord: recall_deleted failed",
                                static_cast<UNSIGNED32>(r.error().code));
                    break;
                }
            }
            reply.opcode = Opcode::RecallRecordAck;
            break;
        }
        case Opcode::GotoRecord: {
            if (f.payload.size() < 8) { reply = err("GotoRecord: bad payload"); break; }
            std::uint32_t id    = read_u32_le(f.payload.data());
            std::uint32_t recno = read_u32_le(f.payload.data() + 4);
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("GotoRecord: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("GotoRecord: lookup failed"); break; }
            auto r = tbl->goto_record(recno);
            if (!r) { reply = err("GotoRecord: failed"); break; }
            // Physical GOTO moves only the engine cursor. When an order
            // is active the parallel ABI handle (used by ordered Skip) must
            // land on the same recno or xBrowse's bookmark DbGoto restore
            // leaves the next index Skip walking from a stale key.
            if (ordered_tables_.count(id)) {
                if (ADSHANDLE hord = ensure_abi_handle(id)) {
                    (void)AdsGotoRecord(hord, recno);
                }
            }
            reply.opcode = Opcode::GotoRecordAck;
            pack_row_trailer(reply, id);
            break;
        }
        case Opcode::FlushTable: {
            if (f.payload.size() < 4) { reply = err("FlushTable: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("FlushTable: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("FlushTable: lookup failed"); break; }
            // Flush the ABI twin first (holds open index bags), then engine.
            if (auto hit = tbls_h_.find(id); hit != tbls_h_.end()) {
                (void)AdsFlushFileBuffers(hit->second);
            }
            // Remote AdsWriteRecord lands here: the append is now committed,
            // so drop the pending-append marker (mirrors the local
            // AdsWriteRecord which clears it). Without this the flag stayed
            // set for the life of the server-side table.
            tbl->set_pending_append(false);
            auto r = tbl->flush();
            if (!r) { reply = err("FlushTable: flush failed"); break; }
            reply.opcode = Opcode::FlushTableAck;
            break;
        }
        case Opcode::Fetch: {
            // M12.11 — payload:
            //   [u32 tid][u32 max_rows][u8 ncols][u8 nlen][name]...
            // Reply:
            //   [u32 nrows][u8 ncols][per row, per col: u16 vlen][val]
            if (f.payload.size() < 9) { reply = err("Fetch: bad payload"); break; }
            std::uint32_t id      = read_u32_le(f.payload.data());
            std::uint32_t maxrows = read_u32_le(f.payload.data() + 4);
            std::uint8_t  ncols   = f.payload[8];
            std::vector<std::string> cols;
            cols.reserve(ncols);
            std::size_t p = 9;
            bool parse_ok = true;
            for (std::uint8_t c = 0; c < ncols && parse_ok; ++c) {
                if (p + 1 > f.payload.size()) {
                    reply = err("Fetch: truncated col header");
                    parse_ok = false; break;
                }
                std::uint8_t nlen = f.payload[p++];
                if (p + nlen > f.payload.size()) {
                    reply = err("Fetch: truncated col name");
                    parse_ok = false; break;
                }
                cols.emplace_back(
                    reinterpret_cast<const char*>(f.payload.data() + p),
                    nlen);
                p += nlen;
            }
            if (!parse_ok) break;

            auto write_u16_le = [](std::vector<std::uint8_t>& out,
                                   std::uint16_t v) {
                out.push_back(static_cast<std::uint8_t>( v        & 0xFFu));
                out.push_back(static_cast<std::uint8_t>((v >>  8) & 0xFFu));
            };

            reply.opcode = Opcode::FetchAck;
            std::vector<std::uint8_t> rowbuf;
            std::uint32_t nrows_out = 0;

            if (auto cit = cursor_tbls_.find(id); cit != cursor_tbls_.end()) {
                ADSHANDLE  hCur  = cit->second;
                UNSIGNED16 atend = 0;
                AdsAtEOF(hCur, &atend);
                while (atend == 0 && nrows_out < maxrows) {
                    for (auto& cn : cols) {
                        UNSIGNED8  fbuf[64]  = {0};
                        UNSIGNED8  out [4096] = {0};
                        UNSIGNED32 cap = sizeof(out);
                        std::size_t n = std::min<std::size_t>(
                            cn.size(), sizeof(fbuf) - 1);
                        std::memcpy(fbuf, cn.data(), n);
                        fbuf[n] = 0;
                        UNSIGNED32 rrc = AdsGetField(hCur, fbuf,
                                                     out, &cap, 0);
                        if (rrc != 0) cap = 0;
                        write_u16_le(rowbuf,
                            static_cast<std::uint16_t>(cap));
                        rowbuf.insert(rowbuf.end(), out, out + cap);
                    }
                    ++nrows_out;
                    if (AdsSkip(hCur, 1) != 0) break;
                    AdsAtEOF(hCur, &atend);
                }
            } else if (auto it = tbls_.find(id);
                       it != tbls_.end() && sess_conn_) {
                auto* tbl = sess_conn_->lookup_table(it->second);
                if (!tbl) { reply = err("Fetch: lookup failed"); break; }
                while (!tbl->eof() && nrows_out < maxrows) {
                    for (auto& cn : cols) {
                        std::int32_t fi = tbl->field_index(cn);
                        std::string val;
                        if (fi >= 0) {
                            auto v = tbl->read_field(
                                static_cast<std::uint16_t>(fi));
                            if (v) val = v.value().as_string;
                        }
                        write_u16_le(rowbuf,
                            static_cast<std::uint16_t>(val.size()));
                        rowbuf.insert(rowbuf.end(),
                                      val.begin(), val.end());
                    }
                    ++nrows_out;
                    auto sk = tbl->skip(1);
                    if (!sk) break;
                }
            } else {
                reply = err("Fetch: bad table id"); break;
            }

            write_u32_le(nrows_out, reply.payload);
            reply.payload.push_back(ncols);
            reply.payload.insert(reply.payload.end(),
                                 rowbuf.begin(), rowbuf.end());
            break;
        }
            case Opcode::FetchWhere: {
                // Tier-2 server-side filtered scan. Payload:
                //   [u32 tid][u32 max_rows][u8 flags][u16 exprlen][expr]
                //   [u8 ncols][u8 nlen][name]...
                // Reply (FetchWhereAck):
                //   [u32 nrows][u8 ncols]
                //   [per row: (u32 recno IF WANT_RECNO)(per col: u16 vlen,val)]
                //   [u8 eof]
                // flags=0 reply is byte-identical to v1.4.0 (backward compat).
                // The server walks the table from the cursor's current
                // position, evaluating `expr` (Clipper-style FOR
                // predicate) per row, and emits only the matching rows'
                // requested columns until `max_rows` matches or EOF.
                // The cursor is left positioned past the last examined
                // row so a follow-up FetchWhere resumes the scan.
                if (f.payload.size() < 12) {
                    reply = err("FetchWhere: bad payload"); break;
                }
                std::uint32_t id      = read_u32_le(f.payload.data());
                std::uint32_t maxrows = read_u32_le(f.payload.data() + 4);
                std::uint8_t  flags   = f.payload[8];
                std::uint16_t elen    =
                    static_cast<std::uint16_t>(
                        static_cast<std::uint32_t>(f.payload[9]) |
                        (static_cast<std::uint32_t>(f.payload[10]) << 8));
                std::size_t p = 11;
                if (p + elen > f.payload.size()) {
                    reply = err("FetchWhere: truncated expr"); break;
                }
                std::string expr(
                    reinterpret_cast<const char*>(f.payload.data() + p),
                    elen);
                p += elen;
                if (p + 1 > f.payload.size()) {
                    reply = err("FetchWhere: missing ncols"); break;
                }
                std::uint8_t ncols = f.payload[p++];
                std::vector<std::string> cols;
                cols.reserve(ncols);
                bool parse_ok = true;
                for (std::uint8_t c = 0; c < ncols && parse_ok; ++c) {
                    if (p + 1 > f.payload.size()) {
                        reply = err("FetchWhere: truncated col header");
                        parse_ok = false; break;
                    }
                    std::uint8_t nlen = f.payload[p++];
                    if (p + nlen > f.payload.size()) {
                        reply = err("FetchWhere: truncated col name");
                        parse_ok = false; break;
                    }
                    cols.emplace_back(
                        reinterpret_cast<const char*>(f.payload.data() + p),
                        nlen);
                    p += nlen;
                }
                if (!parse_ok) break;

                auto write_u16_le = [](std::vector<std::uint8_t>& out,
                                       std::uint16_t v) {
                    out.push_back(static_cast<std::uint8_t>( v        & 0xFFu));
                    out.push_back(static_cast<std::uint8_t>((v >>  8) & 0xFFu));
                };

                // Slice 1 is base-table only: a SQL cursor already
                // filters server-side via its WHERE clause, and the
                // FOR-predicate evaluator needs an engine Table to read
                // the current record. Reject cursor ids with a clear
                // error rather than silently mis-filtering.
                if (cursor_tbls_.find(id) != cursor_tbls_.end()) {
                    reply = err("FetchWhere: not supported on SQL "
                                "cursors (use SQL WHERE)");
                    break;
                }
                auto it = tbls_.find(id);
                if (it == tbls_.end() || !sess_conn_) {
                    reply = err("FetchWhere: bad table id"); break;
                }
                auto* tbl = sess_conn_->lookup_table(it->second);
                if (!tbl) { reply = err("FetchWhere: lookup failed"); break; }

                reply.opcode = Opcode::FetchWhereAck;
                std::vector<std::uint8_t> rowbuf;
                std::uint32_t nrows_out = 0;
                while (!tbl->eof() && nrows_out < maxrows) {
                    if (openads::engine::evaluate_index_expr_truthy(
                            *tbl, expr)) {
                        if (flags & FetchWhereFlags::WANT_RECNO) {
                            write_u32_le(tbl->recno(), rowbuf);
                        }
                        for (auto& cn : cols) {
                            std::int32_t fi = tbl->field_index(cn);
                            std::string val;
                            if (fi >= 0) {
                                auto v = tbl->read_field(
                                    static_cast<std::uint16_t>(fi));
                                if (v) val = v.value().as_string;
                            }
                            write_u16_le(rowbuf,
                                static_cast<std::uint16_t>(val.size()));
                            rowbuf.insert(rowbuf.end(),
                                          val.begin(), val.end());
                        }
                        ++nrows_out;
                    }
                    auto sk = tbl->skip(1);
                    if (!sk) break;
                }

                write_u32_le(nrows_out, reply.payload);
                reply.payload.push_back(ncols);
                reply.payload.insert(reply.payload.end(),
                                     rowbuf.begin(), rowbuf.end());
                reply.payload.push_back(
                    static_cast<std::uint8_t>(tbl->eof() ? 1 : 0));
                break;
            }
            case Opcode::CustomizeAOF: {
                if (f.payload.size() < 7) {
                    reply = err("CustomizeAOF: bad payload"); break;
                }
                std::uint32_t id = read_u32_le(f.payload.data());
                std::uint8_t  opt = f.payload[4];
                std::uint16_t nrecs = static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(f.payload[5]) |
                    (static_cast<std::uint16_t>(f.payload[6]) << 8));
                std::size_t p = 7;
                if (f.payload.size() < p + static_cast<std::size_t>(nrecs) * 4u) {
                    reply = err("CustomizeAOF: truncated recnos"); break;
                }
                if (cursor_tbls_.find(id) != cursor_tbls_.end()) {
                    reply = err("CustomizeAOF: not supported on SQL cursors");
                    break;
                }
                auto it = tbls_.find(id);
                if (it == tbls_.end() || !sess_conn_) {
                    reply = err("CustomizeAOF: bad table id"); break;
                }
                auto* tbl = sess_conn_->lookup_table(it->second);
                if (!tbl) { reply = err("CustomizeAOF: lookup failed"); break; }
                if (!tbl->aof_active()) {
                    reply = err("CustomizeAOF: no active AOF"); break;
                }
                bool include = false;
                if (opt == 1) include = true;
                else if (opt == 2) include = false;
                else { reply = err("CustomizeAOF: invalid option"); break; }
                for (std::uint16_t i = 0; i < nrecs; ++i) {
                    std::uint32_t recno = read_u32_le(f.payload.data() + p);
                    p += 4;
                    (void)tbl->customize_aof_record(recno, include);
                }
                reply.opcode = Opcode::CustomizeAOFAck;
                break;
            }
            case Opcode::GetRecord: {
                if (f.payload.size() < 4) {
                    reply = err("GetRecord: bad payload"); break;
                }
                std::uint32_t id = read_u32_le(f.payload.data());
                if (cursor_tbls_.find(id) != cursor_tbls_.end()) {
                    reply = err("GetRecord: not supported on SQL cursors");
                    break;
                }
                auto it = tbls_.find(id);
                if (it == tbls_.end() || !sess_conn_) {
                    reply = err("GetRecord: bad table id"); break;
                }
                auto* tbl = sess_conn_->lookup_table(it->second);
                if (!tbl) { reply = err("GetRecord: lookup failed"); break; }
                if (!tbl->positioned() || tbl->eof() || tbl->bof() ||
                    tbl->recno() == 0) {
                    reply = err("GetRecord: no current record"); break;
                }
                const auto& buf = tbl->record_buffer();
                if (buf.size() > 0xFFFFu) {
                    reply = err("GetRecord: record too large"); break;
                }
                reply.opcode = Opcode::GetRecordAck;
                auto write_u16 = [](std::vector<std::uint8_t>& out,
                                    std::uint16_t v) {
                    out.push_back(static_cast<std::uint8_t>( v       & 0xFFu));
                    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
                };
                write_u16(reply.payload,
                          static_cast<std::uint16_t>(buf.size()));
                reply.payload.insert(reply.payload.end(),
                                     buf.begin(), buf.end());
                break;
            }
            case Opcode::GetRecordCRC: {
                if (f.payload.size() < 4) {
                    reply = err("GetRecordCRC: bad payload"); break;
                }
                std::uint32_t id = read_u32_le(f.payload.data());
                if (cursor_tbls_.find(id) != cursor_tbls_.end()) {
                    reply = err("GetRecordCRC: not supported on SQL cursors");
                    break;
                }
                auto it = tbls_.find(id);
                if (it == tbls_.end() || !sess_conn_) {
                    reply = err("GetRecordCRC: bad table id"); break;
                }
                auto* tbl = sess_conn_->lookup_table(it->second);
                if (!tbl) { reply = err("GetRecordCRC: lookup failed"); break; }
                if (!tbl->positioned() || tbl->eof() || tbl->bof() ||
                    tbl->recno() == 0) {
                    reply = err("GetRecordCRC: no current record"); break;
                }
                const std::uint32_t crc =
                    openads::engine::crc32_record(tbl->record_buffer());
                reply.opcode = Opcode::GetRecordCRAck;
                reply.payload.push_back(static_cast<std::uint8_t>( crc        & 0xFFu));
                reply.payload.push_back(static_cast<std::uint8_t>((crc >>  8) & 0xFFu));
                reply.payload.push_back(static_cast<std::uint8_t>((crc >> 16) & 0xFFu));
                reply.payload.push_back(static_cast<std::uint8_t>((crc >> 24) & 0xFFu));
                break;
            }
            case Opcode::SetRecord: {
                if (f.payload.size() < 6) {
                    reply = err("SetRecord: bad payload"); break;
                }
                std::uint32_t id = read_u32_le(f.payload.data());
                std::uint16_t rlen = static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(f.payload[4]) |
                    (static_cast<std::uint16_t>(f.payload[5]) << 8));
                if (f.payload.size() < 6u + rlen) {
                    reply = err("SetRecord: truncated record"); break;
                }
                if (cursor_tbls_.find(id) != cursor_tbls_.end()) {
                    reply = err("SetRecord: not supported on SQL cursors");
                    break;
                }
                auto it = tbls_.find(id);
                if (it == tbls_.end() || !sess_conn_) {
                    reply = err("SetRecord: bad table id"); break;
                }
                auto* tbl = sess_conn_->lookup_table(it->second);
                if (!tbl) { reply = err("SetRecord: lookup failed"); break; }
                auto r = tbl->set_record_raw(f.payload.data() + 6,
                                             static_cast<std::size_t>(rlen));
                if (!r) { reply = err("SetRecord: write failed"); break; }
                reply.opcode = Opcode::SetRecordAck;
                break;
            }
            case Opcode::Aggregate: {
                // Tier-3 server-side aggregation. Payload:
                //   [u32 tid][u16 forlen][for_expr][u8 n_aggs]
                //     per agg: [u8 fn_type][u8 nlen][field_name]
                // Reply (AggregateAck):
                //   [u8 n_aggs] per agg: [u8 result_type][u16 vlen][val]
                // The server scans the whole table once (independent of the
                // cursor position), folds each matching row into the
                // accumulators, and returns one scalar per requested agg.
                if (f.payload.size() < 7) {
                    reply = err("Aggregate: bad payload"); break;
                }
                std::uint32_t id   = read_u32_le(f.payload.data());
                std::uint16_t flen =
                    static_cast<std::uint16_t>(
                        static_cast<std::uint32_t>(f.payload[4]) |
                        (static_cast<std::uint32_t>(f.payload[5]) << 8));
                std::size_t p = 6;
                if (p + flen > f.payload.size()) {
                    reply = err("Aggregate: truncated expr"); break;
                }
                std::string for_expr(
                    reinterpret_cast<const char*>(f.payload.data() + p), flen);
                p += flen;
                if (p + 1 > f.payload.size()) {
                    reply = err("Aggregate: missing n_aggs"); break;
                }
                std::uint8_t naggs = f.payload[p++];
                struct AggReq { std::uint8_t fn; std::string field; };
                std::vector<AggReq> specs;
                specs.reserve(naggs);
                bool parse_ok = true;
                for (std::uint8_t i = 0; i < naggs && parse_ok; ++i) {
                    if (p + 2 > f.payload.size()) {
                        reply = err("Aggregate: truncated agg header");
                        parse_ok = false; break;
                    }
                    AggReq s;
                    s.fn = f.payload[p++];
                    std::uint8_t nlen = f.payload[p++];
                    if (p + nlen > f.payload.size()) {
                        reply = err("Aggregate: truncated field name");
                        parse_ok = false; break;
                    }
                    s.field.assign(
                        reinterpret_cast<const char*>(f.payload.data() + p),
                        nlen);
                    p += nlen;
                    specs.push_back(std::move(s));
                }
                if (!parse_ok) break;

                // Base tables only — a SQL cursor aggregates via SQL.
                if (cursor_tbls_.find(id) != cursor_tbls_.end()) {
                    reply = err("Aggregate: not supported on SQL cursors "
                                "(use SQL aggregates)");
                    break;
                }
                auto it = tbls_.find(id);
                if (it == tbls_.end() || !sess_conn_) {
                    reply = err("Aggregate: bad table id"); break;
                }
                auto* tbl = sess_conn_->lookup_table(it->second);
                if (!tbl) { reply = err("Aggregate: lookup failed"); break; }

                auto field_is_numeric =
                    [](openads::drivers::DbfFieldType t) {
                        using T = openads::drivers::DbfFieldType;
                        switch (t) {
                            case T::Numeric:  case T::Float:
                            case T::Integer:  case T::Currency:
                            case T::Double:   case T::ShortInt:
                            case T::AutoInc:  case T::AdtMoney:
                                return true;
                            default:
                                return false;
                        }
                    };

                // One accumulator per spec; resolve field index + numeric-ness
                // (the latter drives numeric vs lexical MIN/MAX).
                std::vector<openads::engine::AggAccumulator> accs;
                std::vector<std::int32_t> fidx;
                accs.reserve(specs.size());
                fidx.reserve(specs.size());
                // Validate every spec before touching the table: an unknown
                // field name (field_index -> -1) must be rejected, never folded
                // as COUNT(*) -- that would silently return the row count for a
                // typo'd or injected field. Only COUNT may omit the field.
                bool specs_ok = true;
                for (const auto& s : specs) {
                    const auto fn = static_cast<openads::engine::AggFn>(s.fn);
                    if (s.field.empty()) {
                        if (fn != openads::engine::AggFn::Count) {
                            reply = err("Aggregate: empty field only valid for "
                                        "COUNT");
                            specs_ok = false;
                            break;
                        }
                        fidx.push_back(-1);
                        accs.emplace_back(fn, false);
                        continue;
                    }
                    std::int32_t fi = tbl->field_index(s.field);
                    if (fi < 0) {
                        reply = err("Aggregate: unknown field " + s.field);
                        specs_ok = false;
                        break;
                    }
                    const auto& fd =
                        tbl->field_descriptor(static_cast<std::uint16_t>(fi));
                    accs.emplace_back(fn, field_is_numeric(fd.type));
                    fidx.push_back(fi);
                }
                if (!specs_ok) break;

                // Scan the whole table once, restoring the cursor afterwards.
                std::uint32_t saved   = tbl->recno();
                bool          was_eof = tbl->eof();
                tbl->goto_top();
                while (!tbl->eof()) {
                    if (openads::engine::evaluate_index_expr_truthy(
                            *tbl, for_expr)) {
                        for (std::size_t i = 0; i < accs.size(); ++i) {
                            if (fidx[i] < 0) {
                                accs[i].feed(false, 0.0, "");   // COUNT(*)
                            } else {
                                auto v = tbl->read_field(
                                    static_cast<std::uint16_t>(fidx[i]));
                                if (v) {
                                    const auto& dv = v.value();
                                    accs[i].feed(dv.is_null, dv.as_double,
                                                 dv.as_string);
                                } else {
                                    accs[i].feed(true, 0.0, "");
                                }
                            }
                        }
                    }
                    if (!tbl->skip(1)) break;
                }
                if (!was_eof && saved >= 1 && saved <= tbl->record_count())
                    tbl->goto_record(saved);
                else
                    tbl->goto_top();

                reply.opcode = Opcode::AggregateAck;
                auto write_u16 = [](std::vector<std::uint8_t>& out,
                                    std::uint16_t v) {
                    out.push_back(static_cast<std::uint8_t>( v       & 0xFFu));
                    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
                };
                reply.payload.push_back(
                    static_cast<std::uint8_t>(accs.size()));
                for (auto& a : accs) {
                    openads::engine::AggValue val = a.finalize();
                    reply.payload.push_back(
                        static_cast<std::uint8_t>(val.type));
                    write_u16(reply.payload,
                              static_cast<std::uint16_t>(val.bytes.size()));
                    reply.payload.insert(reply.payload.end(),
                                         val.bytes.begin(), val.bytes.end());
                }
                break;
            }
        case Opcode::Reindex: {
            if (f.payload.size() < 4) { reply = err("Reindex: bad payload"); break; }
            std::uint32_t id = read_u32_le(f.payload.data());
            auto it = tbls_.find(id);
            if (it == tbls_.end() || !sess_conn_) {
                reply = err("Reindex: bad table id"); break;
            }
            auto* tbl = sess_conn_->lookup_table(it->second);
            if (!tbl) { reply = err("Reindex: lookup failed"); break; }
            auto r = tbl->reindex();
            if (!r) { reply = err("Reindex: reindex failed"); break; }
            reply.opcode = Opcode::ReindexAck;
            break;
        }
        case Opcode::MgConnect: {
            // Management handshake. Optional [u16 ulen][user] payload —
            // when a caller (e.g. DA-Web's Server Info tab) knows which DD
            // user it's asking on behalf of, register this mgmt session
            // under that name so it shows up as (say) "adssys" instead of
            // "(anonymous)" in AdsMgGetUserNames / the Connected Users grid.
            // Every accepted socket already gets a session (register_session
            // in Session::run(), before any opcode is dispatched), so this
            // mgmt-only connection is already tracked — it just never had a
            // user name attached to it until now.
            if (f.payload.size() >= 2) {
                std::uint16_t ulen = static_cast<std::uint16_t>(
                    static_cast<unsigned>(f.payload[0]) |
                    (static_cast<unsigned>(f.payload[1]) << 8));
                if (f.payload.size() >= static_cast<std::size_t>(2 + ulen) && ulen > 0) {
                    std::string mgUser(reinterpret_cast<const char*>(f.payload.data() + 2), ulen);
                    srv_->set_session_user(sid_, mgUser, "");
                }
            }
            reply.opcode = Opcode::MgConnectAck;
            std::string ok = "mg-ok";
            reply.payload.assign(ok.begin(), ok.end());
            break;
        }
        case Opcode::MgRequest: {
            // Iterator-based ctor: payload.data() may be nullptr when empty,
            // and std::string(nullptr, 0) is UB.
            std::string reqbuf(f.payload.begin(), f.payload.end());
            auto req = decode_mg_request(reqbuf);
            if (!req) {
                reply = err("bad mg request");
                break;
            }
            switch (req.value().kind) {
                case MgRequestKind::Snapshot: {
                    reply.opcode = Opcode::MgReplyAck;
                    std::string snap =
                        encode_mg_snapshot(srv_->build_mg_snapshot());
                    reply.payload.assign(snap.begin(), snap.end());
                    break;
                }
                case MgRequestKind::KillUser: {
                    // arg is the 1-based connection number; map it
                    // to the matching session id and kill it.
                    srv_->kill_session_by_conn_no(req.value().arg);
                    reply.opcode = Opcode::MgReplyAck;
                    break;
                }
                case MgRequestKind::ResetCommStats: {
                    openads::mgmt::process_mg_stats().reset_comm();
                    reply.opcode = Opcode::MgReplyAck;
                    break;
                }
                case MgRequestKind::DumpTables: {
                    reply.opcode = Opcode::MgReplyAck;
                    break;
                }
                default:
                    reply = err("unknown mg request kind");
                    break;
            }
            break;
        }
        // M12.29 — AdsDD* Data Dictionary property API, phase 1. See
        // docs/wire-protocol.md §9 / wire.h for the payload formats.
        case Opcode::DDGetProperty: {
            if (!ensure_abi_conn()) { reply = err("DDGetProperty: connect failed"); break; }
            if (f.payload.empty()) { reply = err("DDGetProperty: bad payload"); break; }
            auto kind = static_cast<DDObjectKind>(f.payload[0]);
            std::size_t off = 1;
            std::string name, subName;
            if (!read_lstr16(f.payload, off, name) ||
                !read_lstr16(f.payload, off, subName) ||
                off + 2 > f.payload.size()) {
                reply = err("DDGetProperty: bad payload"); break;
            }
            UNSIGNED16 propId = read_u16_le(f.payload.data() + off);
            UNSIGNED16 len = 0;
            UNSIGNED32 rc = dd_get_property_dispatch(abi_conn_, kind, name, subName,
                                                      propId, nullptr, &len);
            if (rc != 0) { reply = err("DDGetProperty", rc); break; }
            std::vector<UNSIGNED8> buf(len > 0 ? len : 1);
            UNSIGNED16 cap = len;
            rc = dd_get_property_dispatch(abi_conn_, kind, name, subName, propId,
                                           buf.data(), &cap);
            if (rc != 0) { reply = err("DDGetProperty", rc); break; }
            reply.opcode = Opcode::DDGetPropertyAck;
            write_u32_le(cap, reply.payload);
            reply.payload.insert(reply.payload.end(), buf.begin(), buf.begin() + cap);
            break;
        }
        case Opcode::DDSetProperty: {
            if (!ensure_abi_conn()) { reply = err("DDSetProperty: connect failed"); break; }
            if (f.payload.empty()) { reply = err("DDSetProperty: bad payload"); break; }
            auto kind = static_cast<DDObjectKind>(f.payload[0]);
            std::size_t off = 1;
            std::string name, subName;
            if (!read_lstr16(f.payload, off, name) ||
                !read_lstr16(f.payload, off, subName) ||
                off + 2 > f.payload.size()) {
                reply = err("DDSetProperty: bad payload"); break;
            }
            UNSIGNED16 propId = read_u16_le(f.payload.data() + off);
            off += 2;
            if (off + 4 > f.payload.size()) { reply = err("DDSetProperty: bad payload"); break; }
            UNSIGNED32 valLen = read_u32_le(f.payload.data() + off);
            off += 4;
            if (off + valLen > f.payload.size()) { reply = err("DDSetProperty: bad payload"); break; }
            // The underlying local AdsDDSet*Property signatures take a
            // 16-bit length (the same cap ads_misc.c's PHP extension
            // already applies for local calls) — not a wire limitation.
            if (valLen > 0xFFFFu) { reply = err("DDSetProperty: value too large"); break; }
            void* valPtr = valLen > 0
                ? const_cast<std::uint8_t*>(f.payload.data() + off) : nullptr;
            UNSIGNED32 rc = dd_set_property_dispatch(abi_conn_, kind, name, subName,
                propId, valPtr, static_cast<UNSIGNED16>(valLen));
            if (rc != 0) { reply = err("DDSetProperty", rc); break; }
            reply.opcode = Opcode::DDSetPropertyAck;
            break;
        }
        case Opcode::DDCreateProc: {
            if (!ensure_abi_conn()) { reply = err("DDCreateProc: connect failed"); break; }
            std::size_t off = 0;
            std::string name, container, procName, inParams, outParams, comments;
            if (!read_lstr16(f.payload, off, name) ||
                !read_lstr16(f.payload, off, container) ||
                !read_lstr16(f.payload, off, procName) ||
                !read_lstr16(f.payload, off, inParams) ||
                !read_lstr16(f.payload, off, outParams) ||
                !read_lstr16(f.payload, off, comments)) {
                reply = err("DDCreateProc: bad payload"); break;
            }
            auto nameBuf = to_cbuf(name), contBuf = to_cbuf(container),
                 procBuf = to_cbuf(procName), inBuf = to_cbuf(inParams),
                 outBuf = to_cbuf(outParams), cmtBuf = to_cbuf(comments);
            UNSIGNED32 rc = AdsDDCreateProcedure(abi_conn_, nameBuf.data(),
                contBuf.data(), procBuf.data(), 0, inBuf.data(), outBuf.data(),
                cmtBuf.data());
            if (rc != 0) { reply = err("DDCreateProc", rc); break; }
            reply.opcode = Opcode::DDCreateProcAck;
            break;
        }
        case Opcode::DDCreateFunction: {
            if (!ensure_abi_conn()) { reply = err("DDCreateFunction: connect failed"); break; }
            std::size_t off = 0;
            std::string name, container, impl, retType, inParams, comment;
            if (!read_lstr16(f.payload, off, name) ||
                !read_lstr16(f.payload, off, container) ||
                !read_lstr16(f.payload, off, impl) ||
                !read_lstr16(f.payload, off, retType) ||
                !read_lstr16(f.payload, off, inParams) ||
                !read_lstr16(f.payload, off, comment)) {
                reply = err("DDCreateFunction: bad payload"); break;
            }
            auto nameBuf = to_cbuf(name), contBuf = to_cbuf(container),
                 implBuf = to_cbuf(impl), retBuf = to_cbuf(retType),
                 inBuf = to_cbuf(inParams), cmtBuf = to_cbuf(comment);
            UNSIGNED32 rc = AdsDDCreateFunction(abi_conn_, nameBuf.data(),
                contBuf.data(), implBuf.data(), retBuf.data(), inBuf.data(),
                cmtBuf.data());
            if (rc != 0) { reply = err("DDCreateFunction", rc); break; }
            reply.opcode = Opcode::DDCreateFunctionAck;
            break;
        }
        case Opcode::DDCreateTrigger: {
            if (!ensure_abi_conn()) { reply = err("DDCreateTrigger: connect failed"); break; }
            std::size_t off = 0;
            std::string name, table, container, procedure;
            if (!read_lstr16(f.payload, off, name) ||
                !read_lstr16(f.payload, off, table) ||
                off + 4 > f.payload.size()) {
                reply = err("DDCreateTrigger: bad payload"); break;
            }
            UNSIGNED32 type = read_u32_le(f.payload.data() + off);
            off += 4;
            if (!read_lstr16(f.payload, off, container) ||
                !read_lstr16(f.payload, off, procedure) ||
                off + 4 > f.payload.size()) {
                reply = err("DDCreateTrigger: bad payload"); break;
            }
            UNSIGNED32 priority = read_u32_le(f.payload.data() + off);
            auto nameBuf = to_cbuf(name), tblBuf = to_cbuf(table),
                 contBuf = to_cbuf(container), procBuf = to_cbuf(procedure);
            UNSIGNED32 rc = AdsDDCreateTrigger(abi_conn_, nameBuf.data(),
                tblBuf.data(), type, 0, contBuf.data(), procBuf.data(), priority);
            if (rc != 0) { reply = err("DDCreateTrigger", rc); break; }
            reply.opcode = Opcode::DDCreateTriggerAck;
            break;
        }
        case Opcode::DDDropTrigger: {
            if (!ensure_abi_conn()) { reply = err("DDDropTrigger: connect failed"); break; }
            std::size_t off = 0;
            std::string name;
            if (!read_lstr16(f.payload, off, name)) {
                reply = err("DDDropTrigger: bad payload"); break;
            }
            auto nameBuf = to_cbuf(name);
            UNSIGNED32 rc = AdsDDDropTrigger(abi_conn_, nameBuf.data());
            if (rc != 0) { reply = err("DDDropTrigger", rc); break; }
            reply.opcode = Opcode::DDDropTriggerAck;
            break;
        }
        case Opcode::DDDropView: {
            if (!ensure_abi_conn()) { reply = err("DDDropView: connect failed"); break; }
            std::size_t off = 0;
            std::string name;
            if (!read_lstr16(f.payload, off, name)) {
                reply = err("DDDropView: bad payload"); break;
            }
            auto nameBuf = to_cbuf(name);
            UNSIGNED32 rc = AdsDDDropView(abi_conn_, nameBuf.data());
            if (rc != 0) { reply = err("DDDropView", rc); break; }
            reply.opcode = Opcode::DDDropViewAck;
            break;
        }
        case Opcode::DDDropLink: {
            if (!ensure_abi_conn()) { reply = err("DDDropLink: connect failed"); break; }
            std::size_t off = 0;
            std::string name;
            if (!read_lstr16(f.payload, off, name)) {
                reply = err("DDDropLink: bad payload"); break;
            }
            auto nameBuf = to_cbuf(name);
            UNSIGNED32 rc = AdsDDDropLink(abi_conn_, nameBuf.data(), 0);
            if (rc != 0) { reply = err("DDDropLink", rc); break; }
            reply.opcode = Opcode::DDDropLinkAck;
            break;
        }
        // M12.30 — AdsDD* Data Dictionary property API, phase 2. See
        // docs/wire-protocol.md §5.25 / wire.h for the payload formats.
        case Opcode::DDCreateUser: {
            if (!ensure_abi_conn()) { reply = err("DDCreateUser: connect failed"); break; }
            std::size_t off = 0;
            std::string group, user, pwd, desc;
            if (!read_lstr16(f.payload, off, group) ||
                !read_lstr16(f.payload, off, user) ||
                !read_lstr16(f.payload, off, pwd) ||
                !read_lstr16(f.payload, off, desc)) {
                reply = err("DDCreateUser: bad payload"); break;
            }
            auto groupBuf = to_cbuf(group), userBuf = to_cbuf(user),
                 pwdBuf = to_cbuf(pwd), descBuf = to_cbuf(desc);
            UNSIGNED32 rc = AdsDDCreateUser(abi_conn_, groupBuf.data(),
                userBuf.data(), pwdBuf.data(), descBuf.data());
            if (rc != 0) { reply = err("DDCreateUser", rc); break; }
            reply.opcode = Opcode::DDCreateUserAck;
            break;
        }
        case Opcode::DDDropObject: {
            if (!ensure_abi_conn()) { reply = err("DDDropObject: connect failed"); break; }
            if (f.payload.empty()) { reply = err("DDDropObject: bad payload"); break; }
            auto kind = static_cast<DDObjectKind>(f.payload[0]);
            std::size_t off = 1;
            std::string name;
            if (!read_lstr16(f.payload, off, name)) {
                reply = err("DDDropObject: bad payload"); break;
            }
            auto nameBuf = to_cbuf(name);
            UNSIGNED32 rc = openads::AE_FUNCTION_NOT_AVAILABLE;
            switch (kind) {
                case DDObjectKind::User:
                    rc = AdsDDDeleteUser(abi_conn_, nameBuf.data()); break;
                case DDObjectKind::RefIntegrity:
                    rc = AdsDDRemoveRefIntegrity(abi_conn_, nameBuf.data()); break;
                case DDObjectKind::Proc:
                    rc = AdsDDDropProcedure(abi_conn_, nameBuf.data()); break;
                case DDObjectKind::Function:
                    rc = AdsDDDropFunction(abi_conn_, nameBuf.data()); break;
                default: break;
            }
            if (rc != 0) { reply = err("DDDropObject", rc); break; }
            reply.opcode = Opcode::DDDropObjectAck;
            break;
        }
        case Opcode::DDAddUserToGroup: {
            if (!ensure_abi_conn()) { reply = err("DDAddUserToGroup: connect failed"); break; }
            std::size_t off = 0;
            std::string group, user;
            if (!read_lstr16(f.payload, off, group) ||
                !read_lstr16(f.payload, off, user)) {
                reply = err("DDAddUserToGroup: bad payload"); break;
            }
            auto groupBuf = to_cbuf(group), userBuf = to_cbuf(user);
            UNSIGNED32 rc = AdsDDAddUserToGroup(abi_conn_, groupBuf.data(), userBuf.data());
            if (rc != 0) { reply = err("DDAddUserToGroup", rc); break; }
            reply.opcode = Opcode::DDAddUserToGroupAck;
            break;
        }
        case Opcode::DDRemoveUserFromGroup: {
            if (!ensure_abi_conn()) { reply = err("DDRemoveUserFromGroup: connect failed"); break; }
            std::size_t off = 0;
            std::string group, user;
            if (!read_lstr16(f.payload, off, group) ||
                !read_lstr16(f.payload, off, user)) {
                reply = err("DDRemoveUserFromGroup: bad payload"); break;
            }
            auto groupBuf = to_cbuf(group), userBuf = to_cbuf(user);
            UNSIGNED32 rc = AdsDDRemoveUserFromGroup(abi_conn_, groupBuf.data(), userBuf.data());
            if (rc != 0) { reply = err("DDRemoveUserFromGroup", rc); break; }
            reply.opcode = Opcode::DDRemoveUserFromGroupAck;
            break;
        }
        case Opcode::DDCreateLink: {
            if (!ensure_abi_conn()) { reply = err("DDCreateLink: connect failed"); break; }
            std::size_t off = 0;
            std::string alias, path, user, pwd;
            if (!read_lstr16(f.payload, off, alias) ||
                !read_lstr16(f.payload, off, path) ||
                !read_lstr16(f.payload, off, user) ||
                !read_lstr16(f.payload, off, pwd)) {
                reply = err("DDCreateLink: bad payload"); break;
            }
            auto aliasBuf = to_cbuf(alias), pathBuf = to_cbuf(path),
                 userBuf = to_cbuf(user), pwdBuf = to_cbuf(pwd);
            UNSIGNED32 rc = AdsDDCreateLink(abi_conn_, aliasBuf.data(), pathBuf.data(),
                                            userBuf.data(), pwdBuf.data(), 0);
            if (rc != 0) { reply = err("DDCreateLink", rc); break; }
            reply.opcode = Opcode::DDCreateLinkAck;
            break;
        }
        case Opcode::DDModifyLink: {
            if (!ensure_abi_conn()) { reply = err("DDModifyLink: connect failed"); break; }
            std::size_t off = 0;
            std::string alias, path, user, pwd;
            if (!read_lstr16(f.payload, off, alias) ||
                !read_lstr16(f.payload, off, path) ||
                !read_lstr16(f.payload, off, user) ||
                !read_lstr16(f.payload, off, pwd)) {
                reply = err("DDModifyLink: bad payload"); break;
            }
            auto aliasBuf = to_cbuf(alias), pathBuf = to_cbuf(path),
                 userBuf = to_cbuf(user), pwdBuf = to_cbuf(pwd);
            UNSIGNED32 rc = AdsDDModifyLink(abi_conn_, aliasBuf.data(), pathBuf.data(),
                                            userBuf.data(), pwdBuf.data(), 0);
            if (rc != 0) { reply = err("DDModifyLink", rc); break; }
            reply.opcode = Opcode::DDModifyLinkAck;
            break;
        }
        case Opcode::DDCreateRefIntegrity: {
            if (!ensure_abi_conn()) { reply = err("DDCreateRefIntegrity: connect failed"); break; }
            std::size_t off = 0;
            std::string name, failTbl, parent, parentTag, child, childTag;
            if (!read_lstr16(f.payload, off, name) ||
                !read_lstr16(f.payload, off, failTbl) ||
                !read_lstr16(f.payload, off, parent) ||
                !read_lstr16(f.payload, off, parentTag) ||
                !read_lstr16(f.payload, off, child) ||
                !read_lstr16(f.payload, off, childTag) ||
                off + 4 > f.payload.size()) {
                reply = err("DDCreateRefIntegrity: bad payload"); break;
            }
            UNSIGNED16 updateRule = read_u16_le(f.payload.data() + off); off += 2;
            UNSIGNED16 deleteRule = read_u16_le(f.payload.data() + off); off += 2;
            auto nameBuf = to_cbuf(name), failBuf = to_cbuf(failTbl),
                 parentBuf = to_cbuf(parent), parentTagBuf = to_cbuf(parentTag),
                 childBuf = to_cbuf(child), childTagBuf = to_cbuf(childTag);
            UNSIGNED32 rc = AdsDDCreateRefIntegrity(abi_conn_, nameBuf.data(),
                failBuf.data(), parentBuf.data(), parentTagBuf.data(),
                childBuf.data(), childTagBuf.data(), updateRule, deleteRule);
            if (rc != 0) { reply = err("DDCreateRefIntegrity", rc); break; }
            reply.opcode = Opcode::DDCreateRefIntegrityAck;
            break;
        }
        case Opcode::DDCreateView: {
            if (!ensure_abi_conn()) { reply = err("DDCreateView: connect failed"); break; }
            std::size_t off = 0;
            std::string name, comments;
            if (!read_lstr16(f.payload, off, name) ||
                !read_lstr16(f.payload, off, comments) ||
                off + 4 > f.payload.size()) {
                reply = err("DDCreateView: bad payload"); break;
            }
            std::uint32_t sqlLen = read_u32_le(f.payload.data() + off);
            off += 4;
            if (off + sqlLen > f.payload.size()) {
                reply = err("DDCreateView: bad payload"); break;
            }
            std::string sql(reinterpret_cast<const char*>(f.payload.data() + off), sqlLen);
            off += sqlLen;
            auto nameBuf = to_cbuf(name), commentsBuf = to_cbuf(comments),
                 sqlBuf = to_cbuf(sql);
            UNSIGNED32 rc = AdsDDCreateView(abi_conn_, nameBuf.data(),
                commentsBuf.data(), sqlBuf.data());
            if (rc != 0) { reply = err("DDCreateView", rc); break; }
            reply.opcode = Opcode::DDCreateViewAck;
            break;
        }
        case Opcode::DDAddIndexFile: {
            if (!ensure_abi_conn()) { reply = err("DDAddIndexFile: connect failed"); break; }
            std::size_t off = 0;
            std::string table, index, comment;
            if (!read_lstr16(f.payload, off, table) ||
                !read_lstr16(f.payload, off, index) ||
                !read_lstr16(f.payload, off, comment)) {
                reply = err("DDAddIndexFile: bad payload"); break;
            }
            auto tableBuf = to_cbuf(table), indexBuf = to_cbuf(index),
                 commentBuf = to_cbuf(comment);
            UNSIGNED32 rc = AdsDDAddIndexFile(abi_conn_, tableBuf.data(),
                indexBuf.data(), commentBuf.data());
            if (rc != 0) { reply = err("DDAddIndexFile", rc); break; }
            reply.opcode = Opcode::DDAddIndexFileAck;
            break;
        }
        case Opcode::DDRemoveIndexFile: {
            if (!ensure_abi_conn()) { reply = err("DDRemoveIndexFile: connect failed"); break; }
            std::size_t off = 0;
            std::string table, index;
            if (!read_lstr16(f.payload, off, table) ||
                !read_lstr16(f.payload, off, index)) {
                reply = err("DDRemoveIndexFile: bad payload"); break;
            }
            auto tableBuf = to_cbuf(table), indexBuf = to_cbuf(index);
            UNSIGNED32 rc = AdsDDRemoveIndexFile(abi_conn_, tableBuf.data(),
                indexBuf.data(), 0);
            if (rc != 0) { reply = err("DDRemoveIndexFile", rc); break; }
            reply.opcode = Opcode::DDRemoveIndexFileAck;
            break;
        }
        case Opcode::DDGetPermissions: {
            if (!ensure_abi_conn()) { reply = err("DDGetPermissions: connect failed"); break; }
            std::size_t off = 0;
            std::string grantee;
            if (!read_lstr16(f.payload, off, grantee) ||
                off + 2 > f.payload.size()) {
                reply = err("DDGetPermissions: bad payload"); break;
            }
            UNSIGNED16 objType = read_u16_le(f.payload.data() + off); off += 2;
            std::string objName;
            if (!read_lstr16(f.payload, off, objName) ||
                off + 1 > f.payload.size()) {
                reply = err("DDGetPermissions: bad payload"); break;
            }
            UNSIGNED16 getInherited = f.payload[off]; off += 1;
            auto granteeBuf = to_cbuf(grantee), objNameBuf = to_cbuf(objName);
            UNSIGNED32 permissions = 0;
            UNSIGNED32 rc = AdsDDGetPermissions(abi_conn_, granteeBuf.data(),
                objType, objNameBuf.data(), nullptr, getInherited, &permissions);
            if (rc != 0) { reply = err("DDGetPermissions", rc); break; }
            reply.opcode = Opcode::DDGetPermissionsAck;
            write_u32_le(permissions, reply.payload);
            break;
        }
        case Opcode::DDGrantPermission: {
            if (!ensure_abi_conn()) { reply = err("DDGrantPermission: connect failed"); break; }
            std::size_t off = 0;
            if (off + 2 > f.payload.size()) {
                reply = err("DDGrantPermission: bad payload"); break;
            }
            UNSIGNED16 objType = read_u16_le(f.payload.data() + off); off += 2;
            std::string objName, grantee;
            if (!read_lstr16(f.payload, off, objName) ||
                !read_lstr16(f.payload, off, grantee) ||
                off + 4 > f.payload.size()) {
                reply = err("DDGrantPermission: bad payload"); break;
            }
            std::uint32_t permissions = read_u32_le(f.payload.data() + off);
            auto objNameBuf = to_cbuf(objName), granteeBuf = to_cbuf(grantee);
            UNSIGNED32 rc = AdsDDGrantPermission(abi_conn_, objType, objNameBuf.data(),
                nullptr, granteeBuf.data(), permissions);
            if (rc != 0) { reply = err("DDGrantPermission", rc); break; }
            reply.opcode = Opcode::DDGrantPermissionAck;
            break;
        }
        case Opcode::Mutex: {
            if (f.payload.empty()) {
                reply = err("Mutex: empty payload"); break;
            }
            std::uint8_t sub_op = f.payload[0];
            std::size_t pos = 1;
            std::string name;
            if (!read_lstr16(f.payload, pos, name)) {
                reply = err("Mutex: short payload"); break;
            }
            // Session identifier: use connection serial as owner
            std::string owner = conn_serial();
            auto& mm = srv_->mutex_manager();
            switch (static_cast<MutexOp>(sub_op)) {
                case MutexOp::Create: {
                    bool ok = mm.create(name);
                    reply.opcode = Opcode::Mutex;
                    reply.payload = { sub_op, static_cast<std::uint8_t>(ok ? 1 : 0) };
                    break;
                }
                case MutexOp::Lock: {
                    std::uint32_t timeout_ms = 0;
                    if (pos + 4 <= f.payload.size()) {
                        timeout_ms = read_u32_le(f.payload.data() + pos);
                    }
                    bool ok = mm.lock(name, timeout_ms, owner);
                    reply.opcode = Opcode::Mutex;
                    reply.payload = { sub_op, static_cast<std::uint8_t>(ok ? 1 : 0) };
                    break;
                }
                case MutexOp::TryLock: {
                    bool ok = mm.try_lock(name, owner);
                    reply.opcode = Opcode::Mutex;
                    reply.payload = { sub_op, static_cast<std::uint8_t>(ok ? 1 : 0) };
                    break;
                }
                case MutexOp::Unlock: {
                    bool ok = mm.unlock(name, owner);
                    reply.opcode = Opcode::Mutex;
                    reply.payload = { sub_op, static_cast<std::uint8_t>(ok ? 1 : 0) };
                    break;
                }
                case MutexOp::Destroy: {
                    bool ok = mm.destroy(name, owner);
                    reply.opcode = Opcode::Mutex;
                    reply.payload = { sub_op, static_cast<std::uint8_t>(ok ? 1 : 0) };
                    break;
                }
                default:
                    reply = err("Mutex: unknown sub-opcode");
                    break;
            }
            break;
        }
        default: {
            reply = err("unsupported opcode");
            break;
        }
    }
    return { std::move(reply), false };
}

// Pritpal Bedi 29/07/2026 — a remote OrdCreate / AdsCreateIndex61 ignored
// the folder in the index bag name: "cFolder/Indexes/foo.Z01" always landed
// next to the .DBF, because the client used to strip every bag name to its
// basename before sending. The client now sends the path verbatim and the
// SERVER decides where the index goes, mirroring the rules
// Connection::resolve_table_file applies to the .DBF itself:
//   - bare filename              -> returned unchanged; AdsCreateIndex61
//                                   falls back to the table's own folder
//   - relative with directories  -> anchored at the connection data root
//   - absolute inside data root  -> honored verbatim
//   - absolute outside data root -> drive/root folded, remainder joined
//                                   under the data root
// A jail escape (".." outside the root) degrades to the bare filename.
std::string Session::resolve_index_bag_path(const std::string& bag) const {
    namespace fs = std::filesystem;
    if (!sess_conn_ || sess_conn_->data_dir().empty()) return bag;
    std::string s = bag;
    for (char& ch : s) if (ch == '\\') ch = '/';
    if (s.find('/') == std::string::npos) return bag;   // bare filename
    // Same remount as the .DBF: --legacy-paths always folds a client-
    // absolute bag under --data (C:/Creative.RAM/T.Z01 →
    // <jail>/Creative.RAM/T.Z01). Honouring a host-absolute path that
    // merely existed next to the app is what put .z01 on the local
    // tree while the table updated in the jail (Pritpal Bedi, 12/08/2026).
    auto type = openads::engine::TableType::Cdx;
    std::string resolved =
        sess_conn_->resolve_table_file(s, type, /*for_create=*/true);
    std::error_code ec;
    fs::create_directories(fs::path(resolved).parent_path(), ec);
    return resolved;
}

std::optional<std::string> Session::resolve_fs_client_path(
    const std::string& client_path) const {
    if (!sess_conn_) return std::nullopt;
    // Prefer the session connection data directory (already jailed at
    // Connect time). Fall back to server multi-root list.
    // --legacy-paths: use the prefix-stripping resolver so absolute
    // client paths ("C:/TEMP/...") map onto the root instead of
    // folding to "<root>/TEMP/...".
    if (!sess_conn_->data_dir().empty()) {
        if (srv_ && srv_->legacy_paths()) {
            return openads::platform::resolve_client_path(
                {sess_conn_->data_dir()}, client_path);
        }
        return openads::platform::resolve_fs_path(sess_conn_->data_dir(),
                                                    client_path);
    }
    if (srv_ && !srv_->data_dir_.empty()) {
        auto roots = openads::platform::split_data_roots(srv_->data_dir_);
        if (srv_->legacy_paths()) {
            return openads::platform::resolve_client_path(roots, client_path);
        }
        return openads::platform::resolve_fs_path(roots, client_path);
    }
    return std::nullopt;
}

} // namespace openads::network
