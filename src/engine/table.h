#pragma once

#include "drivers/driver_trait.h"
#include "drivers/dbf_common.h"
#include "drivers/index_trait.h"
#include "drivers/memo_trait.h"
#include "engine/lock_mgr.h"
#include "engine/order.h"
#include "engine/tx.h"
#include "util/result.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openads::session { class Connection; }

namespace openads::engine {

bool show_deleted() noexcept;
void set_show_deleted(bool v) noexcept;

bool set_exact() noexcept;
void set_set_exact(bool v) noexcept;

std::uint16_t epoch() noexcept;
void set_epoch(std::uint16_t v) noexcept;


enum class TableType { Cdx, Ntx, Adt, Vfp };
enum class OpenMode  { Read, Shared, Exclusive };

class Table {
public:
    Table() = default;
    Table(const Table&) = delete;
    Table& operator=(const Table&) = delete;
    Table(Table&&) noexcept = default;
    Table& operator=(Table&&) noexcept = default;
    ~Table() = default;

    static util::Result<Table> open(const std::string& path,
                                    TableType type,
                                    OpenMode mode = OpenMode::Read,
                                    LockingMode locking = LockingMode::Compatible);
    static Table from_driver(std::unique_ptr<drivers::IDriver> drv,
                             std::string path,
                             TableType type = TableType::Adt,
                             OpenMode mode = OpenMode::Read,
                             LockingMode locking = LockingMode::Compatible);

    const std::string& path() const noexcept { return path_; }
    const std::string& alias() const noexcept { return alias_; }
    void set_alias(std::string a) noexcept { alias_ = std::move(a); }

    void set_owner(openads::session::Connection* c) noexcept { owner_ = c; }
    openads::session::Connection* owner() const noexcept { return owner_; }

    // RCB 2026-07-10 — per-table character type, straight from the ACE
    // usCharType open parameter (1 = ADS_ANSI, 2 = ADS_OEM; kept as raw
    // ints so the engine layer stays free of ace.h). This is how a
    // Harbour rddads app declares OEM data (AdsSetCharType(ADS_OEM) →
    // usCharType on every AdsOpenTable/AdsCreateTable/AdsCreateIndex) —
    // it never calls AdsSetCollation. The effective-collation accessors
    // below consult it; do not remove (#130 follow-up, PL852 seeks).
    std::uint16_t char_type() const noexcept { return char_type_; }
    void set_char_type(std::uint16_t ct) noexcept { char_type_ = ct; }

    // RCB 2026-07-10 — EFFECTIVE OEM tables for this table, resolved as:
    // connection AdsSetCollation override → default OEM collation when
    // char_type()==ADS_OEM (OPENADS_OEM_COLLATION / future adslocal.cfg)
    // → process-global legacy fallback (upper only). Callers must use
    // these instead of reaching for the Connection directly, otherwise
    // ADS_OEM tables silently lose their collation again.
    const std::uint8_t* oem_upper_table() const noexcept;
    const std::uint8_t* oem_sort_table() const noexcept;
    bool show_deleted_records() const noexcept;

    OpenMode  open_mode()  const noexcept { return mode_; }
    LockingMode locking_mode() const noexcept { return locking_; }

    bool is_table_locked() const noexcept {
        return table_lock_.has_value();
    }
    std::uint16_t lock_count() const noexcept {
        return static_cast<std::uint16_t>(recno_locks_.size());
    }

    // RI old-PK snapshot captured at navigation time and compared against
    // the dirty buffer in ri_enforce_update to decide cascade/restrict.
    // Held on the Table itself (not a global Table*-keyed map) so it is
    // freed with the table and can never alias a reused heap address —
    // the source of intermittent missed cascades/restrictions.
    std::unordered_map<std::string, std::string>& ri_snapshot() noexcept {
        return ri_snapshot_;
    }

    // Set between AdsAppendRecord and the matching AdsWriteRecord so the
    // write knows it is an INSERT (RI insert-check) rather than an UPDATE
    // (RI cascade/restrict). Held on the Table — not a global Table*-keyed
    // set — so a freed-then-reused heap address can't make a plain update
    // look like an append and silently bypass RI update enforcement.
    bool pending_append() const noexcept { return pending_append_; }
    void set_pending_append(bool v) noexcept { pending_append_ = v; }

