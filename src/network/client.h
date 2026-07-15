#pragma once

#include "network/server.h"
#include "network/socket.h"
#include "network/transport.h"
#include "network/wire.h"
#include "engine/aggregate.h"
#include "util/result.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace openads::network {

// Result type for RemoteConnection::fetch_where. `rows` carries the
// matched column values (row-major); `recnos` is populated iff
// FetchWhereFlags::WANT_RECNO was set in the request flags (one entry
// per row, same order as rows). `eof` is true when the server walked
// to the end of the table during this call.
struct FetchWhereBatch {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::uint32_t>            recnos;   // 1 per row iff WANT_RECNO
    bool                                  eof = false;
};

// One requested aggregate (function + column; empty field = COUNT(*)).
// Defined in engine/aggregate.h so the SQL-backend push-down (abi layer)
// and the wire client share one type.
using AggSpec = engine::AggSpec;

// Result of RemoteConnection::aggregate — one scalar per requested AggSpec,
// in the same order.
struct AggregateBatch {
    std::vector<engine::AggValue> values;
};

struct RemoteTable;

// M12.5 — wire client used by ace64.dll's dual-mode dispatch.
// `RemoteConnection` opens a TCP socket to an OpenADS server,
// sends a Connect frame for the data_dir, and exposes a small
// surface mirroring the local Connection methods that the
// remote-routed Ads* functions need.

class RemoteConnection {
public:
    RemoteConnection() = default;
    ~RemoteConnection() { disconnect(); }
    RemoteConnection(const RemoteConnection&) = delete;
    RemoteConnection& operator=(const RemoteConnection&) = delete;

    util::Result<void> connect(const std::string& host,
                               std::uint16_t port,
                               const std::string& data_dir,
                               const std::string& user     = "",
                               const std::string& password = "");

    // M12.12 — connect using a pre-built transport (e.g. a
    // TlsTransport built by `connect_tls`). The Connect frame is
    // sent through the supplied transport; ownership passes to
    // RemoteConnection.
    util::Result<void>
        connect_with_transport(std::unique_ptr<ITransport> transport,
                               const std::string& data_dir,
                               const std::string& user     = "",
                               const std::string& password = "");

    void               disconnect() noexcept;
    bool               valid() const noexcept {
        return transport_ && transport_->valid();
    }

    // M12.16 — remote index handle subsystem.
    struct OpenIndexEntry {
        std::uint32_t id = 0;
        std::string   tag;
        std::string   bag_path;  // production CDX/NTX/ADI filename
        std::string   expression;
        bool          is_unique = false;
        bool          is_descending = false;
    };
    // M-AOF.6 — extended OpenTableAck carries production bag path.
    struct OpenTableResult {
        std::uint32_t id = 0;
        std::string   prod_bag_path;  // production CDX/ADI filename (if found)
    };
    util::Result<OpenTableResult> open_table(const std::string& rel);
    util::Result<void>          close_table(std::uint32_t id);
    util::Result<void>          goto_top(std::uint32_t id);
    util::Result<void>          goto_top(RemoteTable* rt);
    util::Result<void>          skip(std::uint32_t id, std::int32_t step);
    util::Result<void>          skip(RemoteTable* rt, std::int32_t step);
    util::Result<std::string>   get_field(std::uint32_t id,
                                           const std::string& field_name);
    util::Result<std::uint32_t> record_count(std::uint32_t id);
    util::Result<std::uint32_t> key_count(std::uint32_t table_id);
    util::Result<bool>          at_eof(std::uint32_t id);
    // M12.14 — remote field metadata + extended cursor state.
    struct FieldDesc {
        std::string   name;
        std::uint16_t type     = 0;     // ADS_* code
        std::uint32_t length   = 0;
        std::uint16_t decimals = 0;
    };
    util::Result<std::vector<FieldDesc>>
                                describe_table(std::uint32_t id);
    util::Result<bool>          at_bof(std::uint32_t id);
    util::Result<std::uint32_t> get_record_num(std::uint32_t id);
    util::Result<bool>          is_record_deleted(std::uint32_t id);
    util::Result<void>          goto_bottom(std::uint32_t id);
    // M12.15 — remote info / lock / maintenance / AOF.
    util::Result<bool>          is_found(std::uint32_t id);
    util::Result<void>          refresh_record(std::uint32_t id);
    util::Result<std::uint16_t> get_table_type(std::uint32_t id);
    util::Result<std::uint32_t> get_record_length(std::uint32_t id);
    util::Result<std::uint16_t> get_num_indexes(std::uint32_t id);
    util::Result<std::uint32_t> get_last_autoinc(std::uint32_t id);
    // DBF header "last updated" date, packed (y<<16)|(m<<8)|d.
    util::Result<std::uint32_t> get_last_table_update(std::uint32_t id);
    util::Result<void>          lock_record(std::uint32_t id, std::uint32_t recno);
    util::Result<void>          unlock_record(std::uint32_t id, std::uint32_t recno);
    util::Result<void>          lock_table(std::uint32_t id);
    util::Result<void>          unlock_table(std::uint32_t id);
    util::Result<void>          pack_table(std::uint32_t id);
    util::Result<void>          zap_table(std::uint32_t id);
    util::Result<void>          flush_file_buffers(std::uint32_t id);
    util::Result<void>          close_all_indexes(std::uint32_t id);
    util::Result<void>          set_aof(std::uint32_t id, const std::string& cond);
    util::Result<void>          clear_aof(std::uint32_t id);
    util::Result<void>          customize_aof(std::uint32_t id,
                                              std::uint16_t option,
                                              const std::vector<std::uint32_t>& recnos);
    util::Result<std::uint16_t> get_aof_opt_level(std::uint32_t id);
    util::Result<std::vector<OpenIndexEntry>>
                                open_index(std::uint32_t table_id,
                                           const std::string& path);
    util::Result<void>          close_index(std::uint32_t index_id);

