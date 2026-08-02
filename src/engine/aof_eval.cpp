// M-AOF.2 — evaluator that turns an AOF AST into a per-record
// bitmap. The V1 path is a full table scan: for every recno in
// [1, record_count()] decode the relevant fields once, evaluate the
// AST, and set the bit if the AST returns true. M-AOF.4 will short-
// circuit individual leaves through CDX/NTX index range scans
// without changing this file's public contract.

#include "engine/aof_eval.h"
#include "engine/table.h"
#include "drivers/dbf_common.h"
#include "drivers/driver_trait.h"
#include "drivers/index_trait.h"
#include "openads/error.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace openads::engine::aof {

namespace {

// Trim trailing ASCII spaces. CDX-style character indexes are
// right-padded; raw DBF reads come back left-aligned with trailing
// spaces. AOF leaves operate on the logical value, so we strip
// padding before comparing.
std::string rtrim(std::string s) {
    while (!s.empty() &&
           static_cast<unsigned char>(s.back()) <= ' ') {
        s.pop_back();
    }
    return s;
}

// SQL LIKE: % = any sequence, _ = one char (same semantics as M10.33).
bool sql_like_match(const std::string& s, const std::string& pat) {
    std::size_t si = 0, pi = 0;
    std::size_t star = std::string::npos, ss = 0;
    while (si < s.size()) {
        if (pi < pat.size() &&
            (pat[pi] == '_' || pat[pi] == s[si])) {
            ++si; ++pi;
        } else if (pi < pat.size() && pat[pi] == '%') {
            star = pi++;
            ss   = si;
        } else if (star != std::string::npos) {
            pi = star + 1;
            si = ++ss;
        } else {
            return false;
        }
    }
    while (pi < pat.size() && pat[pi] == '%') ++pi;
    return pi == pat.size();
}

std::string value_as_string(const Value& lit) {
    if (auto p = std::get_if<std::string>(&lit)) return *p;
    if (auto p = std::get_if<std::int64_t>(&lit)) return std::to_string(*p);
    if (auto p = std::get_if<double>(&lit))      return std::to_string(*p);
    return {};
}

// Compare a decoded field value against a literal Value. Returns -1
// / 0 / +1 like strcmp. Coerces both sides into the field's
// "natural" domain (string for C/M, double for N/F/I/B/Y/D, bool
// for L). String comparisons are byte-lexicographic so that they
// match the CDX / NTX key ordering once we route this through the
// index in M-AOF.4.
int cmp_field_to_literal(const drivers::DbfField&     fld,
                         const drivers::DbfFieldValue& v,
                         const Value& lit) {
    using drivers::DbfFieldType;
    auto numeric_lit = [&]() -> double {
        if (auto p = std::get_if<std::int64_t>(&lit)) return static_cast<double>(*p);
        if (auto p = std::get_if<double>(&lit))      return *p;
        if (auto p = std::get_if<std::string>(&lit)) {
            try { return std::stod(*p); } catch (...) { return 0.0; }
        }
        return 0.0;
    };
    auto string_lit = [&]() -> std::string {
        if (auto p = std::get_if<std::string>(&lit)) return *p;
        if (auto p = std::get_if<std::int64_t>(&lit)) return std::to_string(*p);
        if (auto p = std::get_if<double>(&lit))      return std::to_string(*p);
        return {};
    };

    switch (fld.type) {
        case DbfFieldType::Character:
        case DbfFieldType::Memo:
        case DbfFieldType::Varchar:
        case DbfFieldType::Date:
        case DbfFieldType::DateTime: {
            std::string lhs = rtrim(v.as_string);
            std::string rhs = string_lit();
            if (lhs < rhs) return -1;
            if (lhs > rhs) return  1;
            return 0;
        }
        case DbfFieldType::Numeric:
        case DbfFieldType::Float:
        case DbfFieldType::Integer:
        case DbfFieldType::Currency:
        case DbfFieldType::Double: {
            double lhs = v.as_double;
            double rhs = numeric_lit();
            if (lhs < rhs) return -1;
            if (lhs > rhs) return  1;
            return 0;
        }
        case DbfFieldType::Logical: {
            bool lhs = v.as_bool;
            bool rhs = false;
            if (auto pi = std::get_if<std::int64_t>(&lit)) rhs = (*pi != 0);
            else if (auto pd = std::get_if<double>(&lit))  rhs = (*pd != 0.0);
            else if (auto ps = std::get_if<std::string>(&lit)) {
                if (!ps->empty()) {
                    char c = static_cast<char>(
                        std::toupper(static_cast<unsigned char>((*ps)[0])));
                    rhs = (c == 'T' || c == 'Y' || c == '1');
                }
            }
            if (lhs == rhs) return 0;
            return lhs ? 1 : -1;
        }
        default:
            return 0;
    }
}

bool eval_leaf(const Leaf& leaf, Table& t) {
    auto idx = t.field_index(leaf.field);
    if (idx < 0) return false;                  // unknown field → never match
    auto val = t.read_field(static_cast<std::uint16_t>(idx));
    if (!val) return false;
    const auto& fld = t.driver()->fields().at(static_cast<std::size_t>(idx));
    const drivers::DbfFieldValue& v = val.value();

    switch (leaf.op) {
        case Op::Eq: return cmp_field_to_literal(fld, v, leaf.values[0]) == 0;
        case Op::Ne: return cmp_field_to_literal(fld, v, leaf.values[0]) != 0;
        case Op::Lt: return cmp_field_to_literal(fld, v, leaf.values[0]) <  0;
        case Op::Le: return cmp_field_to_literal(fld, v, leaf.values[0]) <= 0;
        case Op::Gt: return cmp_field_to_literal(fld, v, leaf.values[0]) >  0;
        case Op::Ge: return cmp_field_to_literal(fld, v, leaf.values[0]) >= 0;
        case Op::Between:
            return cmp_field_to_literal(fld, v, leaf.values[0]) >= 0 &&
                   cmp_field_to_literal(fld, v, leaf.values[1]) <= 0;
        case Op::In:
            for (auto& lit : leaf.values) {
                if (cmp_field_to_literal(fld, v, lit) == 0) return true;
            }
            return false;
        case Op::Like:
            if (leaf.values.empty()) return false;
            return sql_like_match(rtrim(v.as_string),
                                  value_as_string(leaf.values[0]));
        case Op::IsNull:
            return v.is_null;
        case Op::IsNotNull:
            return !v.is_null;
    }
    return false;
}

bool eval_node(const Node& n, Table& t) {
    if (auto p = std::get_if<Leaf>(&n.v)) return eval_leaf(*p, t);
    if (auto p = std::get_if<And>(&n.v)) {
        for (auto& k : p->kids) if (!eval_node(*k, t)) return false;
        return true;
    }
    if (auto p = std::get_if<Or>(&n.v)) {
        for (auto& k : p->kids) if (eval_node(*k, t)) return true;
        return false;
    }
    if (auto p = std::get_if<Not>(&n.v)) {
        return !eval_node(*p->child, t);
    }
    return false;
}

} // namespace

