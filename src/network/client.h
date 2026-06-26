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

    util::Result<std::uint32_t> open_table(const std::string& rel);
    util::Result<void>          close_table(std::uint32_t id);
    util::Result<void>          goto_top(std::uint32_t id);
    util::Result<void>          goto_top(RemoteTable* rt);
    util::Result<void>          skip(std::uint32_t id, std::int32_t step);
    util::Result<void>          skip(RemoteTable* rt, std::int32_t step);
    util::Result<std::string>   get_field(std::uint32_t id,
                                           const std::string& field_name);
    util::Result<std::uint32_t> record_count(std::uint32_t id);
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
    util::Result<std::uint16_t> get_aof_opt_level(std::uint32_t id);
    // M12.16 — remote index handle subsystem.
    util::Result<std::vector<std::uint32_t>>
                                open_index(std::uint32_t table_id,
                                           const std::string& path);
    util::Result<void>          close_index(std::uint32_t index_id);
    util::Result<void>          set_order(std::uint32_t table_id,
                                          std::uint32_t index_id);
    util::Result<void>          set_order_by_name(std::uint32_t table_id,
                                                   const std::string& tag);
    struct SeekOutcome {
        std::uint8_t  hit  = 0;     // 1 = exact, 0 = soft / not found
        std::uint32_t recno = 0;
    };
    util::Result<SeekOutcome>   seek(std::uint32_t index_id,
                                     const std::string& key,
                                     std::uint8_t soft,
                                     std::uint8_t last);
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
    // M12.21 option C — rows popped from prefetch_queue locally since the
    // last server round-trip. The server cursor lags the client's logical
    // position by exactly this count, so the next wire Skip sends
    // (step + prefetch_consumed) to resync. Reset on every nav ack.
    std::uint32_t prefetch_consumed = 0;
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
};

// M12.16 — per-handle wrapper for a remote index. Each tag
// returned by AdsOpenIndex (the multi-tag CDX case) gets one of
// these so the ABI surface can hand the host a real ADSHANDLE.
struct RemoteIndex {
    RemoteConnection* conn  = nullptr;
    std::uint32_t     id    = 0;     // server-side index id
    std::uint32_t     tbl_id = 0;    // server-side table id this binds to
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