    // M12.29 — AdsDD* Data Dictionary property API, phase 1. `subName` is
    // only meaningful for DDObjectKind::Field (the field name within the
    // table named by `name`); pass "" for every other kind. See
    // docs/wire-protocol.md §9 and wire.h for the wire payload formats.
    util::Result<std::string>  dd_get_property(DDObjectKind kind,
                                               const std::string& name,
                                               const std::string& subName,
                                               std::uint16_t propId);
    util::Result<void>          dd_set_property(DDObjectKind kind,
                                                const std::string& name,
                                                const std::string& subName,
                                                std::uint16_t propId,
                                                const std::string& value);
    util::Result<void>          dd_create_proc(const std::string& name,
                                               const std::string& container,
                                               const std::string& procName,
                                               const std::string& inParams,
                                               const std::string& outParams,
                                               const std::string& comments);
    util::Result<void>          dd_create_function(const std::string& name,
                                                    const std::string& container,
                                                    const std::string& implementation,
                                                    const std::string& retType,
                                                    const std::string& inParams,
                                                    const std::string& comment);
    util::Result<void>          dd_create_trigger(const std::string& name,
                                                   const std::string& table,
                                                   std::uint32_t type,
                                                   const std::string& container,
                                                   const std::string& procedure,
                                                   std::uint32_t priority);
    util::Result<void>          dd_drop_trigger(const std::string& name);
    util::Result<void>          dd_drop_view(const std::string& name);
    util::Result<void>          dd_drop_link(const std::string& name);

