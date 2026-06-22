#include "engine/table.h"

#include "openads/error.h"
#include "engine/index_expr.h"

#include "drivers/adt/adt_driver.h"
#include "drivers/cdx/cdx_driver.h"
#include "drivers/ntx/ntx_driver.h"

#include <algorithm>
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
} // namespace openads::engine

namespace openads::abi { inline bool show_deleted() noexcept {
    return openads::engine::show_deleted();
} }

namespace openads::engine {

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
            return util::Error{5004, 0,
                               "VFP table type not yet supported", path};
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
    // SQL column names may arrive in any case.
    for (std::size_t i = 0; i < fs.size(); ++i) {
        if (fs[i].name.size() != name.size()) continue;
        bool eq = true;
        for (std::size_t j = 0; j < name.size(); ++j) {
            if (std::toupper(static_cast<unsigned char>(name[j])) !=
                std::toupper(static_cast<unsigned char>(fs[i].name[j]))) {
                eq = false; break;
            }
        }
        if (eq) return static_cast<std::int32_t>(i);
    }
    return -1;
}

std::uint32_t Table::record_count() const noexcept {
    return driver_->record_count();
}

util::Result<void> Table::load_record_(std::uint32_t recno) {
    auto buf = driver_->read_record_raw(recno);
    if (!buf) return buf.error();
    record_buf_ = std::move(buf).value();
    recno_      = recno;
    state_      = State::Positioned;
    return {};
}

