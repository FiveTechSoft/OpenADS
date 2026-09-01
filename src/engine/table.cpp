#include "engine/table.h"

#include "session/connection.h"

#include "openads/error.h"
#include "engine/index_expr.h"
#include "engine/oem_collation.h"

#include "drivers/adt/adt_driver.h"
#include "drivers/cache/cached_driver.h"
#include "drivers/cdx/cdx_driver.h"
#include "drivers/cdx/cdx_index.h"
#include "drivers/ntx/ntx_driver.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <utility>

#include <atomic>

namespace openads::engine {

// Global SET DELETE flag (process-wide). Default true = include
// deleted rows (Clipper SET DELETED OFF). Set by AdsShowDeleted in
// the ABI layer.
static std::atomic<bool> g_show_deleted{true};
bool show_deleted() noexcept { return g_show_deleted.load(); }
void set_show_deleted(bool v) noexcept { g_show_deleted.store(v); }

// Global SET EXACT flag. Default false = Clipper SET EXACT OFF.
static std::atomic<bool> g_set_exact{false};
bool set_exact() noexcept { return g_set_exact.load(); }
void set_set_exact(bool v) noexcept { g_set_exact.store(v); }

// Global epoch (pivot year for 2-digit dates). Default 1900.
static std::atomic<std::uint16_t> g_epoch{1900};
std::uint16_t epoch() noexcept { return g_epoch.load(); }
void set_epoch(std::uint16_t v) noexcept { g_epoch.store(v); }

} // namespace openads::engine

namespace openads::abi {

inline bool show_deleted_for(const openads::engine::Table* t) noexcept {
    if (t != nullptr) {
        // SAP: AdsShowDeleted "has no effect upon ADT tables" — rows
        // deleted in an ADT "can never be retrieved by a client
        // application" (ace_adsshowdeleted). Only DBF-family tables
        // (CDX/NTX/VFP) surface deleted rows under the TRUE default.
        if (t->table_type() == openads::engine::TableType::Adt) {
            return false;
        }
        if (auto* owner = t->owner()) {
            return owner->show_deleted();
        }
    }
    return openads::engine::show_deleted();
}

inline bool show_deleted() noexcept {
    return openads::engine::show_deleted();
}

}  // namespace openads::abi

namespace openads::engine {

bool Table::show_deleted_records() const noexcept {
    return openads::abi::show_deleted_for(this);
}

namespace {

void write_adt_blob_ref(std::uint8_t* dst, std::uint32_t block_no,
                        std::uint32_t data_len) noexcept {
    dst[0] = static_cast<std::uint8_t>( block_no        & 0xFFu);
    dst[1] = static_cast<std::uint8_t>((block_no >>  8) & 0xFFu);
    dst[2] = static_cast<std::uint8_t>((block_no >> 16) & 0xFFu);
    dst[3] = static_cast<std::uint8_t>((block_no >> 24) & 0xFFu);
    dst[4] = static_cast<std::uint8_t>( data_len        & 0xFFu);
    dst[5] = static_cast<std::uint8_t>((data_len >>  8) & 0xFFu);
    dst[6] = static_cast<std::uint8_t>((data_len >> 16) & 0xFFu);
    dst[7] = static_cast<std::uint8_t>((data_len >> 24) & 0xFFu);
    dst[8] = 0x00;
}

} // namespace

util::Result<Table> Table::open(const std::string& path,
                                TableType type,
                                OpenMode mode,
                                LockingMode locking) {
    std::unique_ptr<drivers::IDriver> drv;
    switch (type) {
        case TableType::Cdx:
            drv = std::make_unique<drivers::cdx::CdxDriver>();
            break;
        case TableType::Ntx:
            drv = std::make_unique<drivers::ntx::NtxDriver>();
            break;
        case TableType::Adt:
            drv = std::make_unique<drivers::adt::AdtDriver>();
            break;
        case TableType::Vfp:
            drv = std::make_unique<drivers::cdx::CdxDriver>();
            break;
    }
    drivers::DriverOpenMode dmode = drivers::DriverOpenMode::ReadOnly;
    switch (mode) {
        case OpenMode::Read:      dmode = drivers::DriverOpenMode::ReadOnly;  break;
        case OpenMode::Shared:    dmode = drivers::DriverOpenMode::Shared;    break;
        case OpenMode::Exclusive: dmode = drivers::DriverOpenMode::Exclusive; break;
    }
    if (auto r = drv->open(path, dmode); !r) return r.error();
    Table t{std::move(drv), mode, locking, type};
    t.path_ = path;
    t.locks_.set_record_geometry(t.driver_->header_length(),
                                 t.driver_->record_length());
    if (t.driver_->record_count() == 0) {
        t.state_ = State::Limbo;
    }
    return t;
}

Table Table::from_driver(std::unique_ptr<drivers::IDriver> drv,
                         std::string path,
                         TableType type,
                         OpenMode mode,
                         LockingMode locking) {
    Table t{std::move(drv), mode, locking, type};
    t.path_ = std::move(path);
    t.locks_.set_record_geometry(t.driver_->header_length(),
                                 t.driver_->record_length());
    if (t.driver_->record_count() == 0) {
        t.state_ = State::Limbo;
    }
    return t;
}

std::uint16_t Table::field_count() const noexcept {
    return static_cast<std::uint16_t>(driver_->fields().size());
}

const drivers::DbfField& Table::field_descriptor(std::uint16_t idx) const {
    return driver_->fields().at(idx);
}

std::int32_t Table::field_index(const std::string& name) const noexcept {
    const auto& fs = driver_->fields();
    // Case-insensitive: DBF field names are always uppercase in storage;
    // SQL column names / index expressions may arrive in any case.
    auto scan_for = [&](const std::string& want) -> std::int32_t {
        for (std::size_t i = 0; i < fs.size(); ++i) {
            if (fs[i].name.size() != want.size()) continue;
            bool eq = true;
            for (std::size_t j = 0; j < want.size(); ++j) {
                if (std::toupper(static_cast<unsigned char>(want[j])) !=
                    std::toupper(static_cast<unsigned char>(fs[i].name[j]))) {
                    eq = false; break;
                }
            }
            if (eq) return static_cast<std::int32_t>(i);
        }
        return -1;
    };
    auto scan = [&]() -> std::int32_t {
        std::int32_t found = scan_for(name);
        // DBF storage truncates field names to 10 characters at CREATE
        // (a proc __output column `databasepath` lands as DATABASEPA in
        // the temp free table), so a longer lookup name can only mean
        // the truncated field — retry with the storage-truncated form.
        // ONLY for a plain identifier: an index expression like
        // `CCODIGOCON+CDOCUMETRA` starts with a 10-char field name, and
        // truncating it would report the whole compound key as that bare
        // field — pinning the key length to the first component's width,
        // which collapses every later component out of the key.
        const bool plain_ident =
            !name.empty() &&
            std::all_of(name.begin(), name.end(), [](char c) {
                return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
            });
        if (found < 0 && plain_ident && name.size() > 10)
            found = scan_for(name.substr(0, 10));
        return found;
    };
    // O(1) repeat lookups via an upper-cased-name cache. Wrapped so an
    // allocation failure degrades to the linear scan rather than violating
    // noexcept.
    try {
        std::string key;
        key.reserve(name.size());
        for (char c : name)
            key.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(c))));
        auto it = field_index_cache_.find(key);
        if (it != field_index_cache_.end()) return it->second;
        std::int32_t found = scan();
        field_index_cache_.emplace(std::move(key), found);
        return found;
    } catch (...) {
        return scan();
    }
}

std::uint32_t Table::record_count() const noexcept {
    return driver_->record_count();
}

util::Result<void> Table::load_record_(std::uint32_t recno) {
    // GoCold before replacing the buffer (navigation / absolute goto).
    if (record_dirty_) {
        if (auto r = commit_dirty_record(); !r) return r.error();
    }
    auto buf = driver_->read_record_raw(recno);
    if (!buf) return buf.error();
    record_buf_ = std::move(buf).value();
    recno_      = recno;
    state_      = State::Positioned;
    return {};
}

std::string Table::compute_index_key_(const drivers::IIndex* idx) const {
    const std::string& expr    = idx->expression();
    const std::uint16_t key_len = idx->key_length();
    // FoxPro/Harbour numeric & date CDX keys are an 8-byte order-preserving
    // binary encoding, not text. Build those bytes here so both the write
    // path (sync_all_indexes_) and the engine's own seek-after-write stay
    // consistent. Everything else (character keys, NTX/ADI) keeps the
    // legacy text path so existing files round-trip byte-exact.
    if (idx->key_encoding() == drivers::KeyEncoding::FoxNumeric) {
        double d = 0.0;
        evaluate_index_expr_number(const_cast<Table&>(*this), expr, d);
        return fox_numeric_key(d);
    }
    // Numeric NTX keys are native zero-padded fixed-width text (negatives
    // byte-complemented). Build them here so the write path and the engine's
    // own seek-after-write stay byte-identical to what a native reader expects.
    if (idx->key_encoding() == drivers::KeyEncoding::NtxNumeric) {
        double d = 0.0;
        evaluate_index_expr_number(const_cast<Table&>(*this), expr, d);
        return ntx_numeric_key(d, idx->key_length(), idx->key_decimals());
    }
    // Compound expressions (UPPER(NAME), STR(AGE,3), concatenation,
    // SUBSTR, ...) handled by engine/index_expr.cpp. Bare field-name
    // expressions short-circuit there to the legacy raw-bytes path so
    // existing CDX files stay byte-exact.
    auto r = evaluate_index_expr(const_cast<Table&>(*this), expr, key_len);
    if (!r) return std::string(key_len, ' ');
    return r.value();
}

// Snapshot the current key for every bound index. Caller invokes
// this BEFORE mutating record_buf_ so the snapshot reflects the
// pre-write key per index; after the write, sync_all_indexes_(snap)
// erases each prior key and inserts the new one.
std::vector<Table::IndexSnap>
Table::snapshot_index_keys_() {
    std::vector<IndexSnap> out;
    auto push = [&](drivers::IIndex* idx) {
        if (idx == nullptr) return;
        // A tag can be bound twice — as the active order and parked as an
        // extra view (reopened bag). Syncing it twice erases+inserts the
        // same (key, recno) pair twice per commit, duplicating entries.
        for (const auto& s : out) {
            if (s.idx == idx) return;
        }
        IndexSnap s;
        s.idx      = idx;
        s.prev_key = compute_index_key_(idx);
        const std::string& cond = idx->condition();
        s.prev_included =
            cond.empty() || evaluate_index_expr_truthy(*this, cond);
        out.push_back(std::move(s));
    };
    if (order_ && order_->index()) push(order_->index());
    for (auto* x : extra_index_views_) push(x);
    return out;
}

util::Result<void> Table::sync_active_index_(const std::string& /*unused*/) {
    // Re-entered after a full snapshot was taken on the caller's
    // side; that path is the new sync_all_indexes_ below.
    return {};
}