    // M12.30 — AdsDD* Data Dictionary property API, phase 2 (deferred
    // user/group/link/RI/view/index-file/permissions calls). See
    // docs/wire-protocol.md §5.25 and wire.h for the wire payload formats.
    util::Result<void>          dd_create_user(const std::string& group,
                                               const std::string& user,
                                               const std::string& pwd,
                                               const std::string& desc);
    // `kind` must be User, RefIntegrity, Proc, or Function — the four
    // plain "drop by name" calls not already covered by a phase-1 opcode.
    util::Result<void>          dd_drop_object(DDObjectKind kind,
                                               const std::string& name);
    util::Result<void>          dd_add_user_to_group(const std::string& group,
                                                      const std::string& user);
    util::Result<void>          dd_remove_user_from_group(const std::string& group,
                                                           const std::string& user);
    util::Result<void>          dd_create_link(const std::string& alias,
                                               const std::string& path,
                                               const std::string& user,
                                               const std::string& pwd);
    util::Result<void>          dd_modify_link(const std::string& alias,
                                               const std::string& path,
                                               const std::string& user,
                                               const std::string& pwd);
    util::Result<void>          dd_create_ref_integrity(const std::string& name,
                                                         const std::string& failTable,
                                                         const std::string& parent,
                                                         const std::string& parentTag,
                                                         const std::string& child,
                                                         const std::string& childTag,
                                                         std::uint16_t updateRule,
                                                         std::uint16_t deleteRule);
    util::Result<void>          dd_create_view(const std::string& name,
                                               const std::string& comments,
                                               const std::string& sql);
    util::Result<void>          dd_add_index_file(const std::string& table,
                                                   const std::string& index,
                                                   const std::string& comment);
    util::Result<void>          dd_remove_index_file(const std::string& table,
                                                      const std::string& index);
    util::Result<std::uint32_t> dd_get_permissions(const std::string& grantee,
                                                    std::uint16_t objType,
                                                    const std::string& objName,
                                                    bool getInherited);
    util::Result<void>          dd_grant_permission(std::uint16_t objType,
                                                     const std::string& objName,
                                                     const std::string& grantee,
                                                     std::uint32_t permissions);

    util::Result<void>          set_order(std::uint32_t table_id,
                                          std::uint32_t index_id);
    util::Result<void>          set_order_by_name(std::uint32_t table_id,
                                                   const std::string& tag);
    struct SeekOutcome {
        std::uint8_t  hit  = 0;     // 1 = exact, 0 = soft / not found
        std::uint32_t recno = 0;
    };
    // RCB 07/14/2026: `parent` is optional but you almost always want to pass
    // it — an M12.24 server returns the row it landed on in the SeekAck, and
    // that is the only way to receive it. Pass nullptr and the row cache stays
    // invalid, which costs a FetchCurrentRow round-trip on the next field read.
    util::Result<SeekOutcome>   seek(std::uint32_t index_id,
                                     const std::string& key,
                                     std::uint8_t soft,
                                     std::uint8_t last,
                                     RemoteTable* parent = nullptr);
    util::Result<std::uint32_t> create_index(std::uint32_t table_id,
                                              const std::string& path,
                                              const std::string& tag,
                                              const std::string& expr,
                                              const std::string& cond,
                                              const std::string& key_filter,
                                              std::uint32_t options,
                                              std::uint16_t page_size);
    util::Result<void>          skip_unique(std::uint32_t index_id,
                                            std::int32_t  direction);
    util::Result<void>          set_scope(std::uint32_t index_id,
                                          std::uint16_t which,
                                          const std::string& key,
                                          std::uint16_t data_type);
    util::Result<void>          clear_scope(std::uint32_t index_id,
                                            std::uint16_t which);
    // M12.31 — propagate SET DELETED ON/OFF to the server session.
    void                        show_deleted(bool visible) noexcept;
    // M12.17 — single-frame whole-record read.
    struct RowSnapshot {
        bool                     has_row = false;
        std::vector<std::string> fields;     // order matches FieldDesc cache
    };
    util::Result<RowSnapshot>   fetch_current_row(std::uint32_t table_id);
    util::Result<void>          fetch_current_row(RemoteTable* rt);
    // M12.6 — remote write surface.
    util::Result<void>          append_blank(std::uint32_t id);
    util::Result<void>          set_field(std::uint32_t id,
                                          const std::string& field_name,
                                          const std::string& value);
    // M12.25 — raw DBF record image at the current cursor position.
    util::Result<std::vector<std::uint8_t>> get_record(std::uint32_t id);
    util::Result<std::uint32_t> get_record_crc(std::uint32_t id);
    util::Result<void>          set_record(std::uint32_t id,
                                           const std::uint8_t* bytes,
                                           std::size_t len);
    util::Result<void>          delete_record(std::uint32_t id);
    util::Result<void>          recall_record(std::uint32_t id);
    util::Result<void>          goto_record(std::uint32_t id,
                                            std::uint32_t recno);
    util::Result<void>          goto_record(RemoteTable* rt,
                                            std::uint32_t recno);
    util::Result<void>          goto_bottom(RemoteTable* rt);
    util::Result<void>          flush_table(std::uint32_t id);
    // M12.7 — remote SQL exec. Returns cursor table-id (0 = no cursor,
    // i.e. INSERT / UPDATE / DELETE / DDL).
    util::Result<std::uint32_t> execute_sql(const std::string& sql);
    // M12.8 — remote index ops.
    util::Result<void>          reindex(std::uint32_t id);
    // M12.11 — batch row read. Walks up to `max_rows` rows starting
    // at the current cursor position, returning a row-major matrix of
    // column values. Empty result set ⇒ empty outer vector. Reduces
    // per-row Skip/GetField round-trips for WAN-latency callers.
    util::Result<std::vector<std::vector<std::string>>>
        fetch_batch(std::uint32_t                   id,
                    std::uint32_t                   max_rows,
                    const std::vector<std::string>& columns);
    // Tier-2 server-side filtered scan. Like fetch_batch, but the
    // server evaluates `where_expr` (a Clipper-style FOR predicate,
    // e.g. "AGE > 40 .AND. CITY = 'RIO'") against each row and returns
    // only the matching rows' requested columns, walking the table
    // server-side until `max_rows` matches or EOF. Collapses a
    // non-index-optimisable SET FILTER / COUNT FOR / LOCATE scan from
    // one round-trip per record to ceil(matches / max_rows). Base
    // tables only — a SQL cursor already filters via its WHERE clause.
    // Callers continue the walk like fetch_batch: a batch returning
    // fewer than `max_rows` rows has reached EOF.
    // `flags`: FetchWhereFlags::WANT_RECNO (0x01) causes the server to
    // include each matching row's recno in the reply; the recnos are
    // stored in FetchWhereBatch::recnos (same order as rows). flags=0
    // (default) is byte-identical to the v1.4.0 request/reply.
    util::Result<FetchWhereBatch>
        fetch_where(std::uint32_t                   id,
                    std::uint32_t                   max_rows,
                    const std::string&              where_expr,
                    const std::vector<std::string>& columns,
                    std::uint8_t                    flags = 0);