std::string Table::compute_index_key_(const std::string& expr,
                                      std::uint16_t       key_len) const {
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
std::vector<std::pair<drivers::IIndex*, std::string>>
Table::snapshot_index_keys_() {
    std::vector<std::pair<drivers::IIndex*, std::string>> out;
    auto push = [&](drivers::IIndex* idx) {
        if (idx == nullptr) return;
        out.emplace_back(idx,
            compute_index_key_(idx->expression(), idx->key_length()));
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
    const std::vector<std::pair<drivers::IIndex*, std::string>>& snap) {
    for (auto& [idx, prev_key] : snap) {
        std::string new_key = compute_index_key_(idx->expression(),
                                                 idx->key_length());
        if (prev_key == new_key) continue;
        // Erase prior (recno, prev_key); ignore failure — the index
        // may not have a prior entry (fresh APPEND case).
        if (!prev_key.empty()) {
            (void)idx->erase(recno_, prev_key);
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
    }
    return {};
}

util::Result<void> Table::writeback_record_() {
    if (state_ != State::Positioned) {
        // rddads (Harbour contrib RDD) special-cases 5068 (AE_NO_CURRENT_RECORD)
        // to return blank field values at BOF/EOF; 5026 causes a hard error.
        return util::Error{5068, 0, "no record positioned", ""};
    }
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

bool Table::key_in_top_scope_(const std::string& key) const {
    if (!order_ || !order_->scope().top.has_value()) return true;
    return key >= *order_->scope().top;
}

bool Table::key_in_bottom_scope_(const std::string& key) const {
    if (!order_ || !order_->scope().bottom.has_value()) return true;
    return key <= *order_->scope().bottom;
}

void Table::set_recno_sequence(std::vector<std::uint32_t> seq) {
    recno_sequence_ = std::move(seq);
    sequence_idx_   = -1;
}

util::Result<void> Table::goto_top() {
    // Empty table → Limbo regardless of active order / sequence.
    if (driver_->record_count() == 0) {
        state_ = State::Limbo; recno_ = 0; return {};
    }
    if (!recno_sequence_.empty()) {
        sequence_idx_ = 0;
        std::uint32_t r = recno_sequence_.front();
        return load_record_(r);
    }
    if (order_ && order_->index()) {
        auto* idx = order_->index();
        util::Result<drivers::SeekOutcome> r = drivers::SeekOutcome{};
        if (order_->descending_traverse()) {
            r = idx->seek_last();
        } else if (order_->scope().top.has_value()) {
            r = idx->seek_key(*order_->scope().top, true);
        } else {
            r = idx->seek_first();
        }
        if (!r) return r.error();
        if (!r.value().positioned) {
            state_ = State::Limbo; recno_ = 0; return {};
        }
        if (!key_in_bottom_scope_(idx->current_key())) {
            state_ = State::Eof; recno_ = 0; return {};
        }
        // SET DELETE ON: skip deleted rows in the walk direction.
        // If everything's deleted (or filtered out by the index +
        // delete combo) → Limbo so DBGOTOP reports BOF+EOF both
        // true (Clipper / DBFCDX convention for "no visible row").
        if (!openads::abi::show_deleted()) {
            while (r.value().positioned) {
                if (auto ld = load_record_(r.value().recno); !ld) {
                    return ld.error();
                }
                if (!is_deleted()) return {};
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
        return load_record_(r.value().recno);
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
    if (!openads::abi::show_deleted()) {
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
    // Empty table → Limbo regardless of active order / sequence.
    if (driver_->record_count() == 0) {
        state_ = State::Limbo; recno_ = 0; return {};
    }
    if (!recno_sequence_.empty()) {
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
        if (!openads::abi::show_deleted()) {
            while (r.value().positioned) {
                if (auto ld = load_record_(r.value().recno); !ld) {
                    return ld.error();
                }
                if (!is_deleted()) {
                    return {};
                }
                r = idx->prev();
                if (!r) return r.error();
            }
            state_ = State::Limbo; recno_ = 0; return {};
        }
        return load_record_(r.value().recno);
    }
    auto n = driver_->record_count();
    if (n == 0) {
        state_ = State::Limbo; recno_ = 0; return {};
    }
    if (auto r = load_record_(n); !r) return r.error();
    // SET DELETE ON without active index: walk back to first live.
    if (!openads::abi::show_deleted()) {
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
        std::string key = compute_index_key_(idx->expression(),
                                             idx->key_length());
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
        }
    }
    return {};
}

util::Result<void> Table::skip(std::int32_t delta) {
    if (!recno_sequence_.empty()) {
        if (delta == 0) return {};
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
        const bool skip_deleted = !openads::abi::show_deleted();
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
                if (auto ld = load_record_(r.value().recno); !ld) {
                    return ld.error();
                }
                if (is_deleted()) continue;
            }
            last_live = r.value().recno;
            ++taken;
        }
        return load_record_(r.value().recno);
    }
    auto n = driver_->record_count();
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
    std::int64_t target = static_cast<std::int64_t>(recno_) + delta;
    if (state_ == State::Bof && delta > 0) target = delta;
    if (target < 1) { state_ = State::Bof; recno_ = 0; return {}; }
    if (target > static_cast<std::int64_t>(n)) {
        state_ = State::Eof; recno_ = n + 1; return {};
    }
    if (auto r = load_record_(static_cast<std::uint32_t>(target)); !r) {
        return r.error();
    }
    // SET DELETE ON (no active index) and/or an active filter hide rows
    // from the natural-order walk: step in the skip direction until a
    // visible row appears or we run off either end. goto_top/goto_bottom
    // already do this for deleted rows (576746e); skip was overlooked.
    const bool skip_deleted = !openads::abi::show_deleted();
    if (skip_deleted || filter_) {
        std::int64_t step = (delta >= 0) ? 1 : -1;
        auto hidden = [&]() {
            return (skip_deleted && is_deleted()) ||
                   (filter_ && !filter_(*this));
        };
        while (state_ == State::Positioned && hidden()) {
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
    if (mode_ == OpenMode::Read) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    auto rec = drivers::make_empty_record(driver_->record_length());

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
    if (tx_ && tx_->active()) {
        tx_->note_append(tid_, recno_);
    }
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

    auto snap = snapshot_index_keys_();

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
        if (auto wb = writeback_record_(); !wb) return wb.error();
        return sync_all_indexes_(snap);
    }

    auto r = drivers::encode_field_string(f, record_buf_.data(),
                                          record_buf_.size(), v);
    if (!r) return r.error();
    if (auto wb = writeback_record_(); !wb) return wb.error();
    return sync_all_indexes_(snap);
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
    auto snap = snapshot_index_keys_();
    auto r = drivers::encode_field_double(driver_->fields().at(idx),
                                          record_buf_.data(),
                                          record_buf_.size(), v);
    if (!r) return r.error();
    if (auto wb = writeback_record_(); !wb) return wb.error();
    return sync_all_indexes_(snap);
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
    auto snap = snapshot_index_keys_();
    auto r = drivers::encode_field_logical(driver_->fields().at(idx),
                                           record_buf_.data(),
                                           record_buf_.size(), v);
    if (!r) return r.error();
    if (auto wb = writeback_record_(); !wb) return wb.error();
    return sync_all_indexes_(snap);
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
    auto snap = snapshot_index_keys_();
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
    if (auto wb = writeback_record_(); !wb) return wb.error();
    return sync_all_indexes_(snap);
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

util::Result<void> Table::mark_deleted() {
    if (state_ != State::Positioned) {
        // rddads (Harbour contrib RDD) special-cases 5068 (AE_NO_CURRENT_RECORD)
        // to return blank field values at BOF/EOF; 5026 causes a hard error.
        return util::Error{5068, 0, "no record positioned", ""};
    }
    drivers::set_record_deleted(record_buf_.data(), record_buf_.size(), true);
    return writeback_record_();
}

util::Result<void> Table::recall_deleted() {
    if (state_ != State::Positioned) {
        // rddads (Harbour contrib RDD) special-cases 5068 (AE_NO_CURRENT_RECORD)
        // to return blank field values at BOF/EOF; 5026 causes a hard error.
        return util::Error{5068, 0, "no record positioned", ""};
    }
    drivers::set_record_deleted(record_buf_.data(), record_buf_.size(), false);
    return writeback_record_();
}

bool Table::is_deleted() const noexcept {
    if (state_ != State::Positioned) return false;
    return drivers::record_is_deleted(record_buf_.data(), record_buf_.size());
}

util::Result<void> Table::zap() {
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
                std::string key = compute_index_key_(idx->expression(),
                                                     idx->key_length());
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
        if (auto g = goto_record(src); !g) return g.error();
        if (is_deleted()) continue;
        ++dst;
        if (dst != src) {
            if (auto w = driver_->write_record_raw(dst, record_buf_.data(),
                                                   record_buf_.size()); !w) {
                return w.error();
            }
        }
    }
    // 2) Truncate the on-disk record count to `dst` by saving the
    //    survivors, zapping the driver (DBF-only — does NOT touch
    //    bound indexes), and re-appending. Pack matches Clipper's
    //    semantics: indexes are left stale, the caller must REINDEX.
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
    state_ = State::Bof;
    recno_ = 0;
    record_buf_.assign(driver_->record_length(), 0);
    return {};
}

util::Result<void> Table::reindex() {
    if (mode_ == OpenMode::Read) {
        return util::Error{5000, 0, "table opened read-only", ""};
    }
    if (driver_ == nullptr) return {};

    // 1) Clear every bound index. Re-using the same erase walk that
    //    zap() uses keeps the IIndex file structurally intact.
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
        return {};
    };
    if (order_ && order_->index()) {
        if (auto r = erase_all(order_->index()); !r) return r.error();
    }
    for (auto* x : extra_index_views_) {
        if (auto r = erase_all(x); !r) return r.error();
    }

    // 2) Walk every live record and re-insert into each index using
    //    its current key expression. Snapshot is built with empty
    //    prev_keys so sync_all_indexes_ skips the (unneeded) erase
    //    pass and performs only the insert.
    std::vector<std::pair<drivers::IIndex*, std::string>> snap;
    if (order_ && order_->index()) snap.emplace_back(order_->index(),
                                                     std::string{});
    for (auto* x : extra_index_views_) {
        if (x) snap.emplace_back(x, std::string{});
    }
    auto rec_count = driver_->record_count();
    for (std::uint32_t r = 1; r <= rec_count; ++r) {
        if (auto g = goto_record(r); !g) return g.error();
        if (is_deleted()) continue;
        if (auto s = sync_all_indexes_(snap); !s) return s.error();
    }

    // 3) Flush every index so the rebuilt entries hit disk before the
    //    caller resumes work.
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
    auto h = locks_.lock_record_excl(driver_->file(), to_lock_type_(),
                                     locking_, recno);
    if (!h) return h.error();
    recno_locks_.emplace(recno, std::move(h).value());
    return {};
}

util::Result<void> Table::try_lock_record_excl(std::uint32_t recno) {
    if (mode_ == OpenMode::Read) return {};
    auto h = locks_.try_lock_record_excl(driver_->file(), to_lock_type_(),
                                         locking_, recno);
    if (!h) return h.error();
    recno_locks_.emplace(recno, std::move(h).value());
    return {};
}

util::Result<void> Table::unlock_record(std::uint32_t recno) {
    auto it = recno_locks_.find(recno);
    if (it != recno_locks_.end()) {
        it->second.release();
        recno_locks_.erase(it);
        locks_.unlock_record(driver_->file(), to_lock_type_(), locking_, recno);
    }
    return {};
}

util::Result<void> Table::lock_table_excl() {
    if (mode_ == OpenMode::Read) return {};
    auto h = locks_.lock_table_excl(driver_->file(), to_lock_type_(), locking_);
    if (!h) return h.error();
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
    auto h = locks_.try_lock_table_excl(driver_->file(), to_lock_type_(),
                                        locking_);
    if (!h) return h.error();
    table_lock_ = std::move(h).value();
    return {};
}

util::Result<void> Table::unlock_table() {
    if (table_lock_) {
        table_lock_->release();
        table_lock_.reset();
        locks_.unlock_table(driver_->file(), to_lock_type_(), locking_);
    }
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

util::Result<bool>
Table::seek_key(const std::string& key, bool soft, bool last) {
    if (!order_ || !order_->index()) {
        return util::Error{6105, 0, "no active index for seek", ""};
    }
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
    if (!openads::abi::show_deleted()) {
        std::string del_key = key;
        del_key.resize(idx->key_length(), ' ');
        while (r.value().positioned) {
            if (auto ld = load_record_(r.value().recno); !ld) return ld.error();
            if (!is_deleted()) break;
            r = idx->next();
            if (!r) return r.error();
        }
        if (!r.value().positioned) {
            state_ = (driver_->record_count() == 0) ? State::Limbo
                                                    : State::Eof;
            recno_ = 0;
            last_seek_found_ = false;
            return false;
        }
        std::string ck = idx->current_key();
        ck.resize(del_key.size(), ' ');
        exact = (ck == del_key);
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
        std::string padded_key = key;
        padded_key.resize(idx->key_length(), ' ');
        std::uint32_t last_recno = r.value().recno;
        while (true) {
            auto step = idx->next();
            if (!step || !step.value().positioned) break;
            std::string ck = idx->current_key();
            ck.resize(padded_key.size(), ' ');
            if (ck != padded_key) {
                // Past the equal-key run — step back one to leave
                // cursor on the last matching entry.
                (void)idx->prev();
                break;
            }
            last_recno = step.value().recno;
        }
        auto load = load_record_(last_recno);
        if (!load) return load.error();
        if (!openads::abi::show_deleted()) {
            while (is_deleted()) {
                auto step = idx->prev();
                if (!step || !step.value().positioned) {
                    state_ = (driver_->record_count() == 0) ? State::Limbo
                                                            : State::Eof;
                    recno_ = 0;
                    last_seek_found_ = false;
                    return false;
                }
                std::string ck = idx->current_key();
                ck.resize(padded_key.size(), ' ');
                if (ck != padded_key) {
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
    // Clipper 5.2 / DBFCDX-baseline quirk: when a SOFT seek lands on
    // a record whose key is strictly greater than the searched key
    // (i.e. we positioned BEFORE the first equal-key entry — there
    // is no equal-key entry at all), the baseline reports EOF
    // instead of "positioned at next-greater". Reproduce that by
    // probing the loaded record's key vs the search key.
    if (!exact && soft && !last) {
        std::string padded = key;
        if (padded.size() < idx->key_length())
            padded.append(idx->key_length() - padded.size(), ' ');
        if (padded.size() > idx->key_length())
            padded.resize(idx->key_length());
        std::string ck = idx->current_key();
        if (ck.size() < padded.size())
            ck.append(padded.size() - ck.size(), ' ');
        if (ck.size() > padded.size()) ck.resize(padded.size());
        if (std::memcmp(padded.data(), ck.data(), padded.size()) < 0) {
            state_ = State::Eof; recno_ = 0;
            last_seek_found_ = false;
            return false;
        }
    }
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
    // M11.6 — peek the table-wide _NullFlags column for this row,
    // test the bit assigned to `field_idx` during schema parse.
    if (state_ != State::Positioned) return false;
    const auto& fields = driver_->fields();
    if (field_idx >= fields.size()) return false;
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

} // namespace openads::engine