util::Result<void> Table::sync_all_indexes_(
    const std::vector<IndexSnap>& snap) {
    for (const auto& s : snap) {
        drivers::IIndex* idx = s.idx;
        if (idx == nullptr) continue;
        const std::string& prev_key = s.prev_key;
        std::string new_key = compute_index_key_(idx);
        // Conditional (FOR) tags index only records whose condition
        // currently evaluates true — write-time maintenance included:
        // appends and edits must move the record in and out of the tag as
        // it crosses the boundary. Previously every written record got a
        // key, so a conditional tag degenerated into a full index; and a
        // record crossing INTO the condition with an unchanged key was
        // swallowed by the prev==new shortcut and never inserted at all.
        const std::string& cond = idx->condition();
        const bool include =
            cond.empty() || evaluate_index_expr_truthy(*this, cond);
        // Erase prior (recno, prev_key).
        //
        // On a genuine UPDATE of an indexed record the old entry MUST
        // exist — a missing key means the B-tree is corrupt (stale
        // separators, duplicate keys) and we propagate the error so the
        // caller can abort before inserting the new key on top of a stale
        // entry.
        //
        // On a fresh APPEND the snapshot was taken before fields were set,
        // so prev_key is the blank-key encoding (spaces/zeros) that was
        // never inserted.  5044 ("key not found") is expected there and
        // tolerated so the insert below can proceed. The append marker is
        // tracked per recno (append_pending_recno_), not only per edit
        // session (snap_was_append_): an intermediate no-op commit — a
        // per-field flush that changes no key — clears pending_append_
        // before the key-bearing commit, and without the recno marker the
        // tolerated erase became a bogus corrupt-tree abort that dropped
        // the new key (remote AppendBlank + SetField flow, key field not
        // written first).
        const bool fresh_append =
            snap_was_append_ ||
            (append_pending_recno_ != 0 && recno_ == append_pending_recno_);
        const bool tolerate_missing_erase = fresh_append;

        if (!include) {
            // Does not match the FOR (or no longer does): erase only when
            // the record WAS indexed before this edit.
            if (s.prev_included && !prev_key.empty()) {
                if (auto e = idx->erase(recno_, prev_key); !e) {
                    if (!tolerate_missing_erase) return e.error();
                }
            }
            continue;
        }

        // Blank-key trap: an appended record whose key equals the blank
        // encoding (numeric 0 -> fox(0), all-space text) compared equal to
        // the snapshot and its insert was skipped entirely — the record
        // never reached the index (bulk walks lost it, key counts ran
        // short). Track the views already keyed this append session and
        // never skip the first insert of a fresh append. A record that did
        // not match the FOR before the edit is in the same state: not
        // indexed, so an unchanged key still needs the insert. Unique
        // views are excluded: a blank key must not collide in a unique bag.
        const bool key_not_inserted_yet =
            fresh_append && !idx->unique() &&
            append_keys_done_.count(idx) == 0;
        const bool need_insert = !s.prev_included || key_not_inserted_yet;
        if (prev_key == new_key && !need_insert) continue;

        if (!prev_key.empty() && prev_key != new_key && s.prev_included) {
            if (auto e = idx->erase(recno_, prev_key); !e) {
                if (!tolerate_missing_erase) return e.error();
                // Fresh append: tolerate missing key (blank-key snapshot).
            }
        }
        // Enforce uniqueness before inserting: seek for the new key and reject
        // if another record already carries it.
        if (idx->unique()) {
            auto sr = idx->seek_key(new_key, /*soft=*/false);
            if (sr) {
                const auto& so = sr.value();
                if (so.positioned &&
                    so.hit == openads::drivers::SeekHit::Exact &&
                    so.recno != recno_) {
                    return util::Error{openads::AE_UNIQUE_INDEX_VIOLATION, 0,
                        "duplicate key value in unique index '" +
                        idx->expression() + "'", ""};
                }
            }
        }
        if (auto e = idx->insert(recno_, new_key); !e) {
            return e.error();
        }
        if (fresh_append) append_keys_done_.insert(idx);
    }
    return {};
}

util::Result<void> Table::ensure_writable_() {
    if (state_ != State::Positioned) {
        return util::Error{5068, 0, "no record positioned", ""};
    }
    // GoHot-equivalent write guard (harbour dbf1.c hb_dbfGoHot).  In shared
    // mode the caller must hold either a file lock (FLock) or a record lock
    // (RLock) on the CURRENT row before mutating it.  Newly-appended
    // records pass because AdsAppendRecord physically auto-locks the new
    // recno (xBase / ACE implicit append lock).  pending_append_ is
    // deliberately NOT consulted: it stays set until WriteRecord, so an app
    // could append, move to a victim row, and edit it with no lock at all
    // (Pritpal Bedi: "Editing succeeds - no alert about LOCK REQUIRED").
    // Exclusive and read-only opens bypass the check — they grant
    // unrestricted access by definition.
    if (mode_ == OpenMode::Shared && !is_table_locked()) {
        if (recno_locks_.find(recno_) == recno_locks_.end()) {
            return util::Error{5035, 0,
                "write failed — record not locked (RLock or FLock required "
                "in shared mode)", ""};
        }
    }
    return {};
}

util::Result<void> Table::begin_dirty_edit_() {
    if (auto w = ensure_writable_(); !w) return w.error();
    // Snapshot index keys once, before the first mutation of this row.
    if (!record_dirty_) {
        index_snap_       = snapshot_index_keys_();
        snap_was_append_  = pending_append_;
        record_dirty_     = true;
    }
    return {};
}

void Table::discard_dirty_() noexcept {
    record_dirty_    = false;
    snap_was_append_ = false;
    index_snap_.clear();
}

util::Result<void> Table::commit_dirty_record() {
    if (!record_dirty_) return {};
    if (auto wb = writeback_record_(); !wb) return wb.error();
    auto snap = std::move(index_snap_);
    record_dirty_ = false;
    pending_append_ = false;
    index_snap_.clear();
    return sync_all_indexes_(snap);
}

util::Result<void> Table::writeback_record_() {
    if (auto w = ensure_writable_(); !w) return w.error();
    if (tx_ && tx_->active()) {
        auto cur = driver_->read_record_raw(recno_);
        if (cur) {
            std::vector<std::uint8_t> before = std::move(cur).value();
            std::vector<std::uint8_t> after(record_buf_);
            tx_->note_before_image(tid_, recno_, std::move(before), std::move(after));
        }
    }
    return driver_->write_record_raw(recno_, record_buf_.data(),
                                     record_buf_.size());
}

// Clipper / DBFCDX: a scope bound SHORTER than the index key bounds by PREFIX —
// every key that begins with it is inside the scope. Comparing a full-width key
// against a short bound puts all of them PAST the bottom ("2B 22563     1" is
// lexicographically greater than "2B 22563"), which empties the scope. Compare
// only as many bytes as the bound supplies.
// The top bound needs no such care mathematically (any extension of a prefix is
// >= the prefix), but it is written symmetrically so the two read alike and a
// future edit cannot break one without the other.
// TOP is the traversal-side START bound: an ascending walk keeps keys >= top;
// a descending one keeps keys <= top (it steps DOWN from the top bound).
bool Table::key_in_top_scope_(const std::string& key) const {
    if (!order_ || !order_->scope().top.has_value()) return true;
    const std::string& top = *order_->scope().top;
    if (order_->descending_traverse()) {
        if (key.size() > top.size())
            return key.compare(0, top.size(), top) <= 0;
        return key <= top;
    }
    if (key.size() > top.size())
        return key.compare(0, top.size(), top) >= 0;
    return key >= top;
}

// BOTTOM is the traversal-side END bound: ascending keeps keys <= bottom;
// descending keeps keys >= bottom.
bool Table::key_in_bottom_scope_(const std::string& key) const {
    if (!order_ || !order_->scope().bottom.has_value()) return true;
    const std::string& bottom = *order_->scope().bottom;
    if (order_->descending_traverse()) {
        if (key.size() > bottom.size())
            return key.compare(0, bottom.size(), bottom) >= 0;
        return key >= bottom;
    }
    if (key.size() > bottom.size())
        return key.compare(0, bottom.size(), bottom) <= 0;
    return key <= bottom;
}

void Table::set_recno_sequence(std::vector<std::uint32_t> seq) {
    recno_sequence_ = std::move(seq);
    sequence_idx_   = -1;
    // Installed, even when empty: an empty visible set means "no rows",
    // not "walk everything". See Table::recno_sequence_active().
    recno_sequence_active_ = true;
}

util::Result<void> Table::goto_top() {
    // Settle any pending field edits before leaving the current row
    // (empty-table paths never call load_record_).
    // If the dirty record cannot be flushed (shared mode + no lock),
    // discard it silently — goto_top is a navigation operation.
    if (auto r = commit_dirty_record(); !r) discard_dirty_();
    // Absolute reposition: drop any read-ahead block so we observe
    // writes made through another handle and start the scan fresh.
    driver_->invalidate_read_cache();
    // Multiuser: peer appends update the on-disk header; re-read so
    // natural-order navigation reaches the new LastRec (Pritpal Bedi:
    // browse only walked rows present at open while RecCount was current).
    driver_->refresh_record_count_from_disk();
    if (order_ && order_->index()) order_->index()->refresh_from_disk();
    // Empty table → Limbo regardless of active order / sequence.
    if (driver_->record_count() == 0) {
        state_ = State::Limbo; recno_ = 0; return {};
    }
    if (recno_sequence_active_) {
        if (recno_sequence_.empty()) {
            state_ = State::Limbo; recno_ = 0; return {};
        }
        sequence_idx_ = 0;
        std::uint32_t r = recno_sequence_.front();
        return load_record_(r);
    }
    if (order_ && order_->index()) {
        auto* idx = order_->index();
        util::Result<drivers::SeekOutcome> r = drivers::SeekOutcome{};
        if (order_->descending_traverse()) {
            if (order_->scope().top.has_value()) {
                // Descending traversal starts at the highest key allowed
                // by the top bound; a soft seek may land ABOVE it, so step
                // back (prev) until the key is inside the scope. Landing
                // unconditionally on seek_last() put the cursor past the
                // top bound and the walk read as empty (Pritpal Bedi's
                // descending scope case).
                r = idx->seek_key(*order_->scope().top, /*soft=*/true);
                while (r && r.value().positioned &&
                       !key_in_top_scope_(idx->current_key())) {
                    r = idx->prev();
                    if (!r) break;
                }
            } else {
                r = idx->seek_last();
            }
        } else if (order_->scope().top.has_value()) {
            r = idx->seek_key(*order_->scope().top, true);
        } else {
            r = idx->seek_first();
        }
        if (!r) return r.error();
        if (!r.value().positioned) {
            state_ = State::Limbo; recno_ = 0; return {};
        }
        // Out-of-scope landing: for an ascending walk the overflow shows
        // past the BOTTOM bound; for a descending one it shows past the
        // TOP (the walk then steps DOWN towards the bottom bound).
        const bool out_of_scope = order_->descending_traverse()
            ? !key_in_top_scope_(idx->current_key())
            : !key_in_bottom_scope_(idx->current_key());
        if (out_of_scope) {
            state_ = State::Eof; recno_ = 0; return {};
        }
        // SET DELETE ON: skip deleted rows in the walk direction.
        // If everything's deleted (or filtered out by the index +
        // delete combo) → Limbo so DBGOTOP reports BOF+EOF both
        // true (Clipper / DBFCDX convention for "no visible row").
        if (!openads::abi::show_deleted_for(this)) {
            while (r.value().positioned) {
                auto ld = load_record_(r.value().recno);
                if (!ld && ld.error().code != 5000) return ld.error();
                // A 5000 here is a stale index entry (recno > rec_count,
                // e.g. a PACK left this tag unreconstructed). Native ADSCDX
                // walks past it, so treat it like a deleted row and advance.
                if (ld && !is_deleted()) return {};
                r = order_->descending_traverse()
                        ? idx->prev() : idx->next();
                if (!r) return r.error();
                if (r.value().positioned &&
                    !key_in_bottom_scope_(idx->current_key())) {
                    state_ = State::Limbo; recno_ = 0; return {};
                }
            }
            state_ = State::Limbo; recno_ = 0; return {};
        }
        // SET DELETE OFF: land on the first index entry, stepping past any
        // stale ones (recno > rec_count after a PACK) the way native ADSCDX
        // does instead of raising ADSCDX/5000.
        while (r.value().positioned) {
            auto ld = load_record_(r.value().recno);
            if (!ld && ld.error().code != 5000) return ld.error();
            if (ld) return {};
            r = order_->descending_traverse() ? idx->prev() : idx->next();
            if (!r) return r.error();
            if (r.value().positioned &&
                !key_in_bottom_scope_(idx->current_key())) {
                state_ = State::Limbo; recno_ = 0; return {};
            }
        }
        state_ = State::Limbo; recno_ = 0; return {};
    }
    if (driver_->record_count() == 0) {
        // GOTOP on empty re-enters Limbo (BOF+EOF both true).
        state_ = State::Limbo; recno_ = 0; return {};
    }
    if (auto r = load_record_(1); !r) return r.error();
    if (filter_ && state_ == State::Positioned && !filter_(*this)) {
        return skip(1);
    }
    // SET DELETE ON without an active index: walk forward over the
    // raw record range until a live row appears.
    if (!openads::abi::show_deleted_for(this)) {
        std::uint32_t r = 1;
        while (r <= driver_->record_count() && is_deleted()) {
            ++r;
            if (r > driver_->record_count()) {
                state_ = State::Limbo; recno_ = 0; return {};
            }
            if (auto ld = load_record_(r); !ld) return ld.error();
        }
    }
    return {};
}