    std::uint16_t field_count() const noexcept;
    const drivers::DbfField& field_descriptor(std::uint16_t idx) const;
    std::int32_t field_index(const std::string& name) const noexcept;

    TableType table_type() const noexcept { return type_; }

    // M11.6 / M13 — true NULL query. VFP: nullable column whose
    // `_NullFlags` bit is set for the current row. ADT: the field
    // holds its per-type NULL sentinel (adt_field_is_null). CDX/NTX
    // have no NULL concept and always report false.
    bool is_field_null(std::uint16_t field_idx);

    // M13 — AdsIsEmpty semantics. ADT: empty ≡ NULL (sentinel test).
    // DBF/VFP: the field region is blank (all spaces for ASCII types,
    // all zero bytes for binary VFP payloads); a VFP NULL also counts.
    bool is_field_empty(std::uint16_t field_idx);

    // M13 — AdsSetNull write path. VFP: sets the `_NullFlags` bit
    // (error 5205 AE_NOT_VFP_NULLABLE_FIELD when the column was not
    // declared nullable). ADT: writes the per-type NULL sentinel
    // (error 5147 for AutoInc/RowVersion). CDX/NTX: blanks the field
    // (NULL ≡ empty, per AdsSetNull's documented behavior).
    util::Result<void> set_field_null(std::uint16_t field_idx);

    std::uint32_t record_count() const noexcept;
    // Re-read the on-disk record count so concurrent appends by other
    // connections become visible.  Called by the server before answering
    // GetRecordCount and by AdsRefreshRecord.
    void refresh_record_count_from_disk() noexcept {
        driver_->refresh_record_count_from_disk();
    }
    // Clipper / SAP-ACE convention: phantom position past last
    // record reports recno()=LastRec()+1, so an empty table reads
    // as recno=1.
    std::uint32_t recno() const noexcept {
        if ((state_ == State::Eof || state_ == State::Limbo ||
             record_count() == 0) &&
            (recno_ == 0 || recno_ > record_count())) {
            return record_count() + 1;
        }
        return recno_;
    }
    // Limbo (freshly-opened empty table or after GOTOP / GOBOTTOM
    // on empty) reports BOTH BOF and EOF true. The first explicit
    // direction-bearing skip drops out into Bof or Eof proper.
    // Plain Bof/Eof states report a single flag each.
    bool eof() const noexcept {
        return state_ == State::Eof || state_ == State::Limbo;
    }
    bool bof() const noexcept {
        return state_ == State::Bof || state_ == State::Limbo;
    }

    util::Result<void> goto_top();
    util::Result<void> goto_bottom();
    util::Result<void> goto_record(std::uint32_t recno);

    // Internal helper for bulk index creation / REINDEX scans. Installs a
    // pre-read record buffer (from driver->read_record_raw) without the
    // public goto_record side effects (invalidate_read_cache + active
    // index cursor repositioning). This lets sequential full-table scans
    // benefit from the driver's read-ahead block cache. Caller is
    // responsible for validity of recno and for any FOR/DELETED filtering.
    void load_record_for_bulk_scan(std::vector<std::uint8_t> buf,
                                   std::uint32_t recno);

    // Prototype for single-pass multi-tag collection (for future optimization
    // of multi-INDEX ON / full bag rebuild). Returns one vector of (key,recno)
    // per expression. This is a prototype; not yet wired into AdsCreateIndex61.
    util::Result<std::vector<std::vector<std::pair<std::string, std::uint32_t>>>>
    collect_keys_for_multiple_expressions(
        const std::vector<std::string>& expressions,
        const std::vector<std::string>& for_clauses,
        const std::vector<std::uint16_t>& key_lens);

    // Reload the current row from disk without repositioning the active
    // index cursor — used after transaction rollback refresh.
    util::Result<void> refresh_record_buffer();
    util::Result<void> skip(std::int32_t delta);

    util::Result<drivers::DbfFieldValue>
        read_field(std::uint16_t field_index);