    // Tier-3 — server-side aggregation. The server scans the whole table
    // once, evaluates `for_expr` per row, and folds each match into the
    // requested accumulators, returning one scalar per spec (same order).
    // Base tables only — a SQL cursor already aggregates via SQL.
    util::Result<AggregateBatch>
        aggregate(std::uint32_t               id,
                  const std::string&          for_expr,
                  const std::vector<AggSpec>& specs);

private:
    util::Result<Frame> request(const Frame& f);

    std::unique_ptr<ITransport> transport_;
    std::mutex                  mu_;
};

// Per-handle wrapper for a remote table. Stores back-pointer to
// the connection plus the server-side table id.
struct RemoteTable {
    RemoteConnection* conn = nullptr;
    std::uint32_t     id   = 0;
    // The table name as passed to open_table (with extension). Served
    // by AdsGetTableFilename so the consuming RDD has something to show.
    std::string       name;
    // M12.14 — schema cache populated lazily on first
    // AdsGetNumFields / AdsGetFieldName / ... call so rddads'
    // adsOpen field-iteration loop stays at one wire round-trip.
    std::vector<RemoteConnection::FieldDesc> fields;
    bool fields_cached = false;
    // M12.17 — current-record buffer cache. Populated lazily on the
    // first AdsGetField after a nav op; invalidated by AdsSkip /
    // AdsGotoTop / AdsGotoBottom / AdsGotoRecord / AdsAppendRecord
    // / AdsWriteRecord / AdsDeleteRecord / AdsRecallRecord. xbrowse
    // re-paints therefore cost one extra RTT per row (the fetch),
    // not one per cell.
    std::vector<std::string> current_row;
    bool                     row_valid = false;
    // M12.18 — recno + deleted flag arrive together with the row
    // bytes so AdsGetRecordNum / AdsIsRecordDeleted can serve from
    // cache instead of a separate RTT each.
    std::uint32_t            current_recno   = 0;
    bool                     current_deleted = false;
    // Logical 1-based key position within the active index order.
    // Updated on remote nav when active_index_id != 0 so AdsGetKeyNum
    // (FWH xBrowse scrollbar) returns a non-zero position over TCP.
    std::uint32_t            current_keyno   = 0;
    bool                     keyno_valid     = false;
    // Set when a backward Skip cannot move (top of order). Cleared on
    // forward nav / GoTop. Without this, AdsAtBOF always answers "not
    // BOF" while row_valid and xBrowse GoUp rubber-bands at key #1.
    bool                     nav_at_bof      = false;
    // Set when a forward Skip cannot move (bottom of order).
    bool                     nav_at_eof      = false;
    // M12.19 — cached record count. Serves AdsGetRecordCount and
    // AdsGetRelKeyPos (scrollbar) without an extra RTT. Invalidated
    // on writes that may change the row count: AppendBlank /
    // DeleteRecord / RecallRecord / Pack / Zap.
    std::uint32_t            cached_rec_count   = 0;
    bool                     rec_count_cached   = false;
    // M12.21 — sequential prefetch queue. SkipAck (when step==1)
    // returns the row at +1 plus up to N lookahead rows; the
    // bridge pops one entry per Skip(1) call so a 20-row PgDn
    // costs 1 RTT total, not 20. Cleared by any non-sequential
    // nav or write so the queue can never serve a stale row.
    struct PrefetchedRow {
        std::uint32_t            recno   = 0;
        bool                     deleted = false;
        std::vector<std::string> fields;
    };
    std::deque<PrefetchedRow> prefetch_queue;
    // RCB 07/15/2026: M12.25 — the direction the queued rows are walked in:
    // +1 = forward (each row is the next in a Skip(+1) scan), -1 = backward
    // (PgUp), 0 = empty/none. A drain only fires when the caller's step sign
    // matches; a direction reversal can't serve from the queue and goes to the
    // wire (which refills the queue in the new direction). Set on each nav ack
    // from the sign of the wire step.
    std::int8_t prefetch_dir = 0;
    // RCB 07/15/2026: M12.25 — was `prefetch_consumed` (unsigned, forward-only).
    // Now SIGNED: cursor_lag = client's logical position MINUS the server's
    // cursor position. A forward local drain moves the client ahead of the
    // server (lag += 1); a backward local drain moves it behind (lag -= 1). The
    // next wire Skip sends (step + cursor_lag) so the server lands at
    // client_logical + step regardless of sign. Reset to 0 on every nav ack
    // (the ack re-anchors the server to the client's logical row). The forward
    // path is byte-for-byte unchanged: lag was always >= 0 there, and
    // step + lag == the old step + prefetch_consumed.
    std::int32_t cursor_lag = 0;
    // M12.21 option C — cached Found() state. xBase clears Found() on any
    // non-seek move (Skip/Goto) and sets it from the seek outcome, so the
    // ops that know the value set it here and AdsIsFound serves it with no
    // round-trip (rddads polls IsFound after every DbSkip). found_cached
    // gates the fast path: when false, AdsIsFound asks the server.
    bool found_cached  = false;
    bool current_found = false;
    // Filter / AOF expression strings (stored for AdsGetFilter / AdsGetAOF).
    std::string filter_expr;
    std::string aof_expr;
    // Table alias (stored for AdsGetTableAlias).
    std::string alias;
    // Server-side wire id of the active controlling index (0 = natural order).
    // Updated by AdsSetIndexOrder / AdsSetIndexOrderByHandle / AdsOpenIndex.
    std::uint32_t active_index_id = 0;
    // RCB 07/14/2026: the order the SERVER actually has installed, as last
    // confirmed by a SetOrder ack. Why this exists as a SECOND field instead
    // of just reusing active_index_id: active_index_id is only the client's
    // *belief*, and it can be set with no round-trip at all (the production-bag
    // auto-open in AdsOpenTable100, or AdsGetIndexHandle resolving a tag). That
    // asymmetry is exactly why remote_activate_index used to re-send SetOrder
    // before every single nav op — trusting active_index_id left
    // ordered_tables_ empty on the server, so GotoTop/Skip walked the engine
    // table in natural order and ignored any scope. Somebody hit that, and the
    // fix was to give up and always send the frame.
    //
    // Keying the skip on a value that ONLY a real ack can write closes the hole
    // properly, which is what makes it safe to send SetOrder once per order
    // change instead of once per row. rddads navigates on hOrdCurrent whenever
    // an order is set, so that was costing a whole round-trip on every single
    // row of every ordered browse.
    static constexpr std::uint32_t kOrderUnknown = 0xFFFFFFFFu;
    std::uint32_t server_order_id = kOrderUnknown;