util::Result<void> Table::goto_bottom() {
    if (auto r = commit_dirty_record(); !r) return r.error();
    // Absolute reposition: drop any read-ahead block (see goto_top).
    driver_->invalidate_read_cache();
    driver_->refresh_record_count_from_disk();
    if (order_ && order_->index()) order_->index()->refresh_from_disk();
    // Empty table → Limbo regardless of active order / sequence.
    if (driver_->record_count() == 0) {
        state_ = State::Limbo; recno_ = 0; return {};
    }
    if (recno_sequence_active_) {
        if (recno_sequence_.empty()) {
            state_ = State::Limbo; recno_ = 0; return {};
        }
        sequence_idx_ = static_cast<std::int64_t>(recno_sequence_.size() - 1);
        return load_record_(recno_sequence_.back());
    }
    if (order_ && order_->index()) {
        auto* idx = order_->index();
        util::Result<drivers::SeekOutcome> r = idx->seek_last();
        if (!r) return r.error();
        if (!r.value().positioned) {
            state_ = State::Limbo; recno_ = 0; return {};
        }
        while (r.value().positioned &&
               !key_in_bottom_scope_(idx->current_key())) {
            r = idx->prev();
            if (!r) return r.error();
        }
        if (!r.value().positioned ||
            !key_in_top_scope_(idx->current_key())) {
            state_ = State::Eof; recno_ = 0; return {};
        }
        if (!openads::abi::show_deleted_for(this)) {
            while (r.value().positioned) {
                auto ld = load_record_(r.value().recno);
                if (!ld && ld.error().code != 5000) return ld.error();
                // Stale index entry (recno > rec_count after a PACK): step
                // past it like a deleted row instead of raising 5000.
                if (ld && !is_deleted()) {
                    return {};
                }
                r = idx->prev();
                if (!r) return r.error();
            }
            state_ = State::Limbo; recno_ = 0; return {};
        }
        // SET DELETE OFF: land on the last entry, stepping back past stale
        // ones the way native ADSCDX does instead of raising 5000.
        while (r.value().positioned) {
            auto ld = load_record_(r.value().recno);
            if (!ld && ld.error().code != 5000) return ld.error();
            if (ld) return {};
            r = idx->prev();
            if (!r) return r.error();
        }
        state_ = State::Limbo; recno_ = 0; return {};
    }
    auto n = driver_->record_count();
    if (n == 0) {
        state_ = State::Limbo; recno_ = 0; return {};
    }
    if (auto r = load_record_(n); !r) return r.error();
    // SET DELETE ON without active index: walk back to first live.
    if (!openads::abi::show_deleted_for(this)) {
        std::uint32_t r = n;
        while (r >= 1 && is_deleted()) {
            if (r == 1) {
                state_ = State::Limbo; recno_ = 0; return {};
            }
            --r;
            if (auto ld = load_record_(r); !ld) return ld.error();
        }
    }
    return {};
}

util::Result<void> Table::goto_record(std::uint32_t recno) {
    if (auto r = commit_dirty_record(); !r) return r.error();
    // Leaving the record ends the append key-sync window (see
    // append_pending_recno_ in table.h).
    append_pending_recno_ = 0;
    append_keys_done_.clear();
    // Absolute reposition / AdsRefreshRecord: drop any read-ahead block
    // so the (re)read hits disk — this is how a workarea sees an edit
    // made through another handle, and how RefreshRecord re-reads.
    driver_->invalidate_read_cache();
    // Multiuser: a peer may have appended since we last read the header.
    // Without this, GO <new_recno> and "past end" decisions use a stale
    // LastRec and the browse never reaches peer-appended rows.
    driver_->refresh_record_count_from_disk();
    // Harbour / SAP-ACE / Clipper convention: GO 0 is the phantom
    // position. On empty table → Limbo (BOF+EOF). Otherwise → Eof,
    // unless we were already sitting in Limbo (e.g. after a
    // GOTOP that walked over an all-deleted set under SET
    // DELETE ON) — preserve Limbo then so DBGOTO(0) doesn't
    // accidentally promote to Eof and lose the BOF half.
    if (recno == 0) {
        if (driver_->record_count() == 0 || state_ == State::Limbo) {
            state_ = State::Limbo; recno_ = 0; return {};
        }
        state_ = State::Eof;
        recno_ = 0;
        return {};
    }
    if (recno > driver_->record_count()) {
        // Clipper / dbf1.c: GO past-end is a phantom position
        // (Limbo) — BOTH BOF() and EOF() true. recno() reports
        // LastRec()+1.
        state_ = State::Limbo; recno_ = 0;
        return {};
    }
    auto r = load_record_(recno);
    if (!r) return r.error();
    // Re-position the active index cursor on this row's key so a
    // subsequent SKIP walks from here (and not from wherever the
    // index was left after a previous SEEK / SKIP-past-end). When
    // multiple records share the key we need to walk forward to
    // the entry whose recno actually equals what the caller asked
    // for — otherwise SKIP(±1) jumps to a sibling-key boundary.
    if (order_ && order_->index()) {
        auto* idx = order_->index();
        idx->invalidate_cursor();
        std::string key = compute_index_key_(idx);
        auto sk = idx->seek_key(key, /*soft=*/false);
        if (sk && sk.value().positioned) {
            std::uint32_t guard = 0;
            while (sk.value().recno != recno && guard++ < 1024) {
                auto nx = idx->next();
                if (!nx || !nx.value().positioned) break;
                std::string ck = idx->current_key();
                if (ck.size() < key.size())
                    ck.append(key.size() - ck.size(), ' ');
                if (ck.size() > key.size()) ck.resize(key.size());
                if (std::memcmp(ck.data(), key.data(), key.size()) != 0)
                    break;
                sk = nx;
            }
            if (sk.value().recno != recno) {
                idx->invalidate_cursor();
            }
        }
    }
    return {};
}

void Table::load_record_for_bulk_scan(std::vector<std::uint8_t> buf,
                                      std::uint32_t recno) {
    // Bulk reindex / key-collect replaces the buffer deliberately; any
    // pending edit on another row should already have been settled by
    // the caller. Drop dirty state so we don't later GoCold a stale snap.
    discard_dirty_();
    record_buf_ = std::move(buf);
    recno_      = recno;
    state_      = State::Positioned;
}

util::Result<std::vector<std::vector<std::pair<std::string, std::uint32_t>>>>
Table::collect_keys_for_multiple_expressions(
    const std::vector<std::string>& expressions,
    const std::vector<std::string>& for_clauses,
    const std::vector<std::uint16_t>& key_lens) {

    const auto n = expressions.size();
    if (for_clauses.size() != n || key_lens.size() != n) {
        return util::Error{5000, 0, "mismatched expression lists", ""};
    }

    std::vector<std::vector<std::pair<std::string, std::uint32_t>>> results(n);
    const auto rec_count = driver_->record_count();

    for (std::uint32_t r = 1; r <= rec_count; ++r) {
        auto raw = driver_->read_record_raw(r);
        if (!raw) continue;
        load_record_for_bulk_scan(std::move(raw.value()), r);

        for (size_t i = 0; i < n; ++i) {
            const auto& for_expr = for_clauses[i];
            if (!for_expr.empty() &&
                !evaluate_index_expr_truthy(*this, for_expr)) {
                continue;
            }
            auto k = evaluate_index_expr(*this, expressions[i], key_lens[i]);
            if (k) {
                results[i].emplace_back(std::move(k.value()), r);
            }
        }
    }
    return results;
}

util::Result<void> Table::refresh_record_buffer() {
    if (state_ != State::Positioned || recno_ == 0) return {};
    // AdsRefreshRecord discards uncommitted buffer edits and reloads
    // the on-disk image (do not GoCold first).
    discard_dirty_();
    return load_record_(recno_);
}