    // Write surface.
    util::Result<void> append_record();
    util::Result<void> set_field(std::uint16_t field_index,
                                 const std::string& value);
    util::Result<void> set_field(std::uint16_t field_index, double value);
    util::Result<void> set_field(std::uint16_t field_index, bool value);

    // Memo write with an explicit FPT block-type tag (M9.13). Mirrors
    // set_field(idx, std::string) but routes through the memo store's
    // write_typed so the on-disk block carries the binary/picture flag
    // instead of the default Text marker.
    util::Result<void>
        set_field_binary(std::uint16_t                field_index,
                         const std::string&           payload,
                         drivers::MemoBlockType       type);

    // Look up the FPT block-type tag for a memo field at the current
    // record. Returns Text if the field is empty or the memo store
    // doesn't track types (e.g. DBT).
    util::Result<drivers::MemoBlockType>
        field_memo_type(std::uint16_t field_index);
    util::Result<void> mark_deleted();
    util::Result<void> recall_deleted();
    bool               is_deleted() const noexcept;

    // True only when a concrete record is loaded (not BOF/EOF/Limbo).
    bool positioned() const noexcept { return state_ == State::Positioned; }

    // Raw physical record image of the current row, including the leading
    // deletion-flag byte. Valid only while positioned(); empty otherwise.
    // Backs AdsGetRecord.
    const std::vector<std::uint8_t>& record_buffer() const noexcept {
        return record_buf_;
    }

    // Overwrite the current record with a raw physical image (deletion flag
    // + field bytes), then write back and re-sync every bound index. Copies
    // min(len, record_length) bytes — a short buffer leaves the tail intact,
    // matching ACE's tolerant AdsSetRecord. Backs AdsSetRecord.
    util::Result<void> set_record_raw(const std::uint8_t* bytes,
                                      std::size_t len);

    // Transaction rollback helpers (M5). Restore a before-image or undo
    // an append directly on disk and re-sync every bound index. Bypasses
    // the active Tx journal — caller is rolling back the tx itself.
    util::Result<void> apply_tx_rollback(std::uint32_t recno,
                                       const std::uint8_t* bytes,
                                       std::size_t         len);
    util::Result<void> apply_tx_rollback_append(std::uint32_t recno);

    // Deferred flush mode. When true, AdsWriteRecord skips the
    // per-record FlushFileBuffers — the record is written to OS cache
    // but not synced to physical media until AdsFlushFileBuffers is
    // called. Gives 10-100x speedup for bulk inserts.
    bool deferred_flush() const noexcept { return deferred_flush_; }
    void set_deferred_flush(bool v) noexcept { deferred_flush_ = v; }

    util::Result<void> flush();
    util::Result<void> enable_cache(std::uint16_t cache_mode);
    bool cache_enabled() const noexcept { return cache_enabled_; }

    // Drop every record. Header rec count -> 0 and every bound index
    // (active order plus parked extras) is walked to erase its
    // entries. Indexes left empty but structurally intact.
    util::Result<void> zap();

    // Physically remove deleted records: copy live rows downward,
    // truncate header reccount to live count, and rebuild every
    // bound index so its entries point at the new recnos.
    util::Result<void> pack();

    // Re-build every bound index in place by clearing its entries
    // and re-inserting (recno, evaluate_index_expr) for each live
    // record. Used by AdsReindex when an app has mutated the DBF
    // outside the index sync path or the key expression has been
    // changed externally.
    util::Result<void> reindex();

    // Undo the appends made during a transaction being rolled back.
    // For each recno (processed high-to-low so trailing rows peel off
    // cleanly), erase the record's entries from every bound index, then
    // physically drop it if it is the last physical record; if a
    // concurrent append sits above it, fall back to a soft-delete. After
    // this the cursor position is reset to BOF.
    util::Result<void> rollback_appends(std::vector<std::uint32_t> recnos);

    // Locking surface.
    util::Result<void> lock_record_excl(std::uint32_t recno);
    util::Result<void> unlock_record    (std::uint32_t recno);
    util::Result<void> lock_table_excl();
    util::Result<void> unlock_table();