bool evaluate_record(const Node& n, Table& t) {
    return eval_node(n, t);
}

util::Result<Bitmap> evaluate(const Node& n, Table& t) {
    auto rc = t.record_count();
    Bitmap bm;
    bm.assign(rc, false);
    for (std::uint32_t r = 1; r <= rc; ++r) {
        auto g = t.goto_record(r);
        if (!g) return g.error();
        bm[r - 1] = eval_node(n, t);
    }
    return bm;
}

// =====================================================================
// M-AOF.4 — index-accelerated leaf evaluation.
//
// For each leaf we look up an open index (active order + extra views)
// whose key expression is a bare reference to the same field, then
// turn the leaf's predicate into a range scan over that index. The
// scan walks the index entries directly and writes recnos into the
// bitmap, avoiding the per-record decode + comparison cost of the
// full-scan path.
//
// V1 surface served by index:
//   * Character / Memo fields only (key encoding is right-padded
//     bytes, identical to the value the index already stores).
//   * Operators: Eq, Ne, Lt, Le, Gt, Ge, Between, In.
//   * Index expression must equal the field name case-insensitively.
//     UPPER(field) / DTOC(field) / compound expressions stay on the
//     full-scan fallback for now — adding them is a matcher-only
//     change downstream (no API impact).
//
// Numeric / Logical / Date fields fall back to per-record leaf
// evaluation. They still produce a correct bitmap, just without the
// index speedup, and the leaf is then counted as "not served by
// index" so AdsGetAOFOptLevel correctly reports PART or NONE.
// =====================================================================
namespace {

std::string upper(std::string s) {
    for (auto& c : s) {
        c = static_cast<char>(
            std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

drivers::IIndex* find_index_for_field(Table& t, const std::string& field) {
    std::string want = upper(field);
    for (auto* idx : t.all_indexes()) {
        if (idx == nullptr) continue;
        // A CONDITIONAL tag (FOR clause) holds only the rows that satisfy
        // its own predicate, so a range scan over it answers "rows matching
        // the leaf AND the FOR clause" — the leaf's own bitmap would come
        // back missing every row the tag excludes, silently. Opening a .dbf
        // auto-binds every tag in the production .cdx (M-AOF.6), so such a
        // tag reaches all_indexes() without the caller asking for it. Leave
        // those leaves on the full-scan path.
        if (!idx->condition().empty()) continue;
        std::string e = upper(idx->expression());
        if (e == want) return idx;
    }
    return nullptr;
}

std::string pad_key(std::string v, std::uint16_t klen) {
    if (v.size() < klen) v.append(klen - v.size(), ' ');
    if (v.size() > klen) v.resize(klen);
    return v;
}

// Encode a Value as a CDX/NTX-compatible character key. Returns
// std::nullopt when the literal is not a string or coerceable to one
// — in V1 we restrict the index path to character fields, so non-
// string literals fall through to the full-scan path.
std::optional<std::string>
encode_char_key(const Value& v, std::uint16_t klen) {
    auto p = std::get_if<std::string>(&v);
    if (p == nullptr) return std::nullopt;
    // The two evaluation paths normalise differently: the full scan compares
    // rtrim(field) against the literal verbatim, the index path compares
    // space-padded keys. They agree only while the literal fits inside the
    // key and carries no trailing blank of its own. A WIDER literal would be
    // truncated by pad_key — `CLI = "0001XX"` on a C(6) field is false for
    // every row, but truncated to "0001  " it matches. A blank-tailed
    // literal diverges the other way: rtrim(field) never ends in a blank, so
    // the scan path says no where the padded compare said yes. The bitmap is
    // authoritative (nothing re-checks it per record), so in both cases fall
    // back to the scan instead of answering from the index.
    if (p->size() > klen) return std::nullopt;
    if (!p->empty() && p->back() == ' ') return std::nullopt;
    return pad_key(*p, klen);
}

// Drive a seek + walk loop: starting from `start`, advance with
// `step` while `keep` returns true on the visited (recno, key) pair.
// Each visited record sets bm[recno-1] when recno is in [1, rc].
util::Result<void>
range_walk(drivers::IIndex& idx, Bitmap& bm, std::uint32_t rc,
           drivers::SeekOutcome start,
           const std::function<bool(std::uint32_t, const std::string&)>& keep,
           bool forward) {
    auto outcome = start;
    while (outcome.positioned) {
        std::uint32_t r = outcome.recno;
        std::string   k = idx.current_key();
        if (!keep(r, k)) break;
        if (r >= 1 && r <= rc) bm[r - 1] = true;

        auto nxt = forward ? idx.next() : idx.prev();
        if (!nxt) return nxt.error();
        outcome = nxt.value();
    }
    return {};
}

// Try to serve a single Eq / Ne / Lt / Le / Gt / Ge / Between / In
// leaf via an index range scan. Returns std::nullopt when the leaf
// cannot be served (no matching index, non-character field, …). On
// success returns the leaf's bitmap.
std::optional<util::Result<Bitmap>>
serve_leaf_via_index(const Leaf& leaf, Table& t) {
    std::int32_t fidx = t.field_index(leaf.field);
    if (fidx < 0) return std::nullopt;
    const auto& fld = t.driver()->fields().at(static_cast<std::size_t>(fidx));
    if (fld.type != drivers::DbfFieldType::Character &&
        fld.type != drivers::DbfFieldType::Memo) {
        return std::nullopt;
    }

    drivers::IIndex* idx = find_index_for_field(t, leaf.field);
    if (idx == nullptr) return std::nullopt;
    std::uint16_t klen = idx->key_length();
    auto rc = t.record_count();

    Bitmap bm;
    bm.assign(rc, false);

    auto eq_scan = [&](const std::string& key) -> util::Result<void> {
        auto seek = idx->seek_key(key, /*soft=*/false);
        if (!seek) return seek.error();
        if (seek.value().hit != drivers::SeekHit::Exact) return {};
        return range_walk(*idx, bm, rc, seek.value(),
            [&](std::uint32_t /*r*/, const std::string& k) {
                return k == key;
            }, /*forward=*/true);
    };

    auto le_scan = [&](const std::string& key) -> util::Result<void> {
        auto seek = idx->seek_first();
        if (!seek) return seek.error();
        return range_walk(*idx, bm, rc, seek.value(),
            [&](std::uint32_t /*r*/, const std::string& k) {
                return k <= key;
            }, /*forward=*/true);
    };

    auto ge_scan = [&](const std::string& key) -> util::Result<void> {
        auto seek = idx->seek_key(key, /*soft=*/true);
        if (!seek) return seek.error();
        if (!seek.value().positioned) return {};
        return range_walk(*idx, bm, rc, seek.value(),
            [&](std::uint32_t /*r*/, const std::string& /*k*/) {
                return true;
            }, /*forward=*/true);
    };

    auto between_scan = [&](const std::string& lo,
                             const std::string& hi) -> util::Result<void> {
        auto seek = idx->seek_key(lo, /*soft=*/true);
        if (!seek) return seek.error();
        if (!seek.value().positioned) return {};
        return range_walk(*idx, bm, rc, seek.value(),
            [&](std::uint32_t /*r*/, const std::string& k) {
                return k <= hi;
            }, /*forward=*/true);
    };

    switch (leaf.op) {
        case Op::Eq: {
            auto k = encode_char_key(leaf.values[0], klen);
            if (!k) return std::nullopt;
            if (auto r = eq_scan(*k); !r) return util::Result<Bitmap>{r.error()};
            return util::Result<Bitmap>{std::move(bm)};
        }
        case Op::Le: {
            auto k = encode_char_key(leaf.values[0], klen);
            if (!k) return std::nullopt;
            if (auto r = le_scan(*k); !r) return util::Result<Bitmap>{r.error()};
            return util::Result<Bitmap>{std::move(bm)};
        }
        case Op::Lt: {
            // strict less: walk while key < value (i.e. key <= prev-of-value
            // in lex order — equivalent to key != value AND key <= value).
            auto k = encode_char_key(leaf.values[0], klen);
            if (!k) return std::nullopt;
            auto seek = idx->seek_first();
            if (!seek) return util::Result<Bitmap>{seek.error()};
            auto rr = range_walk(*idx, bm, rc, seek.value(),
                [&](std::uint32_t /*r*/, const std::string& kk) {
                    return kk < *k;
                }, /*forward=*/true);
            if (!rr) return util::Result<Bitmap>{rr.error()};
            return util::Result<Bitmap>{std::move(bm)};
        }
        case Op::Ge: {
            auto k = encode_char_key(leaf.values[0], klen);
            if (!k) return std::nullopt;
            if (auto r = ge_scan(*k); !r) return util::Result<Bitmap>{r.error()};
            return util::Result<Bitmap>{std::move(bm)};
        }
        case Op::Gt: {
            auto k = encode_char_key(leaf.values[0], klen);
            if (!k) return std::nullopt;
            auto seek = idx->seek_key(*k, /*soft=*/true);
            if (!seek) return util::Result<Bitmap>{seek.error()};
            // skip the equal block first.
            auto cur = seek.value();
            while (cur.positioned && idx->current_key() == *k) {
                auto nx = idx->next();
                if (!nx) return util::Result<Bitmap>{nx.error()};
                cur = nx.value();
            }
            auto rr = range_walk(*idx, bm, rc, cur,
                [&](std::uint32_t /*r*/, const std::string& /*kk*/) {
                    return true;
                }, /*forward=*/true);
            if (!rr) return util::Result<Bitmap>{rr.error()};
            return util::Result<Bitmap>{std::move(bm)};
        }
        case Op::Between: {
            auto lo = encode_char_key(leaf.values[0], klen);
            auto hi = encode_char_key(leaf.values[1], klen);
            if (!lo || !hi) return std::nullopt;
            if (auto r = between_scan(*lo, *hi); !r) {
                return util::Result<Bitmap>{r.error()};
            }
            return util::Result<Bitmap>{std::move(bm)};
        }
        case Op::In: {
            for (auto& lit : leaf.values) {
                auto k = encode_char_key(lit, klen);
                if (!k) return std::nullopt;
                if (auto r = eq_scan(*k); !r) {
                    return util::Result<Bitmap>{r.error()};
                }
            }
            return util::Result<Bitmap>{std::move(bm)};
        }
        case Op::Ne:
            // !=  is "not (equality)" — easier to compute via equality
            // scan + invert at the leaf level.
            {
                auto k = encode_char_key(leaf.values[0], klen);
                if (!k) return std::nullopt;
                Bitmap eq_bm;
                eq_bm.assign(rc, false);
                Bitmap save = std::move(bm);  // unused; reset below
                (void)save;
                {
                    auto seek = idx->seek_key(*k, /*soft=*/false);
                    if (!seek) return util::Result<Bitmap>{seek.error()};
                    if (seek.value().hit == drivers::SeekHit::Exact) {
                        auto rr = range_walk(*idx, eq_bm, rc, seek.value(),
                            [&](std::uint32_t /*r*/, const std::string& kk) {
                                return kk == *k;
                            }, /*forward=*/true);
                        if (!rr) return util::Result<Bitmap>{rr.error()};
                    }
                }
                // Final !=  bitmap: every existing recno that isn't in
                // the equality set. Walking the index also yields
                // "exists" signals; rather than re-walking, mark every
                // recno [1..rc] as candidate then strip the eq matches.
                bm.assign(rc, true);
                for (std::uint32_t i = 0; i < rc; ++i) {
                    if (eq_bm[i]) bm[i] = false;
                }
                return util::Result<Bitmap>{std::move(bm)};
            }
        case Op::Like:
        case Op::IsNull:
        case Op::IsNotNull:
            // V2 predicates — no index-range shortcut; caller full-scans.
            return std::nullopt;
    }
    return std::nullopt;
}

struct LeafCounter {
    std::uint32_t total      = 0;
    std::uint32_t served_idx = 0;
};

util::Result<Bitmap>
eval_optimised_node(const Node& n, Table& t, LeafCounter& lc) {
    if (auto leaf = std::get_if<Leaf>(&n.v)) {
        ++lc.total;
        // Try the index path first.
        auto opt = serve_leaf_via_index(*leaf, t);
        if (opt.has_value()) {
            ++lc.served_idx;
            return std::move(*opt);
        }
        // Full-scan fallback for this leaf.
        Bitmap bm;
        bm.assign(t.record_count(), false);
        for (std::uint32_t r = 1; r <= t.record_count(); ++r) {
            auto g = t.goto_record(r);
            if (!g) return g.error();
            bm[r - 1] = eval_leaf(*leaf, t);
        }
        return bm;
    }
    if (auto a = std::get_if<And>(&n.v)) {
        Bitmap acc;
        bool first = true;
        for (auto& k : a->kids) {
            auto rb = eval_optimised_node(*k, t, lc);
            if (!rb) return rb;
            if (first) { acc = std::move(rb).value(); first = false; }
            else {
                auto& other = rb.value();
                if (other.size() != acc.size()) {
                    return util::Error{
                        static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                        0, "AOF AND: bitmap size mismatch", {}};
                }
                for (std::size_t i = 0; i < acc.size(); ++i) {
                    if (!other[i]) acc[i] = false;
                }
            }
        }
        if (first) acc.assign(t.record_count(), true);
        return acc;
    }
    if (auto o = std::get_if<Or>(&n.v)) {
        Bitmap acc;
        acc.assign(t.record_count(), false);
        for (auto& k : o->kids) {
            auto rb = eval_optimised_node(*k, t, lc);
            if (!rb) return rb;
            auto& other = rb.value();
            if (other.size() != acc.size()) {
                return util::Error{
                    static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
                    0, "AOF OR: bitmap size mismatch", {}};
            }
            for (std::size_t i = 0; i < acc.size(); ++i) {
                if (other[i]) acc[i] = true;
            }
        }
        return acc;
    }
    if (auto nt = std::get_if<Not>(&n.v)) {
        auto rb = eval_optimised_node(*nt->child, t, lc);
        if (!rb) return rb;
        Bitmap inv = std::move(rb).value();
        for (std::size_t i = 0; i < inv.size(); ++i) inv[i] = !inv[i];
        return inv;
    }
    return util::Error{
        static_cast<std::int32_t>(openads::AE_INTERNAL_ERROR),
        0, "AOF eval: unknown node kind", {}};
}

} // namespace

util::Result<EvalReport> evaluate_optimised(const Node& n, Table& t) {
    LeafCounter lc;
    auto rb = eval_optimised_node(n, t, lc);
    if (!rb) return rb.error();
    EvalReport r;
    r.bm = std::move(rb).value();
    if      (lc.total == 0)              r.level = OptLevel::None;
    else if (lc.served_idx == lc.total)  r.level = OptLevel::Full;
    else if (lc.served_idx == 0)         r.level = OptLevel::None;
    else                                 r.level = OptLevel::Part;
    return r;
}

} // namespace openads::engine::aof