util::Result<void> Table::skip(std::int32_t delta) {
    // Settle dirty buffer before cursor motion (GoCold). load_record_
    // also commits, but EOF/BOF exits never load.
    if (auto r = commit_dirty_record(); !r) return r.error();
    // Leaving the record ends the append key-sync window (see
    // append_pending_recno_ in table.h).
    append_pending_recno_ = 0;
    append_keys_done_.clear();
    // Multiuser visibility vs browse speed:
    //   - GoTop / GoBottom / GetRecordCount always re-read the header.
    //   - Per-Skip full refresh was correct but ~header-I/O per keystroke
    //     and crushed xBrowse (Pritpal). Instead we refresh only when the
    //     cached EOF fence is about to fire (natural order) or when the
    //     index walk runs off the end (peer may have grown the bag).
    // SQL materialised sequences stay a frozen snapshot.
    if (recno_sequence_active_) {
        if (delta == 0) {
            if (state_ == State::Bof) sequence_idx_ = -1;
            else if (state_ == State::Eof)
                sequence_idx_ =
                    static_cast<std::int64_t>(recno_sequence_.size());
            return {};
        }
        std::int64_t idx = sequence_idx_;
        if (state_ == State::Bof) idx = -1;
        if (state_ == State::Eof)
            idx = static_cast<std::int64_t>(recno_sequence_.size());
        idx += delta;
        if (idx < 0) {
            state_ = State::Bof; recno_ = 0; sequence_idx_ = -1; return {};
        }
        if (idx >= static_cast<std::int64_t>(recno_sequence_.size())) {
            state_ = State::Eof; recno_ = 0;
            sequence_idx_ = static_cast<std::int64_t>(recno_sequence_.size());
            return {};
        }
        sequence_idx_ = idx;
        return load_record_(recno_sequence_[static_cast<std::size_t>(idx)]);
    }
    if (order_ && order_->index()) {
        auto* idx = order_->index();
        if (delta == 0) return {};
        // M10.4: when the order is descending, forward-skip walks
        // prev() instead of next() so the cursor moves through the
        // tree in reverse from the caller's perspective.
        bool effective_forward = (delta > 0) ^ order_->descending_traverse();
        util::Result<drivers::SeekOutcome> r = drivers::SeekOutcome{};
        const bool skip_deleted = !openads::abi::show_deleted_for(this);
        std::int32_t want = std::abs(delta);
        std::int32_t taken = 0;
        // Clipper convention: SKIP that overshoots the order leaves
        // the cursor on the LAST visited live record and reports Bof
        // / Eof — not on recno 0. Track the most-recent live we saw.
        std::uint32_t last_live = recno_;
        while (taken < want) {
            r = effective_forward ? idx->next() : idx->prev();
            if (!r) return r.error();
            if (!r.value().positioned) {
                // Index walk end: do not re-read the bag on every Skip
                // (browse hot path). GoTop/GoBottom already refresh the
                // CDX header so a full re-paint sees peer keys.
                if (delta > 0) {
                    state_ = State::Eof; recno_ = 0;
                } else {
                    state_ = State::Bof;
                    if (last_live != 0) {
                        (void)load_record_(last_live);
                        state_ = State::Bof;
                    } else {
                        recno_ = 0;
                    }
                }
                return {};
            }
            if (!key_in_top_scope_(idx->current_key()) ||
                !key_in_bottom_scope_(idx->current_key())) {
                if (delta > 0) {
                    state_ = State::Eof; recno_ = 0;
                } else {
                    state_ = State::Bof;
                    if (last_live != 0) {
                        (void)load_record_(last_live);
                        state_ = State::Bof;
                    } else {
                        recno_ = 0;
                    }
                }
                return {};
            }
            if (skip_deleted) {
                // Probe the row's deleted flag without advancing
                // the user-visible step count.
                auto ld = load_record_(r.value().recno);
                if (!ld) {
                    if (ld.error().code != 5000) return ld.error();
                    // Stale index entry (recno > rec_count after a PACK):
                    // invisible like a deleted row — skip without counting.
                    continue;
                }
                if (is_deleted()) continue;
            } else if (r.value().recno > driver_->record_count()) {
                // SET DELETED OFF doesn't load every row (perf), so a stale
                // entry would only surface at the final load below. Gate
                // cheaply on the recno range — no I/O in the common case —
                // and only when it looks past the live count confirm with a
                // load: a genuine just-appended row (peer multiuser) loads
                // fine and counts; a stale PACK leftover returns 5000 and is
                // skipped without counting.
                auto ld = load_record_(r.value().recno);
                if (!ld) {
                    if (ld.error().code != 5000) return ld.error();
                    continue;
                }
            }
            last_live = r.value().recno;
            ++taken;
        }
        return load_record_(r.value().recno);
    }
    auto n = driver_->record_count();
    if (n == 0) {
        // Empty may be stale: a peer could have just written the first row.
        driver_->refresh_record_count_from_disk();
        n = driver_->record_count();
    }
    if (n == 0) {
        // Skip on empty:
        //   delta == 0  -> preserve current state (Limbo / Bof / Eof
        //                  per Clipper SKIP-zero "refresh, don't move").
        //   delta > 0   -> Eof (single flag).
        //   delta < 0   -> Bof (single flag).
        if (delta > 0) state_ = State::Eof;
        else if (delta < 0) state_ = State::Bof;
        recno_ = 0; return {};
    }
    // Rows the cursor must not land on: deleted rows when SET DELETED
    // is ON (show_deleted() == false) and rows rejected by an active
    // filter. The index-order path above already skips deleted rows;
    // the natural-order path must do the same.
    const bool skip_deleted = !openads::abi::show_deleted_for(this);
    auto must_skip = [&]() -> bool {
        if (state_ != State::Positioned) return false;
        if (skip_deleted && is_deleted()) return true;
        if (filter_ && !filter_(*this)) return true;
        return false;
    };
    // Lazy multiuser refresh of the EOF fence: only re-read the header
    // when a step would land past the cached LastRec (or before 1).
    auto refresh_n_if_needed = [&](std::int64_t pos) {
        if (pos < 1) return;
        if (pos <= static_cast<std::int64_t>(n)) return;
        driver_->refresh_record_count_from_disk();
        n = driver_->record_count();
    };
    if ((skip_deleted || filter_) && delta != 0) {
        // M12.33 — count VISIBLE rows, like the index-order path above.
        // The old recno + delta jump (then slide only while sitting on a
        // hidden row) landed one visible row short for every hidden row
        // strictly inside the range, so a remote folded Skip(N) re-served
        // a row the client had already painted (the "deleted record
        // duplicates the previous item" browse bug).
        const std::int64_t stepdir = (delta > 0) ? 1 : -1;
        const std::int64_t want =
            (delta > 0) ? delta : -static_cast<std::int64_t>(delta);
        std::int64_t pos = static_cast<std::int64_t>(recno_);
        std::int64_t taken = 0;
        while (taken < want) {
            pos += stepdir;
            if (pos < 1) { state_ = State::Bof; recno_ = 0; return {}; }
            refresh_n_if_needed(pos);
            if (pos > static_cast<std::int64_t>(n)) {
                state_ = State::Eof; recno_ = n + 1; return {};
            }
            if (auto r = load_record_(static_cast<std::uint32_t>(pos)); !r) {
                return r.error();
            }
            if (must_skip()) continue;
            ++taken;
        }
        return {};
    }
    std::int64_t target = static_cast<std::int64_t>(recno_) + delta;
    if (state_ == State::Bof && delta > 0) target = delta;
    if (target < 1) { state_ = State::Bof; recno_ = 0; return {}; }
    refresh_n_if_needed(target);
    if (target > static_cast<std::int64_t>(n)) {
        state_ = State::Eof; recno_ = n + 1; return {};
    }
    if (auto r = load_record_(static_cast<std::uint32_t>(target)); !r) {
        return r.error();
    }
    // Skip(0) refresh on a hidden row still slides off it (Clipper
    // filter convention: the cursor never rests on an invisible row).
    if (skip_deleted || filter_) {
        std::int64_t step = (delta >= 0) ? 1 : -1;
        while (must_skip()) {
            std::int64_t nt = static_cast<std::int64_t>(recno_) + step;
            if (nt < 1) { state_ = State::Bof; recno_ = 0; return {}; }
            if (nt > static_cast<std::int64_t>(n)) {
                state_ = State::Eof; recno_ = n + 1; return {};
            }
            if (auto r = load_record_(static_cast<std::uint32_t>(nt)); !r) {
                return r.error();
            }
        }
    }
    return {};
}

util::Result<drivers::DbfFieldValue>
Table::read_field(std::uint16_t field_index) {
    if (state_ != State::Positioned) {
        // 5068 = AE_NO_CURRENT_RECORD (SAP ADS SDK). Harbour rddads'
        // adsGetValue special-cases this exact code as the graceful
        // "read past the last record" path and substitutes a blank value;
        // any other error code — including 5026 (AE_INVALID_WORKAREA) — is
        // raised as a hard error (the ADSCDX/5000 failure TBrowse hits when
        // it paints an EOF row).
        return util::Error{5068, 0, "table not positioned on a record",
                           ""};
    }
    if (field_index >= driver_->fields().size()) {
        return util::Error{5063, 0, "field index out of range", ""};
    }
    const auto& f = driver_->fields().at(field_index);
    // M13 — a VFP column whose _NullFlags bit is set holds undefined data
    // bytes; report NULL instead of decoding the stale payload. (ADT
    // sentinel NULLs are recognized inside decode_field via f.adt.)
    if (type_ == TableType::Vfp && is_field_null(field_index)) {
        drivers::DbfFieldValue nul;
        nul.is_null = true;
        return nul;
    }
    auto v = drivers::decode_field(f, record_buf_.data(), record_buf_.size());
    if (!v) return v.error();

    // ADT binary memo/binary reference (9 bytes in record):
    //   uint32 LE block_no | uint32 LE data_len | 0x00
    // Detected by field.length == 9 which is exclusive to ADT memo refs
    // (DBF memo fields are always 10-byte ASCII).
    if ((f.type == drivers::DbfFieldType::Memo ||
         f.type == drivers::DbfFieldType::Binary) &&
        f.length == 9 && memo_) {
        const std::uint8_t* ref = record_buf_.data() + f.record_offset;
        std::uint32_t block_no = static_cast<std::uint32_t>(ref[0])        |
                                (static_cast<std::uint32_t>(ref[1]) <<  8) |
                                (static_cast<std::uint32_t>(ref[2]) << 16) |
                                (static_cast<std::uint32_t>(ref[3]) << 24);
        std::uint32_t data_len = static_cast<std::uint32_t>(ref[4])        |
                                (static_cast<std::uint32_t>(ref[5]) <<  8) |
                                (static_cast<std::uint32_t>(ref[6]) << 16) |
                                (static_cast<std::uint32_t>(ref[7]) << 24);
        if (block_no != 0 && data_len != 0) {
            auto mr = memo_->read(block_no, data_len);
            if (!mr) return mr.error();
            drivers::DbfFieldValue out;
            out.as_string = std::move(mr).value();
            return out;
        }
        return v;
    }

    // DBF ASCII memo reference: 10-byte decimal block number.
    if (f.type == drivers::DbfFieldType::Memo && memo_) {
        std::string raw(reinterpret_cast<const char*>(
            record_buf_.data() + f.record_offset), f.length);
        // Skip leading spaces; trailing NULs/spaces ignored by stoul.
        std::uint32_t block = 0;
        try {
            std::size_t pos = 0;
            block = static_cast<std::uint32_t>(std::stoul(raw, &pos, 10));
        } catch (...) {
            block = 0;
        }
        if (block != 0) {
            auto mr = memo_->read(block);
            if (!mr) return mr.error();
            drivers::DbfFieldValue out;
            out.as_string = std::move(mr).value();
            return out;
        }
    }
    return v;
}