    // Non-blocking variants (M9.18). Used by the ABI retry loop so an
    // already-locked range surfaces as AE_LOCKED instead of blocking
    // the calling thread on the kernel.
    util::Result<void> try_lock_record_excl(std::uint32_t recno);
    util::Result<void> try_lock_table_excl ();

    // Snapshot the currently-held record-lock recnos (M9.23). Used by
    // AdsGetAllLocks to enumerate the per-table lock view without
    // exposing the LockMgr internals.
    std::vector<std::uint32_t> held_record_locks() const;

    // Recno-sequence cursor (M10.6). When non-empty, goto_top /
    // goto_bottom / skip walk this list of recnos in order instead of
    // the natural append order or any active index. Used by SQL
    // ORDER BY to pre-sort matching rows. clear_recno_sequence()
    // restores the natural order.
    void set_recno_sequence(std::vector<std::uint32_t> seq);
    void clear_recno_sequence() noexcept {
        recno_sequence_.clear(); sequence_idx_ = -1;
    }
    bool has_recno_sequence() const noexcept {
        return !recno_sequence_.empty();
    }
    // M10.31 / M10.32 — read-only access so DISTINCT / LIMIT / OFFSET
    // can post-process an ORDER-BY-installed sequence.
    const std::vector<std::uint32_t>& recno_sequence() const noexcept {
        return recno_sequence_;
    }

    // Memo surface (M4).
    void               attach_memo(std::unique_ptr<drivers::IMemoStore> memo);
    drivers::IMemoStore* memo() noexcept { return memo_.get(); }

    // Row filter (M7.3). When set, navigation methods automatically
    // advance past non-matching records in their movement direction.
    using RowPredicate = std::function<bool(Table&)>;
    void set_filter(RowPredicate p)   { filter_ = std::move(p); }
    void set_filter_expr(const std::string& e) { filter_expr_ = e; }
    const std::string& filter_expr() const noexcept { return filter_expr_; }
    void clear_filter()                {
        filter_ = nullptr;
        filter_expr_.clear();
        aof_expr_.clear();
        // M-AOF.5: AOF also installs a recno sequence on the table
        // for the sparse-bitmap Skip path. Drop both in lockstep so
        // AdsClearAOF restores the full unfiltered walk.
        if (aof_active_) clear_recno_sequence();
        aof_active_ = false;
        aof_opt_level_ = 0;
        aof_bitmap_.clear();
    }
    bool has_filter() const noexcept   { return static_cast<bool>(filter_); }

    // M-AOF.3 — install a per-record bitmap built by aof::evaluate
    // as the table-level filter predicate. Honoured by goto_top,
    // goto_bottom, and skip exactly like a hand-rolled SetFilter
    // closure; the difference is that the bitmap is precomputed
    // once (O(N)) and the predicate at navigation time is O(1) per
    // record rather than re-evaluating the filter expression.
    // `aof_active()` lets the ABI layer report AdsGetAOFOptLevel
    // distinctly from a plain SetFilter installation.
    void install_aof_bitmap(std::vector<bool> bm) {
        // The bitmap moves into the closure; navigation state
        // owns it from this point on.
        aof_active_ = true;
        auto p = std::make_shared<std::vector<bool>>(std::move(bm));
        // Keep a retained copy so AdsCustomizeAOF can flip individual
        // record bits and reinstall without re-evaluating the filter.
        aof_bitmap_ = *p;
        filter_ = [p](Table& t) -> bool {
            std::uint32_t r = t.recno();
            if (r == 0 || r > p->size()) return false;
            return static_cast<bool>((*p)[r - 1]);
        };
        // M-AOF.5 — sparse-bitmap navigation. Translate the bitmap
        // into the sorted recno sequence Table already uses to walk
        // a precomputed visible set; goto_top / goto_bottom / skip
        // then jump O(1) per step instead of iterating every recno
        // checking the predicate. Result: Skip-through-visible-set
        // becomes O(M) where M is the number of matching records,
        // which is where the textbook Rushmore "10-100x" total
        // speedup actually shows up.
        std::vector<std::uint32_t> seq;
        seq.reserve(p->size() / 4);     // typical AOF selectivities
        for (std::size_t i = 0; i < p->size(); ++i) {
            if ((*p)[i]) seq.push_back(static_cast<std::uint32_t>(i + 1));
        }
        set_recno_sequence(std::move(seq));
    }
    bool aof_active() const noexcept   { return aof_active_; }