    // RCB 07/14/2026: M12.23 — AdsCacheRecords(hTable, N). The depth the CALLER
    // asked for, sent on each Skip request. kPrefetchDepthAuto (the default)
    // means the app never called AdsCacheRecords, so the server picks the depth
    // itself by ramping on detected sequential access.
    //
    // Why honour an explicit value at all, when the server's ramp is usually
    // smarter than a fixed number: because the app can know things the access
    // pattern cannot reveal. "I am about to edit most of the records I visit"
    // looks identical to a plain scan until the writes start landing — and by
    // then we have already shipped blocks that each write throws away. SAP
    // documents 0/1 as the remedy for exactly that, and it needs a way to reach
    // us.
    std::uint16_t cache_records_hint = kPrefetchDepthAuto;

    // RCB 07/14/2026: drop the read-ahead block AND the consumed-lag counter.
    // Every op that moves the server cursor outside the sequential-skip path
    // (Seek, order change, ...) must call this. Missing it is not a perf bug,
    // it is a WRONG-DATA bug, and we shipped three of them: the queued rows
    // were read at the *old* position/in the *old* order, so a following
    // Skip(1) would pop a stale row with no wire traffic at all (nothing on the
    // network to make it look suspicious), and the next real skip would then
    // send a step inflated by a lag that no longer applies.
    void invalidate_prefetch() {
        prefetch_queue.clear();
        prefetch_dir = 0;
        cursor_lag   = 0;
    }
    // Tag name → server wire index id (populated at AdsOpenIndex).
    std::vector<std::pair<std::string, std::uint32_t>> index_by_tag;
    // Parallel to index_by_tag: the ABI ADSHANDLE (registry handle, 64-bit)
    // wrapping each opened index. Lets AdsGetIndexHandle (by tag) and
    // AdsGetIndexHandleByOrder (by ordinal) resolve to a usable remote
    // index handle over the wire — the path rddads' DbSetOrder() takes.
    std::vector<std::uint64_t> index_handles;
    // Production index bag path (e.g. "customers.cdx"). Populated from the
    // OpenTableAck auto-open or AdsOpenIndex. Lets AdsGetIndexFilename
    // (OrdBagName) return the bag name without a separate wire round-trip.
    std::string prod_bag_path;
};

