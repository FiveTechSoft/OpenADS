#pragma once

#include "drivers/dbf_common.h"
#include "util/result.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openads::engine {

class Table;

// Evaluate a CDX-style index key expression against the current
// record buffer of `t`, padded / clipped to `key_len` bytes.
//
// Supported grammar (subset of Clipper / FoxPro key expressions):
//
//   expr  := term ('+' term)*
//   term  := call | ident | number | string-literal | '(' expr ')'
//   call  := ident '(' (expr (',' expr)*)? ')'
//
//   functions:  UPPER(s) LOWER(s) LTRIM(s) RTRIM(s) ALLTRIM(s)
//               STR(n)   STR(n, len)   STR(n, len, dec)
//               DTOS(d)
//               SUBSTR(s, start)   SUBSTR(s, start, len)
//               LEFT(s, n) RIGHT(s, n) SPACE(n) REPLICATE(s, n) LEN(s)
//               PADL/PADR/PADC(s, n [, c])  EMPTY(x)
//               IIF(cond, a, b) / IF(...)  -- cond uses the FOR grammar
//
// Identifiers are looked up first as field names against the table's
// schema; an unknown identifier evaluates to an empty string. Bare
// field names short-circuit to the legacy "raw bytes padded with
// spaces" path so existing single-field expressions stay byte-exact
// with M8.8.
util::Result<std::string>
    evaluate_index_expr(Table&             t,
                        const std::string& expr,
                        std::uint16_t      key_len);

// Width of a character key expression the way Visual FoxPro sizes a CDX
// tag: from the expression's STATIC maximum, not from any record's value.
// TRIM(x) keeps x's declared width, LEFT/RIGHT(x,n) is min(n, width(x)),
// PADx/SPACE(n) is n, IIF takes the wider branch, '+' sums. VFP sizes
// INDEX ON TYPE+RIGHT(TRIM(CCN),4) as 3+4 = 7 even when the first record's
// CCN is blank or the table is empty; sizing from record 1 truncated
// every later key. Returns nullopt for numeric results or anything the
// analyser does not know (unknown function / field, non-literal count) --
// the caller then falls back to probing a record.
std::optional<std::uint16_t>
    static_key_width(Table& t, const std::string& expr);

// FOR-clause / scope predicate evaluator. Parses a Clipper-style
// boolean expression and returns whether the current row passes.
// On parse failure or unsupported syntax returns true (be permissive
// — the FOR clause then degrades to "include all").
bool evaluate_index_expr_truthy(Table& t, const std::string& expr);

// Evaluate `expr` against the current record and, if it yields a numeric
// value, return true and set `out`. Used to build FoxPro/Harbour binary
// numeric index keys (8-byte order-preserving) for CDX. Returns false for
// string-valued expressions (e.g. STR(...), bare character fields).
bool evaluate_index_expr_number(Table& t, const std::string& expr, double& out);

// Encode a double as the 8-byte order-preserving key FoxPro / Harbour store
// for numeric and date CDX keys (HB_DBL2ORD). The result compares correctly
// byte-for-byte (unsigned) in ascending numeric order, so the CDX B+tree —
// which compares keys as opaque bytes — needs no type awareness.
std::string fox_numeric_key(double value);

// Encode a double as the native DBFNTX numeric key (verified byte-for-byte
// against Harbour DBFNTX `INDEX ON <numfield>`): the magnitude is zero-padded
// to `width` with `dec` decimals via printf("%0*.*f"); for a negative value
// every byte is complemented as (0x5c - byte) so negatives sort before
// positives and the decimal point (0x2e, self-complementing) stays fixed.
// The NTX B+tree compares keys as opaque bytes, so it needs no type awareness.
std::string ntx_numeric_key(double value, std::uint16_t width, std::uint16_t dec);

// Drop `ALIAS->` workarea qualifiers from a key/FOR expression so a
// bare `FIELD->NAME` resolves to the plain field name `NAME`. Used by
// the evaluator (write side) and by the ABI seek path so a numeric
// seek can recover the field width from the schema the same way the
// stored key was built. Compound expressions keep their structure;
// only the qualifier is removed (e.g. `UPPER(CUST->NAME)` -> `UPPER(NAME)`).
std::string strip_alias_qualifiers(const std::string& expr);

// ── Tier-2 SQL push-down (spike) ──────────────────────────────────────────
//
// Per-backend knobs for translating an xBase predicate into SQL. Defaults
// target SQLite / PostgreSQL / Firebird (ANSI-ish). MySQL needs CONCAT() and
// SUBSTRING(). Adjust now_fn, length_fn, etc. per dialect.
struct SqlDialect {
    std::string concat_op     = "||";       // xBase string '+'  (MySQL: CONCAT)
    bool        use_concat_fn = false;       // a + b -> CONCAT(a, b)
    std::string substr_fn     = "SUBSTR";    // SUBSTR(s, start [, len])
    std::string alltrim_open  = "TRIM(";     // ALLTRIM(x) -> TRIM(x)
    std::string alltrim_close = ")";
    std::string length_fn     = "LENGTH";    // LEN(x) -> LENGTH(x) (or CHAR_LENGTH)
    bool        use_char_length = false;     // true for MySQL/MariaDB
    std::string now_fn        = "NOW()";     // DATE() -> NOW() or CURRENT_DATE
    std::string true_literal  = "1";         // boolean true in SQL (some DBs use TRUE)
    std::string false_literal = "0";         // boolean false in SQL
};

// Translate an xBase-style FOR / SET FILTER predicate into an equivalent SQL
// boolean expression (a WHERE fragment), for tables backed by a SQL engine —
// so the backend filters server-side instead of the engine scanning every row
// and evaluating evaluate_index_expr_truthy() per record.
//
// Returns std::nullopt when ANY part of the expression falls outside the
// safely-translatable subset (RECNO()/DELETED() — backend must handle
// specially, REPLICATE/SPACE/STUFF/OCCURS — no portable SQL form, unknown
// functions, or any construct without a portable SQL form). The caller then
// filters with evaluate_index_expr_truthy() — so push-down is ALWAYS optional
// and never changes the result set. Conservative by design: on any doubt it
// declines rather than emit a predicate that could match the wrong rows.
//
// Supported subset:
//   Operands: field/literal/number
//   Comparisons: = == # != <> < <= > >=
//   $ operator: 'needle' $ field → field LIKE '%needle%'
//               field1 $ field2  → field2 LIKE '%' || field1 || '%'
//   Logical: .AND. .OR. .NOT. / !  (also SQL AND/OR/NOT)
//   Groups: ( expression )
//   Concat: a + b → a || b  (or CONCAT(a, b))
//   String: UPPER LOWER LTRIM RTRIM/TRIM ALLTRIM SUBSTR/SUBS LEFT RIGHT
//           LEN AT/ATNUM PADR PADL PADC STRTRAN
//   Numeric: INT/FLOOR ABS ROUND MOD CEILING CEIL EXP LOG LOG10 SQRT SIGN VAL STR
//   Date: DATE TODAY NOW TIME DATETIME CTOD DTOC DTOS YEAR MONTH DAY HOUR MINUTE SECOND
//         DOW CDOW CMONTH
//   Conditional: IIF IF EMPTY ISNULL ISBLANK
std::optional<std::string>
    try_emit_sql_where(const std::string& expr, const SqlDialect& dialect = {});

} // namespace openads::engine