    // AdsCustomizeAOF — force a single record into (`include=true`) or out
    // of (`include=false`) the active AOF result set. Flips its bit in the
    // retained bitmap and reinstalls so the predicate and the sparse recno
    // sequence stay consistent. Returns false if no AOF is active or the
    // recno is invalid.
    bool customize_aof_record(std::uint32_t recno, bool include) {
        if (!aof_active_ || recno < 1) return false;
        if (recno > record_count()) return false;
        if (aof_bitmap_.size() < recno) aof_bitmap_.resize(recno, false);
        aof_bitmap_[recno - 1] = include;
        install_aof_bitmap(aof_bitmap_);   // rebuilds predicate + seq
        return true;
    }

    void set_aof_expr(const std::string& e) { aof_expr_ = e; }
    const std::string& aof_expr() const noexcept { return aof_expr_; }

    // M-AOF.4 — cached opt level reported by AdsGetAOFOptLevel. Set
    // by the ABI layer right after install_aof_bitmap so the answer
    // doesn't require re-walking the AST. Cleared by clear_filter().
    void          set_aof_opt_level(int v) noexcept { aof_opt_level_ = v; }
    int           aof_opt_level() const noexcept    { return aof_opt_level_; }

    // AdsSetTableTransactionFree — records the caller's intent that
    // updates to this table bypass transaction semantics (immediate,
    // visible to other clients, never rolled back). Stored here so
    // AdsIsTableTransactionFree can report it back; the transaction
    // commit/rollback path does not yet consult this flag.
    void          set_transaction_free(bool v) noexcept { transaction_free_ = v; }
    bool          transaction_free() const noexcept     { return transaction_free_; }
    bool passes_filter() {
        return !filter_ || filter_(*this);
    }

    // Transaction binding (M5). When a Connection has an active Tx,
    // it points each open Table at it via attach_tx so Table writes
    // record before-images for rollback.
    void               attach_tx(Tx* tx, Tx::TableId tid) noexcept {
        tx_ = tx; tid_ = tid;
    }
    void               detach_tx() noexcept { tx_ = nullptr; }

    drivers::IDriver*  driver() noexcept { return driver_.get(); }

    // Order + scope surface (M3).
    void               set_order(std::unique_ptr<drivers::IIndex> idx);
    void               clear_order();
    // Take ownership of the active index back from the Table, leaving
    // it without an order. Returns nullptr if no order was set.
    std::unique_ptr<drivers::IIndex> take_order();
    Order*             order() noexcept { return order_ ? &*order_ : nullptr; }

    // Non-owning views of additional indexes the ABI layer parks
    // outside the Table. Every record mutation (`set_field`,
    // `append_record`) walks both the active order and these views,
    // so a multi-tag CDX stays consistent across all tags. The ABI
    // owns the IIndex lifetime; the Table only borrows it for sync.
    void register_extra_index_view(drivers::IIndex* idx);
    void unregister_extra_index_view(drivers::IIndex* idx);
    void clear_extra_index_views();

    // M-AOF.4 — read-only view of every open index on this table
    // (active order plus parked extra views). The AOF leaf matcher
    // uses this to find an index whose key expression matches the
    // leaf's field, so a `field OP literal` leaf can be answered
    // by an index range scan instead of a full table walk.
    std::vector<drivers::IIndex*> all_indexes();
    const Order*       order() const noexcept { return order_ ? &*order_ : nullptr; }
    util::Result<bool> seek_key(const std::string& key, bool soft,
                                bool last = false);
    bool last_seek_found() const noexcept { return last_seek_found_; }
    void set_last_seek_found(bool v) noexcept { last_seek_found_ = v; }
    util::Result<void> set_scope(bool top, const std::string& key);
    util::Result<void> clear_scope(bool top);
    util::Result<void> clear_scopes();
    std::optional<std::string> get_scope(bool top) const;

private:
    enum class State { Bof, Positioned, Eof, Limbo };