// M12.16 — per-handle wrapper for a remote index. Each tag
// returned by AdsOpenIndex (the multi-tag CDX case) gets one of
// these so the ABI surface can hand the host a real ADSHANDLE.
struct RemoteIndex {
    RemoteConnection* conn  = nullptr;
    std::uint32_t     id    = 0;     // server-side index id
    std::uint32_t     tbl_id = 0;    // server-side table id this binds to
    std::string       tag_name;      // CDX/NTX tag (AdsGetIndexName / OrdName)
    std::string       bag_path;      // production CDX/NTX/ADI filename (AdsGetIndexFilename / OrdBagName)
    std::string       expression;    // key expression (AdsGetIndexExpr)
    bool              is_unique = false;      // AdsIsIndexUnique
    bool              is_descending = false;  // AdsIsIndexDescending (physical flag)
    // M12.17 — back-pointer so AdsSeek / AdsSeekLast / AdsSkipUnique
    // can invalidate the parent table's row cache after the cursor
    // moves on the server side.
    RemoteTable*      parent = nullptr;
};

// Parse `tcp://host:port/<data_dir>` into its parts. Returns
// false when the input doesn't carry the tcp:// prefix.
bool parse_tcp_uri(const std::string& uri,
                   std::string& host,
                   std::uint16_t& port,
                   std::string& data_dir);

// M12.12 — TLS URI scheme `tls://host:port/<data_dir>` is reserved
// but not yet implemented. parse_tls_uri only returns true when the
// input carries the tls:// prefix; AdsConnect60 surfaces
// AE_FUNCTION_NOT_AVAILABLE so apps get a clear failure instead of
// a silent plaintext fallback. Real TLS (vendored mbedtls /
// platform-native) lands in v0.4.0.
bool parse_tls_uri(const std::string& uri,
                   std::string& host,
                   std::uint16_t& port,
                   std::string& data_dir);

} // namespace openads::network
