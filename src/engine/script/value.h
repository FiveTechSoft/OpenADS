// OpenADS script engine — typed values (docs/script-engine.md §3.3).
//
// RCB 07/17/2026: every semantic here is oracle-verified against SAP ADS 11
// (probe results in docs/script-engine.md §10). The headline rule is STRICT
// typing: SAP rejects implicit string<->number mixing in operators and
// assignments ('5' + 1 is an error, CHAR := 5 is an error); numeric widths
// convert freely with truncation; CHAR(N) truncates on assignment; integer
// division is integral (7/2 = 3); comparisons ignore trailing spaces; NULL
// follows SQL three-valued logic and propagates through arithmetic.
#pragma once

#include "util/result.h"

#include <cstdint>
#include <string>

namespace openads::script {

// Error code for script/type errors — matches the ADS 7200 "parse/semantic
// error in SQL" family the SAP oracle returns for these cases.
inline constexpr std::int32_t kScriptError = 7200;

// sub_code marker on an Error produced by RAISE. Call sites that surface an
// uncaught RAISE to a client rewrap it in SAP's shape: rc 7200, NativeError
// 2224, message "An exception is raised in the SQL script. {[name] code :
// msg}" (§11 F3c/F5). Inside the engine the user's code/message stay
// untouched so __errcode/__errtext and CATCH <name> keep working.
inline constexpr std::int32_t kRaiseSubCode = 2224;

enum class Type : std::uint8_t {
    Null,        // typeless NULL (a DECLAREd-but-unassigned var keeps its
                 // declared type with is_null set; bare NULL literal is this)
    Char,        // CHAR/VARCHAR/CICHAR/MEMO — one string family
    Integer,     // SHORT/INTEGER/AUTOINC — 64-bit here
    Double,      // NUMERIC/DOUBLE/MONEY/CURDOUBLE — double here (NUMERIC's
                 // BCD exactness is out of scope for S1; noted in the doc)
    Logical,
    Date,        // Julian Day Number
    Timestamp,   // milliseconds since JDN 0 (jdn * 86400000 + ms-of-day)
};

struct Value {
    Type          type    = Type::Null;
    bool          is_null = true;
    std::string   s;       // Char
    std::int64_t  i = 0;   // Integer / Logical(0|1) / Date(JDN) / Timestamp
    double        d = 0.0; // Double

    static Value null() { return Value{}; }
    static Value character(std::string v);
    static Value integer(std::int64_t v);
    static Value real(double v);
    static Value logical(bool v);
    static Value date(std::int64_t jdn);
    static Value timestamp(std::int64_t ms);
    // A NULL that carries a type (e.g. freshly DECLAREd variable).
    static Value typed_null(Type t);

    bool numeric() const {
        return type == Type::Integer || type == Type::Double;
    }
    double as_double() const {
        return type == Type::Integer ? static_cast<double>(i) : d;
    }
};

// ---- Calendar helpers (proleptic Gregorian, same as SQL DATE) ------------
std::int64_t jdn_from_ymd(int y, int m, int day);
void         ymd_from_jdn(std::int64_t jdn, int& y, int& m, int& day);

// ---- Rendering -----------------------------------------------------------
// Display string using the ADS default formats: dates MM/DD/YYYY, timestamps
// "MM/DD/YYYY HH:MM:SS AM/PM" (what AdsGetField/AdsGetString produce).
// NULL renders as "" for Char and per-SAP "0"-style only at the ABI edge —
// here NULL is always "".
std::string to_display(const Value& v);

// ---- Operations (strict; each returns kScriptError on type mismatch) -----
util::Result<Value> op_add(const Value& a, const Value& b);   // + (concat/arith/date+int)
util::Result<Value> op_sub(const Value& a, const Value& b);   // - (arith/date-int/date-date)
util::Result<Value> op_mul(const Value& a, const Value& b);
util::Result<Value> op_div(const Value& a, const Value& b);   // int/int is integral
util::Result<Value> op_mod(const Value& a, const Value& b);
util::Result<Value> op_neg(const Value& a);

// Comparison: -1/0/+1 through `out`; NULL operands yield is_null result via
// the cmp_* wrappers below. Trailing spaces ignored for Char (P27).
util::Result<Value> cmp_eq(const Value& a, const Value& b);
util::Result<Value> cmp_ne(const Value& a, const Value& b);
util::Result<Value> cmp_lt(const Value& a, const Value& b);
util::Result<Value> cmp_le(const Value& a, const Value& b);
util::Result<Value> cmp_gt(const Value& a, const Value& b);
util::Result<Value> cmp_ge(const Value& a, const Value& b);

// Three-valued logic (P34). Operands must be Logical or NULL.
util::Result<Value> op_and(const Value& a, const Value& b);
util::Result<Value> op_or(const Value& a, const Value& b);
util::Result<Value> op_not(const Value& a);

// Truthiness for IF/WHILE conditions: NULL counts as false (P34).
bool truthy(const Value& v);

// Assignment coercion into a declared slot type (strict: Char<-number and
// number<-Char are errors; numeric widths truncate; CHAR(N) truncates when
// a length limit is given; 0 = unlimited).
util::Result<Value> coerce_assign(Type slot, const Value& v,
                                  std::size_t char_limit);

}  // namespace openads::script