    Table(std::unique_ptr<drivers::IDriver> drv,
          OpenMode mode, LockingMode locking, TableType type) noexcept
        : driver_(std::move(drv)), mode_(mode),
          locking_(locking), type_(type) {}

    // Case-insensitive name -> field index cache. The field set is fixed
    // for the lifetime of an open table, and a work area is driven by a
    // single thread at a time (xBase / ACE work-area model), so this
    // lazily-filled cache needs no locking. Keyed by upper-cased name;
    // stores -1 for names that do not resolve.
    mutable std::unordered_map<std::string, std::int32_t> field_index_cache_;

    util::Result<void> load_record_(std::uint32_t recno);
    util::Result<void> writeback_record_();

    // Sync the active index with the current record's key. Called
    // after every record mutation (set_field / append_record). For
    // appends `prev_key` is empty so erase is skipped; for modifies
    // it carries the index key as it was just before the write so
    // the old entry can be removed before the new one is inserted.
    util::Result<void> sync_active_index_(const std::string& prev_key);

    // Snapshot every bound index's current key (active order + extra
    // views), and replay the snapshot after a write to update the
    // entries in lockstep. Multi-index variant of sync_active_index_.
    std::vector<std::pair<drivers::IIndex*, std::string>>
        snapshot_index_keys_();
    util::Result<void> sync_all_indexes_(
        const std::vector<std::pair<drivers::IIndex*, std::string>>& snap);

    // Compute the index key bytes for the current `record_buf_` for the
    // given index. Honours the index's key encoding (text vs FoxPro 8-byte
    // numeric/date) so the bytes match what a native reader expects.
    std::string compute_index_key_(const drivers::IIndex* idx) const;

    bool key_in_top_scope_   (const std::string& key) const;
    bool key_in_bottom_scope_(const std::string& key) const;

    // M13 — clear the VFP `_NullFlags` bit for `field_idx` in the
    // current record buffer. Called by every set_field overload so
    // writing a value un-NULLs the column; no-op for non-VFP tables,
    // non-nullable columns, and tables without a _NullFlags field.
    void clear_field_null_(std::uint16_t field_idx);

    TableTypeForLock to_lock_type_() const noexcept;

    std::unique_ptr<drivers::IDriver>             driver_;
    std::unique_ptr<drivers::IMemoStore>          memo_;
    RowPredicate                                  filter_;
    Tx*                                           tx_       = nullptr;
    Tx::TableId                                   tid_      = 0;
    OpenMode                                      mode_     = OpenMode::Read;
    LockingMode                                   locking_  = LockingMode::Compatible;
    TableType                                     type_     = TableType::Cdx;
    LockMgr                                       locks_;
    std::unordered_map<std::uint32_t, LockHandle> recno_locks_;
    std::optional<LockHandle>                     table_lock_;
    std::optional<Order>                          order_;
    std::vector<drivers::IIndex*>                 extra_index_views_;
    State                                         state_  = State::Bof;
    std::uint32_t                                 recno_  = 0;
    std::vector<std::uint8_t>                     record_buf_;
    std::string                                   path_;
    std::string                                   alias_;
    openads::session::Connection*                 owner_ = nullptr;
    std::uint16_t                                 char_type_ = 1;  // ADS_ANSI
    std::unordered_map<std::string, std::string>  ri_snapshot_;
    bool                                          pending_append_ = false;
    bool                                          deferred_flush_ = false;
    bool                                          cache_enabled_  = false;
    bool                                          last_seek_found_ = false;
    bool                                          aof_active_      = false;
    bool                                          transaction_free_ = false;
    int                                           aof_opt_level_   = 0;
    std::string                                   filter_expr_;    // source filter expression string
    std::string                                   aof_expr_;       // source AOF expression string
    std::vector<bool>                             aof_bitmap_;     // retained AOF set for AdsCustomizeAOF

    // M10.6 recno-sequence cursor — empty means "natural order".
    std::vector<std::uint32_t>                    recno_sequence_;
    std::int64_t                                  sequence_idx_ = -1;
};

} // namespace openads::engine