util::Result<void> Table::append_record() {
    bump_live_gen();
    if (mode_ == OpenMode::Read) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    // GoCold the previous row before starting a new append.
    if (auto r = commit_dirty_record(); !r) return r.error();
    auto rec = drivers::make_empty_record(driver_->record_length());

    // M13 — make_empty_record fills the buffer with spaces, which is only
    // right for CDX/NTX. ADT rows initialize every field to its per-type
    // NULL sentinel (SAP: "all fields are initialized with their NULL
    // value") with the 4 reserved prefix bytes zeroed; VFP rows must zero
    // the _NullFlags bitmap bytes or the 0x20 fill makes bits 5, 13, ...
    // read as spurious NULLs.
    {
        const auto& f0 = driver_->fields();
        if (!f0.empty() && f0.front().adt) {
            if (rec.size() >= 5) std::memset(rec.data() + 1, 0x00, 4);
            for (const auto& f : f0) {
                if (f.type == drivers::DbfFieldType::AutoInc ||
                    f.type == drivers::DbfFieldType::RowVersion) {
                    if (static_cast<std::size_t>(f.record_offset) + f.length
                            <= rec.size()) {
                        std::memset(rec.data() + f.record_offset, 0x00,
                                    f.length);
                    }
                    continue;
                }
                (void)drivers::encode_field_null(f, rec.data(), rec.size());
            }
        } else {
            std::int32_t nf = field_index("_NullFlags");
            if (nf >= 0) {
                const auto& nff = f0[static_cast<std::size_t>(nf)];
                if (static_cast<std::size_t>(nff.record_offset) + nff.length
                        <= rec.size()) {
                    std::memset(rec.data() + nff.record_offset, 0x00,
                                nff.length);
                }
            }
        }
    }

    // M10.11: pre-fill VFP autoinc fields with their current counter
    // value, then bump the on-disk counter so the next append picks
    // up `value + step`.
    const auto& fs = driver_->fields();
    for (std::uint16_t i = 0; i < fs.size(); ++i) {
        if (!fs[i].autoinc) continue;
        auto bumped = driver_->bump_autoinc(i);
        if (!bumped) continue;   // tolerate non-autoinc-capable drivers
        std::uint32_t v = bumped.value();
        std::uint16_t off = fs[i].record_offset;
        if (fs[i].length >= 4 &&
            static_cast<std::size_t>(off) + 4 <= rec.size()) {
            rec[off    ] = static_cast<std::uint8_t>( v        & 0xFFu);
            rec[off + 1] = static_cast<std::uint8_t>((v >>  8) & 0xFFu);
            rec[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
            rec[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
        }
    }

    auto new_recno = driver_->append_record_raw(rec.data(), rec.size());
    if (!new_recno) return new_recno.error();
    record_buf_ = std::move(rec);
    recno_      = new_recno.value();
    state_      = State::Positioned;
    // No key is inserted at append time (a unique index must not see the
    // blank key); the first key-bearing commit inserts it. Mark the recno
    // so sync_all_indexes_ tolerates the never-inserted blank key on
    // erase until that commit lands (see append_pending_recno_ in table.h).
    append_pending_recno_ = recno_;
    append_keys_done_.clear();
    // xBase / ACE semantics: a freshly-appended record in a shared table is
    // automatically locked, so immediate field sets pass the GoHot write
    // guard (which is physical-lock based — hb_dbfGoHot checks
    // hb_dbfIsLocked). The lock used to be taken only by the ABI's
    // AdsAppendRecord; engine-level callers (SQL INSERT, server AppendBlank)
    // need it too. Best-effort: the lock layer no-ops in read/exclusive
    // modes, and contention on a brand-new recno cannot occur.
    (void)try_lock_record_excl(recno_);
    // Mark the table as "pending append" so AdsWriteRecord classifies the
    // commit as an insert for RI / trigger handling.
    pending_append_ = true;
    if (tx_ && tx_->active()) {
        tx_->note_append(tid_, recno_);
    }
    // NOTE: the new record is intentionally NOT keyed here. An eager
    // blank-key insert at append time (tried in 760ad3d) costs two extra
    // B-tree mutations per appended record (insert blank + erase blank at
    // write) and measured a ~40% remote append slowdown with an active
    // CDX (1081 -> 660 rec/s). Everything the eager insert protected is
    // covered by the write path: append_pending_recno_ makes the blank
    // erase tolerated, and key_not_inserted_yet forces the first insert
    // of a fresh append even when the key is unchanged (blank-key trap).
    // A bare, never-written append stays unkeyed until its first commit —
    // matching SAP, where an uncommitted append is not visible to reads.
    return {};
}

util::Result<void> Table::set_field(std::uint16_t idx, const std::string& v) {
    if (state_ != State::Positioned) {
        // rddads (Harbour contrib RDD) special-cases 5068 (AE_NO_CURRENT_RECORD)
        // to return blank field values at BOF/EOF; 5026 causes a hard error.
        return util::Error{5068, 0, "no record positioned", ""};
    }
    if (idx >= driver_->fields().size()) {
        return util::Error{5063, 0, "field index out of range", ""};
    }
    const auto& f = driver_->fields().at(idx);

    // Encode into the in-memory record buffer; writeback + index sync
    // are deferred to commit_dirty_record() (WriteRecord / flush / skip).
    if (auto d = begin_dirty_edit_(); !d) return d.error();

    // Memo/Binary fields write to the memo store, then store the
    // resulting block number in the record. ADT uses a 9-byte binary
    // reference (block_no u32 LE + data_len u32 LE + 0x00); DBF uses
    // a right-aligned ASCII string.
    if (f.type == drivers::DbfFieldType::Memo ||
        (f.type == drivers::DbfFieldType::Binary && f.length == 9)) {
        if (!memo_) {
            return util::Error{5004, 0, "memo store not attached", ""};
        }
        auto wm = memo_->write(v);
        if (!wm) return wm.error();
        std::uint8_t* dst = record_buf_.data() + f.record_offset;
        if (f.length == 9) {
            write_adt_blob_ref(dst, wm.value(),
                               static_cast<std::uint32_t>(v.size()));
        } else {
            char buf[16];
            int n = std::snprintf(buf, sizeof(buf), "%*u",
                                  static_cast<int>(f.length),
                                  static_cast<unsigned>(wm.value()));
            if (n < 0 || static_cast<std::size_t>(n) > f.length) {
                return util::Error{5000, 0,
                                   "memo block number overflows field", ""};
            }
            std::memcpy(dst, buf, f.length);
        }
        clear_field_null_(idx);
        return {};
    }

    auto r = drivers::encode_field_string(f, record_buf_.data(),
                                          record_buf_.size(), v);
    if (!r) return r.error();
    clear_field_null_(idx);
    return {};
}

util::Result<void> Table::set_field(std::uint16_t idx, double v) {
    if (state_ != State::Positioned) {
        // rddads (Harbour contrib RDD) special-cases 5068 (AE_NO_CURRENT_RECORD)
        // to return blank field values at BOF/EOF; 5026 causes a hard error.
        return util::Error{5068, 0, "no record positioned", ""};
    }
    if (idx >= driver_->fields().size()) {
        return util::Error{5063, 0, "field index out of range", ""};
    }
    if (auto d = begin_dirty_edit_(); !d) return d.error();
    auto r = drivers::encode_field_double(driver_->fields().at(idx),
                                          record_buf_.data(),
                                          record_buf_.size(), v);
    if (!r) return r.error();
    clear_field_null_(idx);
    return {};
}

util::Result<void> Table::set_field(std::uint16_t idx, bool v) {
    if (state_ != State::Positioned) {
        // rddads (Harbour contrib RDD) special-cases 5068 (AE_NO_CURRENT_RECORD)
        // to return blank field values at BOF/EOF; 5026 causes a hard error.
        return util::Error{5068, 0, "no record positioned", ""};
    }
    if (idx >= driver_->fields().size()) {
        return util::Error{5063, 0, "field index out of range", ""};
    }
    if (auto d = begin_dirty_edit_(); !d) return d.error();
    auto r = drivers::encode_field_logical(driver_->fields().at(idx),
                                           record_buf_.data(),
                                           record_buf_.size(), v);
    if (!r) return r.error();
    clear_field_null_(idx);
    return {};
}

util::Result<void>
Table::set_field_binary(std::uint16_t idx, const std::string& payload,
                        drivers::MemoBlockType type) {
    if (state_ != State::Positioned) {
        // rddads (Harbour contrib RDD) special-cases 5068 (AE_NO_CURRENT_RECORD)
        // to return blank field values at BOF/EOF; 5026 causes a hard error.
        return util::Error{5068, 0, "no record positioned", ""};
    }
    if (idx >= driver_->fields().size()) {
        return util::Error{5063, 0, "field index out of range", ""};
    }
    const auto& f = driver_->fields().at(idx);
    if (f.type != drivers::DbfFieldType::Memo &&
        !(f.type == drivers::DbfFieldType::Binary && f.length == 9)) {
        return util::Error{5063, 0, "field is not a memo column", ""};
    }
    if (!memo_) {
        return util::Error{5004, 0, "memo store not attached", ""};
    }
    if (auto d = begin_dirty_edit_(); !d) return d.error();
    auto wm = memo_->write_typed(payload, type);
    if (!wm) return wm.error();
    std::uint8_t* dst = record_buf_.data() + f.record_offset;
    if (f.length == 9) {
        write_adt_blob_ref(dst, wm.value(),
                           static_cast<std::uint32_t>(payload.size()));
    } else {
        char buf[16];
        int n = std::snprintf(buf, sizeof(buf), "%*u",
                              static_cast<int>(f.length),
                              static_cast<unsigned>(wm.value()));
        if (n < 0 || static_cast<std::size_t>(n) > f.length) {
            return util::Error{5000, 0, "memo block number overflows field", ""};
        }
        std::memcpy(dst, buf, f.length);
    }
    clear_field_null_(idx);
    return {};
}

util::Result<drivers::MemoBlockType>
Table::field_memo_type(std::uint16_t idx) {
    if (state_ != State::Positioned) {
        // rddads (Harbour contrib RDD) special-cases 5068 (AE_NO_CURRENT_RECORD)
        // to return blank field values at BOF/EOF; 5026 causes a hard error.
        return util::Error{5068, 0, "no record positioned", ""};
    }
    if (idx >= driver_->fields().size()) {
        return util::Error{5063, 0, "field index out of range", ""};
    }
    const auto& f = driver_->fields().at(idx);
    if (f.type != drivers::DbfFieldType::Memo || !memo_) {
        return drivers::MemoBlockType::Text;
    }
    std::string raw(reinterpret_cast<const char*>(record_buf_.data() +
                                                  f.record_offset),
                    f.length);
    while (!raw.empty() && raw.front() == ' ') raw.erase(raw.begin());
    while (!raw.empty() && raw.back()  == ' ') raw.pop_back();
    if (raw.empty()) return drivers::MemoBlockType::Text;
    std::uint32_t block_no = 0;
    for (char c : raw) {
        if (c < '0' || c > '9') return drivers::MemoBlockType::Text;
        block_no = block_no * 10 + static_cast<std::uint32_t>(c - '0');
    }
    if (block_no == 0) return drivers::MemoBlockType::Text;
    return memo_->read_type(block_no);
}

util::Result<void> Table::apply_tx_rollback(std::uint32_t recno,
                                            const std::uint8_t* bytes,
                                            std::size_t         len) {
    if (driver_ == nullptr) {
        return util::Error{5000, 0, "no driver", ""};
    }
    if (auto r = load_record_(recno); !r) return r.error();
    auto snap = snapshot_index_keys_();
    const std::size_t rl = driver_->record_length();
    if (len != rl) {
        return util::Error{5000, 0, "rollback record length mismatch", ""};
    }
    std::memcpy(record_buf_.data(), bytes, len);
    if (auto w = driver_->write_record_raw(recno, record_buf_.data(), rl); !w) {
        return w.error();
    }
    return sync_all_indexes_(snap);
}

util::Result<void> Table::set_record_raw(const std::uint8_t* bytes,
                                         std::size_t len) {
    if (state_ != State::Positioned) {
        // rddads special-cases 5068 to blank out at BOF/EOF; see writeback_.
        return util::Error{5068, 0, "no record positioned", ""};
    }
    if (mode_ == OpenMode::Read) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    if (bytes == nullptr) {
        return util::Error{5000, 0, "null record buffer", ""};
    }
    // Whole-record replace: fold into the dirty buffer (reuse existing
    // pre-edit snap if fields were already dirty on this row).
    if (auto d = begin_dirty_edit_(); !d) return d.error();
    const std::size_t rl = driver_->record_length();
    const std::size_t n  = (len < rl) ? len : rl;
    std::memcpy(record_buf_.data(), bytes, n);
    return {};
}

util::Result<void> Table::apply_tx_rollback_append(std::uint32_t recno) {
    if (driver_ == nullptr) {
        return util::Error{5000, 0, "no driver", ""};
    }
    if (auto r = load_record_(recno); !r) return r.error();
    auto snap = snapshot_index_keys_();
    drivers::set_record_deleted(record_buf_.data(), record_buf_.size(), true);
    if (auto w = driver_->write_record_raw(recno, record_buf_.data(),
                                           record_buf_.size()); !w) {
        return w.error();
    }
    return sync_all_indexes_(snap);
}

util::Result<void> Table::mark_deleted() {
    bump_live_gen();
    if (state_ != State::Positioned) {
        // rddads (Harbour contrib RDD) special-cases 5068 (AE_NO_CURRENT_RECORD)
        // to return blank field values at BOF/EOF; 5026 causes a hard error.
        return util::Error{5068, 0, "no record positioned", ""};
    }
    // Deletion is a structural change: fold into dirty (with any pending
    // field edits) and settle immediately so SET DELETE ON walks see it.
    if (auto d = begin_dirty_edit_(); !d) return d.error();
    drivers::set_record_deleted(record_buf_.data(), record_buf_.size(), true);
    return commit_dirty_record();
}

util::Result<void> Table::recall_deleted() {
    bump_live_gen();
    if (state_ != State::Positioned) {
        // rddads (Harbour contrib RDD) special-cases 5068 (AE_NO_CURRENT_RECORD)
        // to return blank field values at BOF/EOF; 5026 causes a hard error.
        return util::Error{5068, 0, "no record positioned", ""};
    }
    if (auto d = begin_dirty_edit_(); !d) return d.error();
    drivers::set_record_deleted(record_buf_.data(), record_buf_.size(), false);
    return commit_dirty_record();
}

bool Table::is_deleted() const noexcept {
    if (state_ != State::Positioned) return false;
    return drivers::record_is_deleted(record_buf_.data(), record_buf_.size());
}

util::Result<bool> Table::deleted_at(std::uint32_t recno) {
    if (recno == 0 || recno > driver_->record_count()) {
        return util::Error{5000, 0, "record number out of range", ""};
    }
    auto rec = driver_->read_record_raw(recno);
    if (!rec) return rec.error();
    const auto& buf = rec.value();
    return drivers::record_is_deleted(buf.data(), buf.size());
}

util::Result<void> Table::zap() {
    bump_live_gen();
    if (mode_ == OpenMode::Read) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    if (driver_ == nullptr) {
        return util::Error{5000, 0, "no driver", ""};
    }
    // Walk every bound index and erase its entries before truncating
    // the DBF; otherwise stale (recno, key) pairs remain.
    auto erase_all = [&](drivers::IIndex* idx) -> util::Result<void> {
        if (idx == nullptr) return {};
        std::vector<std::pair<std::uint32_t, std::string>> entries;
        auto seek = idx->seek_first();
        while (seek && seek.value().positioned) {
            entries.emplace_back(seek.value().recno, idx->current_key());
            seek = idx->next();
        }
        for (auto& [rec, key] : entries) {
            (void)idx->erase(rec, key);
        }
        if (auto fl = idx->flush(); !fl) return fl.error();
        return {};
    };
    if (order_ && order_->index()) {
        if (auto r = erase_all(order_->index()); !r) return r.error();
    }
    for (auto* x : extra_index_views_) {
        if (auto r = erase_all(x); !r) return r.error();
    }
    if (auto r = driver_->zap(); !r) return r.error();
    state_ = State::Bof;
    recno_ = 0;
    record_buf_.assign(driver_->record_length(), 0);
    return {};
}

util::Result<void> Table::rollback_appends(std::vector<std::uint32_t> recnos) {
    if (driver_ == nullptr || recnos.empty()) return {};
    // High-to-low so the transaction's own trailing rows peel off the
    // end one at a time.
    std::sort(recnos.begin(), recnos.end(),
              [](std::uint32_t a, std::uint32_t b) { return a > b; });
    for (std::uint32_t r : recnos) {
        if (r == 0) continue;
        // Load the record so compute_index_key_ sees its current bytes,
        // then erase its (recno, key) from every bound index. (A record
        // appended and later updated in the same tx carries the updated
        // key — the one currently in the index.)
        bool loaded = false;
        if (auto rec = driver_->read_record_raw(r); rec) {
            record_buf_ = std::move(rec).value();
            recno_      = r;
            state_      = State::Positioned;
            loaded      = true;
            auto erase_idx = [&](drivers::IIndex* idx) {
                if (idx == nullptr) return;
                std::string key = compute_index_key_(idx);
                (void)idx->erase(r, key);
            };
            if (order_ && order_->index()) erase_idx(order_->index());
            for (auto* x : extra_index_views_) erase_idx(x);
        }
        // Physically drop the row if it is the trailing record; if a
        // concurrent append now sits above it, soft-delete instead.
        auto popped = driver_->truncate_trailing(r);
        if (popped && popped.value()) continue;
        if (loaded) {
            std::vector<std::uint8_t> buf = record_buf_;
            drivers::set_record_deleted(buf.data(), buf.size(), true);
            (void)driver_->write_record_raw(r, buf.data(), buf.size());
        }
    }
    // Structural change — drop any cursor position.
    state_ = State::Bof;
    recno_ = 0;
    return {};
}

util::Result<void> Table::pack() {
    bump_live_gen();
    if (mode_ == OpenMode::Read) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    if (driver_ == nullptr) {
        return util::Error{5000, 0, "no driver", ""};
    }
    // 1) Copy live records down. Iterate from recno 1; track destination.
    std::uint32_t dst = 0;
    std::uint32_t total = driver_->record_count();
    for (std::uint32_t src = 1; src <= total; ++src) {
        // Sequential scan: read straight from the driver (read-ahead friendly)
        // instead of goto_record(), which would reseek the active order on
        // every record. Nothing here consults the order cursor.
        auto raw = driver_->read_record_raw(src);
        if (!raw) return raw.error();
        load_record_for_bulk_scan(std::move(raw.value()), src);
        if (is_deleted()) continue;
        ++dst;
        if (dst != src) {
            if (auto w = driver_->write_record_raw(dst, record_buf_.data(),
                                                   record_buf_.size()); !w) {
                return w.error();
            }
        }
    }
    // 2) Drop the trailing stale rows. The copy-down above already placed the
    //    survivors at recnos 1..dst, so this is a single header/EOF rewrite +
    //    physical file truncate via truncate_to() — avoiding the read-all + zap
    //    + re-append pass (a full extra I/O pass, with a per-record flush, that
    //    dominated PACK on large tables). Drivers without truncate_to fall back
    //    to that legacy path. Pack matches Clipper's semantics: indexes are
    //    left stale, the caller must REINDEX.
    bool truncated = (dst >= total);   // nothing removed → records unchanged
    if (!truncated) {
        auto t = driver_->truncate_to(dst);
        if (!t) return t.error();
        truncated = t.value();
    }
    if (!truncated) {
        std::vector<std::vector<std::uint8_t>> survivors;
        survivors.reserve(dst);
        for (std::uint32_t i = 1; i <= dst; ++i) {
            auto rec = driver_->read_record_raw(i);
            if (!rec) return rec.error();
            survivors.push_back(std::move(rec).value());
        }
        if (auto r = driver_->zap(); !r) return r.error();
        for (auto& buf : survivors) {
            auto a = driver_->append_record_raw(buf.data(), buf.size());
            if (!a) return a.error();
        }
    }
    record_buf_.assign(driver_->record_length(), 0);
    // Clipper / DBFCDX semantics: PACK rebuilds the controlled indexes so a
    // post-PACK index walk never references a recno beyond the compacted
    // record count. (the truncate/zap above intentionally leaves bound indexes stale —
    // without this rebuild, dbGoTop+dbSkip over a stale tag walks onto a
    // dropped recno and raises ADSCDX/5000 "record number out of range".)
    // Only needed when records were actually removed: if nothing was deleted
    // (dst == total) the recnos are unchanged and the indexes stay valid.
    bool removed = (dst < total);
    bool has_index = (order_ && order_->index() != nullptr);
    for (auto* x : extra_index_views_) {
        if (x != nullptr) { has_index = true; break; }
    }
    if (removed && has_index) {
        if (auto r = reindex(); !r) return r.error();   // sets Bof / recno 0
    } else {
        state_ = State::Bof;
        recno_ = 0;
    }
    return {};
}

util::Result<void> Table::reindex() {
    if (mode_ == OpenMode::Read) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    // Settle any coalesced dirty record first: the rebuild below reads
    // rows straight from disk, so a pending buffer edit would otherwise
    // be indexed from its stale on-disk image (and silently dropped by
    // load_record_for_bulk_scan's discard).
    if (auto r = commit_dirty_record(); !r) return r.error();
    if (driver_ == nullptr) return {};

    std::vector<drivers::IIndex*> indexes;
    if (order_ && order_->index()) indexes.push_back(order_->index());
    for (auto* x : extra_index_views_) {
        if (x != nullptr) indexes.push_back(x);
    }
    if (indexes.empty()) {
        state_ = State::Bof;
        recno_ = 0;
        return {};
    }

    // RCB 2026-07-10 — effective per-table collation (connection
    // AdsSetCollation override, else ADS_OEM char type + configured
    // default), not just the connection's. An ADS_OEM table reindexed
    // without this rebuilt its tags in binary order (#130 follow-up).
    const std::uint8_t* conn_sort = oem_sort_table();
    const auto rec_count = driver_->record_count();

    auto rebuild_cdx = [&](drivers::cdx::CdxIndex* cdx)
        -> util::Result<void> {
        cdx->set_oem_sort_table(conn_sort);
        const std::string& for_expr = cdx->condition();
        const std::string& expr    = cdx->expression();
        const std::uint16_t klen   = cdx->key_length();
        const bool fox = cdx->key_encoding() == drivers::KeyEncoding::FoxNumeric;
        std::vector<std::pair<std::string, std::uint32_t>> keys;
        keys.reserve(rec_count);
        for (std::uint32_t r = 1; r <= rec_count; ++r) {
            // Direct read to let the CdxDriver read-ahead cache work for
            // the full scan (goto_record would invalidate on each step).
            auto raw = driver_->read_record_raw(r);
            if (!raw) return raw.error();
            load_record_for_bulk_scan(std::move(raw.value()), r);

            // DBFCDX keeps deleted rows in the index -- the index is a
            // logical mirror of the table and SET DELETED hides them at
            // navigation time. The CREATE INDEX bulk path already follows
            // this rule; REINDEX must too, or an OrdListRebuild silently
            // drops deleted rows' keys (OrdKeyCount drift vs DBFCDX,
            // found by the dballcmp conformance harness).
            if (!for_expr.empty() &&
                !evaluate_index_expr_truthy(*this, for_expr)) {
                continue;
            }
            std::string kbytes;
            if (fox) {
                double dv = 0.0;
                if (!evaluate_index_expr_number(*this, expr, dv)) {
                    return util::Error{5000, 0,
                        "failed to evaluate numeric index expression", ""};
                }
                kbytes = fox_numeric_key(dv);
            } else {
                auto k = evaluate_index_expr(*this, expr, klen);
                if (!k) return k.error();
                kbytes = std::move(k).value();
            }
            keys.emplace_back(std::move(kbytes), r);
        }
        if (auto cl = cdx->clear_data(); !cl) return cl.error();
        if (auto b = cdx->build_bulk(std::move(keys)); !b) return b.error();
        return {};
    };

    // CDX: bulk bottom-up rebuild per tag (same fast path as CREATE INDEX).
    // NTX / ADI: legacy erase-then-insert walk.
    for (auto* idx : indexes) {
        if (auto* cdx = dynamic_cast<drivers::cdx::CdxIndex*>(idx)) {
            if (auto r = rebuild_cdx(cdx); !r) return r.error();
            continue;
        }
        std::vector<std::pair<std::uint32_t, std::string>> entries;
        auto seek = idx->seek_first();
        while (seek && seek.value().positioned) {
            entries.emplace_back(seek.value().recno, idx->current_key());
            seek = idx->next();
        }
        for (auto& [rec, key] : entries) {
            (void)idx->erase(rec, key);
        }
        // Entries were already erased above; sync only re-inserts.
        IndexSnap rebuild_snap;
        rebuild_snap.idx           = idx;
        rebuild_snap.prev_key      = {};
        rebuild_snap.prev_included = true;
        const std::vector<IndexSnap> snap{rebuild_snap};
        const std::string for_expr = idx->condition();
        for (std::uint32_t r = 1; r <= rec_count; ++r) {
            auto raw = driver_->read_record_raw(r);
            if (!raw) return raw.error();
            load_record_for_bulk_scan(std::move(raw.value()), r);

            // Deleted rows keep their keys (see the CDX rebuild above).
            if (!for_expr.empty() &&
                !evaluate_index_expr_truthy(*this, for_expr)) {
                continue;
            }
            if (auto s = sync_all_indexes_(snap); !s) return s.error();
        }
    }

    // Flush every index so the rebuilt entries hit disk before the
    // caller resumes work.
    if (order_ && order_->index()) {
        if (auto r = order_->index()->flush(); !r) return r.error();
    }
    for (auto* x : extra_index_views_) {
        if (x == nullptr) continue;
        if (auto r = x->flush(); !r) return r.error();
    }
    state_ = State::Bof;
    recno_ = 0;
    return {};
}

util::Result<void> Table::flush() {
    // Settle the dirty record buffer before OS-level fsync so
    // AdsWriteRecord / AdsFlushFileBuffers persist field edits.
    if (auto r = commit_dirty_record(); !r) return r.error();
    if (auto r = driver_->flush(); !r) return r.error();
    if (order_ && order_->index()) {
        if (auto r = order_->index()->flush(); !r) return r.error();
    }
    for (auto* extra : extra_index_views_) {
        if (extra == nullptr) continue;
        if (auto r = extra->flush(); !r) return r.error();
    }
    return {};
}

util::Result<void> Table::enable_cache(std::uint16_t cache_mode) {
    using openads::drivers::cache::CachedDriver;
    using openads::drivers::cache::TableCacheMode;

    if (cache_mode == 0 || cache_enabled_) return {};
    TableCacheMode mode = TableCacheMode::None;
    if (cache_mode == 1) mode = TableCacheMode::Reads;
    else if (cache_mode == 2) mode = TableCacheMode::Writes;
    else return {};

    // RCB 06/28/2026: Cache wrapping happens after the concrete driver is
    // opened and configured, so encryption and driver-specific setup remain
    // owned by the normal open path. Oversized tables simply keep using disk;
    // the table property is a preference, not permission to exhaust memory.
    auto wrapped = CachedDriver::create(std::move(driver_), mode);
    if (!wrapped) {
        if (wrapped.error().code == openads::AE_FUNCTION_NOT_AVAILABLE) {
            return {};
        }
        return wrapped.error();
    }
    driver_ = std::move(wrapped).value();
    cache_enabled_ = true;
    return {};
}

TableTypeForLock Table::to_lock_type_() const noexcept {
    switch (type_) {
        case TableType::Cdx: return TableTypeForLock::Cdx;
        case TableType::Ntx: return TableTypeForLock::Ntx;
        case TableType::Adt: return TableTypeForLock::Adt;
        case TableType::Vfp: return TableTypeForLock::Vfp;
    }
    return TableTypeForLock::Cdx;
}

util::Result<void> Table::lock_record_excl(std::uint32_t recno) {
    if (mode_ == OpenMode::Read) return {};
    if (table_lock_) {
        // xBase semantics (hb_dbfRawLock REC_LOCK): a record lock while the
        // table lock is held is a no-op success — the FLock range already
        // covers every record-lock byte, and taking the OS byte lock would
        // fail with ERROR_LOCK_VIOLATION against our own FLock. Register a
        // virtual (offset 0) entry so the write guard / AdsGetNumLocks /
        // AdsIsRecordLocked still see the recno.
        recno_locks_.emplace(recno, LockHandle{});
        return load_record_(recno);
    }
    auto h = locks_.lock_record_excl(driver_->file(), to_lock_type_(),
                                     locking_, recno);
    if (!h) return h.error();
    auto [it, inserted] = recno_locks_.emplace(recno, std::move(h).value());
    if (!inserted) {
        // Nested acquire on the same record — refcount bumped in LockMgr.
        (void)it;
    }
    return load_record_(recno);
}

util::Result<void> Table::try_lock_record_excl(std::uint32_t recno) {
    if (mode_ == OpenMode::Read) return {};
    if (table_lock_) {
        recno_locks_.emplace(recno, LockHandle{});
        return load_record_(recno);
    }
    auto h = locks_.try_lock_record_excl(driver_->file(), to_lock_type_(),
                                         locking_, recno);
    if (!h) return h.error();
    auto [it, inserted] = recno_locks_.emplace(recno, std::move(h).value());
    if (!inserted) {
        (void)it;
    }
    return load_record_(recno);
}

util::Result<void> Table::unlock_record(std::uint32_t recno) {
    // GoCold while the RLock is still held. Shared-mode writeback needs the
    // lock (5035 otherwise); apps that RLock → REPLACE → Unlock without an
    // explicit WriteRecord still must land field edits + index keys on disk
    // (xBase / ACE cold-on-unlock semantics).
    if (record_dirty_ && (recno == 0 || recno == recno_)) {
        if (auto r = commit_dirty_record(); !r) return r.error();
    }
    auto it = recno_locks_.find(recno);
    if (it != recno_locks_.end()) {
        if (table_lock_) {
            // While FLocked no entry holds an OS byte lock (virtual or
            // suspended) — just drop the registration.
            recno_locks_.erase(it);
        } else if (locks_.unlock_record(driver_->file(), to_lock_type_(),
                                        locking_, recno)) {
            it->second.release();
            recno_locks_.erase(it);
        }
    }
    return {};
}

// While the table lock is held, no recno_locks_ entry holds an OS byte
// lock (the Harbour FLock range covers every record-lock byte, and
// Windows rejects a same-handle overlap). Re-assert them at OS level.
// Entries a peer grabbed in the gap are dropped — the lock is genuinely
// lost, and a later write to it fails the 5035 guard instead of silently
// proceeding unprotected.
void Table::reassert_record_locks_() noexcept {
    std::vector<std::uint32_t> lost;
    for (auto& [recno, h] : recno_locks_) {
        auto rh = locks_.try_lock_record_excl(driver_->file(),
                                              to_lock_type_(), locking_,
                                              recno);
        if (rh) h = std::move(rh).value();
        else    lost.push_back(recno);
    }
    for (auto recno : lost) recno_locks_.erase(recno);
}

// xBase/ACE semantics: FLock subsumes the caller's OWN record locks. The
// registrations are kept (AdsGetNumLocks keeps counting them and the
// write guard keeps seeing them); only the OS bytes are released, and
// re-asserted by unlock_table / on failure here.
void Table::suspend_own_record_locks_() noexcept {
    for (auto& [recno, h] : recno_locks_) {
        if (h.offset() == 0) continue;   // virtual — nothing at OS level
        locks_.force_unlock_record(driver_->file(), to_lock_type_(),
                                   locking_, recno);
        h.release();
    }
}

util::Result<void> Table::lock_table_excl() {
    if (mode_ == OpenMode::Read) return {};
    if (table_lock_) return {};
    suspend_own_record_locks_();
    auto h = locks_.lock_table_excl(driver_->file(), to_lock_type_(), locking_);
    if (!h) {
        reassert_record_locks_();   // best-effort restore
        return h.error();
    }
    table_lock_ = std::move(h).value();
    return {};
}

std::vector<std::uint32_t> Table::held_record_locks() const {
    std::vector<std::uint32_t> out;
    out.reserve(recno_locks_.size());
    for (auto& [recno, _] : recno_locks_) out.push_back(recno);
    std::sort(out.begin(), out.end());
    return out;
}

util::Result<void> Table::try_lock_table_excl() {
    if (mode_ == OpenMode::Read) return {};
    if (table_lock_) return {};
    suspend_own_record_locks_();
    auto h = locks_.try_lock_table_excl(driver_->file(), to_lock_type_(),
                                        locking_);
    if (!h) {
        reassert_record_locks_();   // best-effort restore
        return h.error();
    }
    table_lock_ = std::move(h).value();
    return {};
}

util::Result<void> Table::unlock_table() {
    // GoCold under FLock before releasing the table lock.
    if (record_dirty_) {
        if (auto r = commit_dirty_record(); !r) return r.error();
    }
    if (table_lock_) {
        if (locks_.unlock_table(driver_->file(), to_lock_type_(), locking_)) {
            table_lock_->release();
            table_lock_.reset();
        }
    }
    // SAP ACE semantics (verified against ace32/ace64, commit 1fb224b):
    // AdsUnlockTable releases EVERY lock on the table — the file lock AND
    // all record locks. Harbour's rddads maps dbUnlock() straight here
    // with no client-side lock tracking, so leaving RLocks behind leaks
    // them: the next dbRLock() on the same record blocks forever
    // (Pritpal Bedi: "dbUnlock() in threads fail somehow", Aug 2026).
    // The bb86a40 experiment (RLocks survive UnlockTable) was reverted.
    for (auto& [recno, lh] : recno_locks_) {
        if (lh.offset() != 0) {   // real OS byte lock (not FLock-virtual)
            locks_.unlock_record(driver_->file(), to_lock_type_(),
                                 locking_, recno);
        }
        lh.release();
    }
    recno_locks_.clear();
    return {};
}

void Table::attach_memo(std::unique_ptr<drivers::IMemoStore> memo) {
    memo_ = std::move(memo);
}

void Table::set_order(std::unique_ptr<drivers::IIndex> idx) {
    bool desc = idx && idx->descending();
    order_.emplace(std::move(idx));
    if (desc) order_->set_descending_traverse(true);
}

void Table::clear_order() {
    order_.reset();
}

std::unique_ptr<drivers::IIndex> Table::take_order() {
    if (!order_) return nullptr;
    auto idx = order_->release();
    order_.reset();
    return idx;
}

void Table::register_extra_index_view(drivers::IIndex* idx) {
    if (idx == nullptr) return;
    for (auto* v : extra_index_views_) if (v == idx) return;
    extra_index_views_.push_back(idx);
}

void Table::unregister_extra_index_view(drivers::IIndex* idx) {
    extra_index_views_.erase(
        std::remove(extra_index_views_.begin(), extra_index_views_.end(), idx),
        extra_index_views_.end());
}

void Table::clear_extra_index_views() {
    extra_index_views_.clear();
}

std::vector<drivers::IIndex*> Table::all_indexes() {
    std::vector<drivers::IIndex*> out;
    if (order_ && order_->index()) out.push_back(order_->index());
    for (auto* x : extra_index_views_) {
        if (x == nullptr) continue;
        // Skip dup-of-active so callers see each index once.
        if (order_ && order_->index() == x) continue;
        out.push_back(x);
    }
    return out;
}

// Clipper / DBFCDX / SAP-ACE: a seek key SHORTER than the index key matches
// by PREFIX — `SEEK con+doc` over a `con+doc+STR(seq,6,0)` tag must report
// Found()=.T. and land on the first entry of the group. Padding the search
// key out to the full index key length before comparing turns every partial
// hit into a miss, because the pad bytes never equal the stored suffix
// (" " vs "     1"). Compare only the bytes the caller actually supplied.
// A key LONGER than the index key keeps the historical truncation.
static bool key_prefix_matches(const std::string& current_key,
                               const std::string& seek_key,
                               std::size_t index_key_len) {
    const std::size_t n = std::min(seek_key.size(), index_key_len);
    return current_key.size() >= n &&
           current_key.compare(0, n, seek_key, 0, n) == 0;
}

util::Result<bool>
Table::seek_key(const std::string& key, bool soft, bool last) {
    if (!order_ || !order_->index()) {
        return util::Error{6105, 0, "no active index for seek", ""};
    }
    // GoCold before repositioning via the index.
    // If the dirty record cannot be flushed (shared mode + no lock),
    // discard it silently — DBSEEK is a read operation and must not
    // fail because of a pending edit.  Matches DBFCDX behaviour.
    if (auto cr = commit_dirty_record(); !cr) discard_dirty_();
    auto* idx = order_->index();
    auto r = idx->seek_key(key, soft);
    if (!r) return r.error();
    if (!r.value().positioned) {
        // Failed seek on empty table → Limbo. Otherwise → Eof.
        state_ = (driver_->record_count() == 0) ? State::Limbo
                                                  : State::Eof;
        recno_ = 0;
        last_seek_found_ = false;
        return false;
    }
    bool exact = r.value().hit == drivers::SeekHit::Exact;
    // SET DELETED ON: a seek must not report a deleted row as found.
    // goto_top/goto_bottom/skip already honor show_deleted() but seek_key
    // did not (the B+tree match landed on a deleted row and was returned
    // as found). Skip forward over deleted records at the landing, then
    // re-derive `exact` from the row we actually land on (Clipper/DBFCDX:
    // if the only matching rows are deleted, Found() is .F. and the cursor
    // sits on the next live record or Eof).
    if (!openads::abi::show_deleted_for(this)) {
        bool moved = false;
        while (r.value().positioned) {
            if (auto ld = load_record_(r.value().recno); !ld) return ld.error();
            if (!is_deleted()) break;
            r = idx->next();
            if (!r) return r.error();
            moved = true;
        }
        if (!r.value().positioned) {
            state_ = (driver_->record_count() == 0) ? State::Limbo
                                                    : State::Eof;
            recno_ = 0;
            last_seek_found_ = false;
            return false;
        }
        // Re-derive `exact` ONLY when we skipped past deleted rows: the
        // byte-wise prefix compare is valid for character keys (the only
        // kind a deleted-DBF skip walks over in practice) but wrong for
        // transformed numeric/date ADI keys, where `key` is the caller's
        // raw form and current_key() the index encoding. If the landing
        // row was live, the driver's own hit verdict is already right.
        if (moved) {
            exact = key_prefix_matches(idx->current_key(), key,
                                       idx->key_length());
        }
    }
    // DESCEND order treats the FIRST match in walk direction as
    // the LAST entry in the equal-key group when sorted ASC. Walk
    // duplicates regardless of `last` flag.
    bool walk_to_last = last;
    if (order_->descending_traverse() && exact) walk_to_last = true;
    // SAP-ACE / Clipper "AdsSeekLast" semantics: when fLast and we
    // have an exact hit, walk forward across equal-key entries and
    // stop on the last one. After the walk, idx's cursor is on
    // the last matching entry; load_record_ syncs the table buffer.
    if (walk_to_last && exact) {
        std::uint32_t last_recno = r.value().recno;
        while (true) {
            auto step = idx->next();
            if (!step || !step.value().positioned) break;
            if (!key_prefix_matches(idx->current_key(), key,
                                    idx->key_length())) {
                // Past the equal-key run — step back one to leave
                // cursor on the last matching entry.
                (void)idx->prev();
                break;
            }
            last_recno = step.value().recno;
        }
        auto load = load_record_(last_recno);
        if (!load) return load.error();
        if (!openads::abi::show_deleted_for(this)) {
            while (is_deleted()) {
                auto step = idx->prev();
                if (!step || !step.value().positioned) {
                    state_ = (driver_->record_count() == 0) ? State::Limbo
                                                            : State::Eof;
                    recno_ = 0;
                    last_seek_found_ = false;
                    return false;
                }
                if (!key_prefix_matches(idx->current_key(), key,
                                        idx->key_length())) {
                    state_ = (driver_->record_count() == 0) ? State::Limbo
                                                            : State::Eof;
                    recno_ = 0;
                    last_seek_found_ = false;
                    return false;
                }
                if (auto ld = load_record_(step.value().recno); !ld)
                    return ld.error();
            }
        }
        last_seek_found_ = true;
        return true;
    }
    auto load = load_record_(r.value().recno);
    if (!load) return load.error();
    last_seek_found_ = exact;
    // SOFT seek with no exact match: DBFCDX leaves the cursor positioned on
    // the next-greater key, with Found()=.F. and Eof()=.F. Only a key greater
    // than EVERY entry yields Eof — and that case is already handled above,
    // where the index returns not-positioned. Proven against the native
    // DBFCDX baseline (same .exe): `SET SOFTSEEK ON ; dbSeek( <absent
    // numeric> )` lands on the next key, NOT at Eof. (A prior revision forced
    // Eof here on the strictly-greater landing, contradicting DBFCDX.)
    return exact;
}

util::Result<void> Table::set_scope(bool top, const std::string& key) {
    if (!order_) {
        return util::Error{6105, 0, "no active index for scope", ""};
    }
    if (top) order_->scope().top    = key;
    else     order_->scope().bottom = key;
    return {};
}

util::Result<void> Table::clear_scope(bool top) {
    if (!order_) return {};
    if (top) order_->scope().top.reset();
    else     order_->scope().bottom.reset();
    return {};
}

util::Result<void> Table::clear_scopes() {
    if (!order_) return {};
    order_->scope().top.reset();
    order_->scope().bottom.reset();
    return {};
}

std::optional<std::string> Table::get_scope(bool top) const {
    if (!order_) return std::nullopt;
    return top ? order_->scope().top : order_->scope().bottom;
}

bool Table::is_field_null(std::uint16_t field_idx) {
    if (state_ != State::Positioned) return false;
    const auto& fields = driver_->fields();
    if (field_idx >= fields.size()) return false;

    // M13 — ADT stores NULL as a per-type in-field sentinel.
    if (fields[field_idx].adt) {
        return drivers::adt_field_is_null(fields[field_idx],
                                          record_buf_.data(),
                                          record_buf_.size());
    }

    // M11.6 — VFP: peek the table-wide _NullFlags column for this row,
    // test the bit assigned to `field_idx` during schema parse.
    if (!fields[field_idx].nullable) return false;
    std::int32_t nf_idx = field_index("_NullFlags");
    if (nf_idx < 0) return false;
    const auto& nf = fields[static_cast<std::size_t>(nf_idx)];
    std::uint16_t bit = fields[field_idx].null_bit;
    std::size_t byte_off = nf.record_offset +
                           static_cast<std::size_t>(bit / 8);
    if (byte_off >= record_buf_.size()) return false;
    return (record_buf_[byte_off] & (1u << (bit & 7u))) != 0;
}

bool Table::is_field_empty(std::uint16_t field_idx) {
    if (state_ != State::Positioned) return false;
    const auto& fields = driver_->fields();
    if (field_idx >= fields.size()) return false;
    const auto& f = fields[field_idx];

    // ADT: empty ≡ NULL (AdsIsEmpty "determines if a given field is
    // empty (null)"; the sentinel IS the empty value). A VFP NULL also
    // reports empty.
    if (f.adt) return is_field_null(field_idx);
    if (is_field_null(field_idx)) return true;

    if (static_cast<std::size_t>(f.record_offset) + f.length >
        record_buf_.size()) {
        return false;
    }
    const std::uint8_t* p = record_buf_.data() + f.record_offset;
    // Binary VFP payloads have no space-padded form; their empty value is
    // all-zero bytes. Everything else (Character/Numeric/Date/Logical/...)
    // is empty when blank.
    bool binary_payload =
        f.type == drivers::DbfFieldType::Integer   ||
        f.type == drivers::DbfFieldType::Currency  ||
        f.type == drivers::DbfFieldType::Double    ||
        f.type == drivers::DbfFieldType::DateTime  ||
        f.type == drivers::DbfFieldType::Varchar   ||
        f.type == drivers::DbfFieldType::Varbinary;
    const std::uint8_t blank = binary_payload ? 0x00 : ' ';
    for (std::uint16_t i = 0; i < f.length; ++i) {
        if (p[i] != blank) return false;
    }
    return f.length > 0;
}

util::Result<void> Table::set_field_null(std::uint16_t field_idx) {
    if (state_ != State::Positioned) {
        // rddads special-cases 5068 to blank out at BOF/EOF; see set_field.
        return util::Error{5068, 0, "no record positioned", ""};
    }
    const auto& fields = driver_->fields();
    if (field_idx >= fields.size()) {
        return util::Error{5063, 0, "field index out of range", ""};
    }
    const auto& f = fields[field_idx];
    if (auto d = begin_dirty_edit_(); !d) return d.error();

    if (f.adt) {
        auto r = drivers::encode_field_null(f, record_buf_.data(),
                                            record_buf_.size());
        if (!r) return r.error();
    } else if (type_ == TableType::Vfp) {
        std::int32_t nf_idx = field_index("_NullFlags");
        if (!f.nullable || nf_idx < 0) {
            // 5205 = AE_NOT_VFP_NULLABLE_FIELD (SAP SDK).
            return util::Error{5205, 0,
                "field was not declared nullable", f.name};
        }
        const auto& nf = fields[static_cast<std::size_t>(nf_idx)];
        std::size_t byte_off = nf.record_offset +
                               static_cast<std::size_t>(f.null_bit / 8);
        if (byte_off >= record_buf_.size()) {
            return util::Error{5000, 0, "_NullFlags past record buffer", ""};
        }
        record_buf_[byte_off] |=
            static_cast<std::uint8_t>(1u << (f.null_bit & 7u));
        // The data bytes are undefined while the bit is set; zero them so
        // raw readers (and index key builders) see deterministic content.
        if (static_cast<std::size_t>(f.record_offset) + f.length <=
            record_buf_.size()) {
            std::memset(record_buf_.data() + f.record_offset, 0x00, f.length);
        }
    } else {
        // CDX/NTX have no NULL concept: AdsSetNull is documented to behave
        // identically to AdsSetEmpty — blank the field region.
        if (static_cast<std::size_t>(f.record_offset) + f.length >
            record_buf_.size()) {
            return util::Error{5000, 0, "field range past record buffer", ""};
        }
        std::memset(record_buf_.data() + f.record_offset, ' ', f.length);
    }

    return {};
}

void Table::clear_field_null_(std::uint16_t field_idx) {
    if (type_ != TableType::Vfp) return;
    const auto& fields = driver_->fields();
    if (field_idx >= fields.size()) return;
    const auto& f = fields[field_idx];
    if (!f.nullable) return;
    std::int32_t nf_idx = field_index("_NullFlags");
    if (nf_idx < 0) return;
    const auto& nf = fields[static_cast<std::size_t>(nf_idx)];
    std::size_t byte_off = nf.record_offset +
                           static_cast<std::size_t>(f.null_bit / 8);
    if (byte_off >= record_buf_.size()) return;
    record_buf_[byte_off] &=
        static_cast<std::uint8_t>(~(1u << (f.null_bit & 7u)));
}

// RCB 2026-07-10 — effective per-table OEM collation (#130 follow-up).
// Resolution order, and WHY it must stay this way:
//   1. Explicit AdsSetCollation on the owning connection (OpenADS
//      extension) always wins.
//   2. A table opened with usCharType=ADS_OEM picks up the configured
//      default OEM collation (OPENADS_OEM_COLLATION env var today,
//      adslocal.cfg in Phase 2). This is SAP's zero-config behaviour:
//      rddads apps only pass ADS_OEM at open and expect the machine's
//      OEM collation language to apply — they cannot call
//      AdsSetCollation (no such call exists in rddads).
//   3. Upper only: the legacy process-global published by
//      AdsSetCollation, so evaluations on connection-less tables keep
//      their pre-existing behaviour.
// Reducing this back to "connection override only" re-breaks PL852
// seeks for every OEM app (contributor regression on v1.8.6/v1.8.7).
const std::uint8_t* Table::oem_upper_table() const noexcept {
    if (owner_ != nullptr) {
        if (const std::uint8_t* up = owner_->oem_upper_table()) return up;
    }
    if (char_type_ == 2 /* ADS_OEM */) {
        if (const std::uint8_t* up = default_oem_upper_table()) return up;
    }
    return active_oem_upper_table();
}

const std::uint8_t* Table::oem_sort_table() const noexcept {
    if (owner_ != nullptr) {
        if (const std::uint8_t* st = owner_->oem_sort_table()) return st;
    }
    if (char_type_ == 2 /* ADS_OEM */) {
        if (const OemCollation* c = default_oem_collation()) return c->sort;
    }
    return nullptr;
}

} // namespace openads::engine
