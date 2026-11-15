#pragma once

// M-AOF.1 — parser + AST for the filter-expression subset OpenADS
// can serve through Advantage Optimized Filters (AOF). The ABI
// surface (`AdsSetAOF`, `AdsGetAOFOptLevel`, `AdsClearAOF`) takes a
// Clipper-flavoured expression string; this header turns that string
// into a small AST so the rest of the AOF pipeline (tag matcher in
// M-AOF.2, bitmap combinator in M-AOF.3) can operate on a structured
// representation rather than re-scanning text on every leaf.
//
// Provenance / clean-room note. The grammar implemented here was
// designed from the public Harbour `contrib/rddads/ads1.c` (the call
// site that hands AOF strings to the engine, Apache-2.0) plus the
// publicly documented behaviour of `AdsSetAOF` from the original
// ADS documentation. No GPL-licensed Harbour `contrib/rddbm/bmdbfx.c`
// internals were read while authoring this file.
//
// Subset supported in V2:
//   <field> OP <literal>     OP in { = == != <> < <= > >= }
//   <field> BETWEEN a AND b
//   <field> IN ( v1, v2, ... )
//   <field> LIKE 'pattern'   (V2: % and _ wildcards)
//   <field> IS NULL           (V2)
//   <field> IS NOT NULL       (V2)
//   <expr> AND <expr>        also `.AND.` (Clipper)
//   <expr> OR  <expr>        also `.OR.`
//   NOT <expr>               also `.NOT.` and `!`
//   ( <expr> )
//
// Out of scope V2 (deliberately): function calls, arithmetic on
// fields, DESCEND-only sub-expressions.
// Anything we cannot parse falls back to "the whole AOF is a no-op
// and AdsGetAOFOptLevel returns ADS_OPTIMIZED_NONE", matching the
// pre-AOF behaviour exactly.

#include "util/result.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace openads::engine::aof {

enum class Op {
    Eq,    // =, ==
    Ne,    // !=, <>, #
    Lt,    // <
    Le,    // <=
    Gt,    // >
    Ge,    // >=
    Between,
    In,
    Like,       // V2: LIKE 'pattern' (% and _ wildcards)
    IsNull,     // V2: IS NULL
    IsNotNull,  // V2: IS NOT NULL
};

// A single literal value carried in a leaf. Strings keep their raw
// source representation (without quotes) — type coercion happens
// downstream when the leaf is matched against a tag's key expression.
using Value = std::variant<std::int64_t, double, std::string>;

struct Node;                   // fwd
using NodePtr = std::unique_ptr<Node>;

struct Leaf {
    std::string field;         // case kept as written; matcher does its own folding
    Op          op;
    std::vector<Value> values; // 1 for unary ops, 2 for BETWEEN, N for IN
};

struct And { std::vector<NodePtr> kids; };
struct Or  { std::vector<NodePtr> kids; };
struct Not { NodePtr child; };

// Polymorphic node payload. std::variant keeps allocation patterns
// flat — every Node carries exactly one of {Leaf, And, Or, Not}.
struct Node {
    std::variant<Leaf, And, Or, Not> v;
};

inline NodePtr make_leaf(Leaf l) {
    return std::unique_ptr<Node>(new Node{std::move(l)});
}
inline NodePtr make_and(std::vector<NodePtr> kids) {
    return std::unique_ptr<Node>(new Node{And{std::move(kids)}});
}
inline NodePtr make_or(std::vector<NodePtr> kids) {
    return std::unique_ptr<Node>(new Node{Or{std::move(kids)}});
}
inline NodePtr make_not(NodePtr c) {
    return std::unique_ptr<Node>(new Node{Not{std::move(c)}});
}

// Parse a Clipper-flavoured filter expression into an AST. On any
// syntax we deliberately do not handle (function call, arithmetic,
// LIKE, ...) the parser returns an error so the caller can degrade
// gracefully — the AOF surface treats "cannot parse" as "filter is
// not optimised" rather than as a hard failure.
util::Result<NodePtr> parse(const std::string& src);

// Recognise a constant logical filter: `.T.` / `.F.` (also `.Y.` /
// `.N.`), case-insensitive, optionally wrapped in whitespace and
// parentheses. Returns the constant's value, or std::nullopt when the
// expression is anything else. `AdsSetAOF(".F.")` is the classic idiom
// for "start with an empty AOF, then AdsCustomizeAOF records into it";
// the AST has no constant node, so callers check this before parse().
std::optional<bool> constant_literal(const std::string& src);

// Pretty-print for tests / debugging. Format is stable but not
// part of the public ABI.
std::string to_string(const Node& n);

} // namespace openads::engine::aof
