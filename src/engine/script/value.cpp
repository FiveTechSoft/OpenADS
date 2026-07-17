// OpenADS script engine — typed value semantics (oracle-verified, see
// docs/script-engine.md §10).
#include "engine/script/value.h"

#include <cmath>
#include <cstdio>

namespace openads::script {

using util::Error;
using util::Result;

static Error type_err(const char* what) {
    return Error{kScriptError, 0,
                 std::string("script type error: ") + what, ""};
}

Value Value::character(std::string v) {
    Value r; r.type = Type::Char; r.is_null = false; r.s = std::move(v);
    return r;
}
Value Value::integer(std::int64_t v) {
    Value r; r.type = Type::Integer; r.is_null = false; r.i = v; return r;
}
Value Value::real(double v) {
    Value r; r.type = Type::Double; r.is_null = false; r.d = v; return r;
}
Value Value::logical(bool v) {
    Value r; r.type = Type::Logical; r.is_null = false; r.i = v ? 1 : 0;
    return r;
}
Value Value::date(std::int64_t jdn) {
    Value r; r.type = Type::Date; r.is_null = false; r.i = jdn; return r;
}
Value Value::timestamp(std::int64_t ms) {
    Value r; r.type = Type::Timestamp; r.is_null = false; r.i = ms; return r;
}
Value Value::typed_null(Type t) {
    Value r; r.type = t; r.is_null = true; return r;
}

// ---- Calendar (Fliegel & Van Flandern) -----------------------------------
std::int64_t jdn_from_ymd(int y, int m, int day) {
    std::int64_t a = (14 - m) / 12;
    std::int64_t y2 = y + 4800 - a;
    std::int64_t m2 = m + 12 * a - 3;
    return day + (153 * m2 + 2) / 5 + 365 * y2 + y2 / 4 - y2 / 100 +
           y2 / 400 - 32045;
}
void ymd_from_jdn(std::int64_t jdn, int& y, int& m, int& day) {
    std::int64_t a = jdn + 32044;
    std::int64_t b = (4 * a + 3) / 146097;
    std::int64_t c = a - 146097 * b / 4;
    std::int64_t d2 = (4 * c + 3) / 1461;
    std::int64_t e = c - 1461 * d2 / 4;
    std::int64_t m2 = (5 * e + 2) / 153;
    day = static_cast<int>(e - (153 * m2 + 2) / 5 + 1);
    m   = static_cast<int>(m2 + 3 - 12 * (m2 / 10));
    y   = static_cast<int>(100 * b + d2 - 4800 + m2 / 10);
}

// ---- Rendering -----------------------------------------------------------
static std::string fmt_double(double v) {
    // Integral doubles print without decimals (SAP: 7.0/2 renders "3.50"
    // only because the column metadata says so; scripts render plainly).
    char buf[48];
    if (v == std::floor(v) && std::fabs(v) < 1e15)
        std::snprintf(buf, sizeof(buf), "%.0f", v);
    else
        std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

std::string to_display(const Value& v) {
    if (v.is_null) return "";
    switch (v.type) {
        case Type::Char:    return v.s;
        case Type::Integer: {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%lld",
                          static_cast<long long>(v.i));
            return buf;
        }
        case Type::Double:  return fmt_double(v.d);
        case Type::Logical: return v.i ? "True" : "False";
        case Type::Date: {
            int y, m, d;
            ymd_from_jdn(v.i, y, m, d);
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%02d/%02d/%04d", m, d, y);
            return buf;
        }
        case Type::Timestamp: {
            std::int64_t jdn = v.i / 86400000;
            std::int64_t ms  = v.i % 86400000;
            int y, m, d;
            ymd_from_jdn(jdn, y, m, d);
            int hh = static_cast<int>(ms / 3600000);
            int mi = static_cast<int>((ms / 60000) % 60);
            int ss = static_cast<int>((ms / 1000) % 60);
            const char* ap = hh < 12 ? "AM" : "PM";
            int h12 = hh % 12; if (h12 == 0) h12 = 12;
            char buf[40];
            std::snprintf(buf, sizeof(buf),
                          "%02d/%02d/%04d %02d:%02d:%02d %s",
                          m, d, y, h12, mi, ss, ap);
            return buf;
        }
        case Type::Null: break;
    }
    return "";
}

// ---- Arithmetic ----------------------------------------------------------
// NULL propagates through arithmetic (P12a). A typeless NULL literal mixed
// into concat is an error on SAP (P12b) — we propagate NULL when either side
// is a *typed* null and error only when the op itself is type-invalid.

static Result<Value> arith(const Value& a, const Value& b, char op) {
    if (a.numeric() && b.numeric()) {
        if (a.is_null || b.is_null) {
            Type t = (a.type == Type::Double || b.type == Type::Double)
                   ? Type::Double : Type::Integer;
            return Value::typed_null(t);
        }
        if (a.type == Type::Integer && b.type == Type::Integer) {
            std::int64_t x = a.i, y = b.i;
            switch (op) {
                case '+': return Value::integer(x + y);
                case '-': return Value::integer(x - y);
                case '*': return Value::integer(x * y);
                case '/':
                    if (y == 0) return type_err("division by zero");
                    return Value::integer(x / y);   // integral (P14)
                case '%':
                    if (y == 0) return type_err("division by zero");
                    return Value::integer(x % y);
            }
        }
        double x = a.as_double(), y = b.as_double();
        switch (op) {
            case '+': return Value::real(x + y);
            case '-': return Value::real(x - y);
            case '*': return Value::real(x * y);
            case '/':
                if (y == 0.0) return type_err("division by zero");
                return Value::real(x / y);
            case '%':
                if (y == 0.0) return type_err("division by zero");
                return Value::real(std::fmod(x, y));
        }
    }
    return type_err("numeric operands required");
}

Result<Value> op_add(const Value& a, const Value& b) {
    if (a.type == Type::Char && b.type == Type::Char) {
        if (a.is_null || b.is_null) return Value::typed_null(Type::Char);
        return Value::character(a.s + b.s);
    }
    if (a.type == Type::Date && b.type == Type::Integer) {
        if (a.is_null || b.is_null) return Value::typed_null(Type::Date);
        return Value::date(a.i + b.i);              // DATE + n (P17e)
    }
    if (a.type == Type::Integer && b.type == Type::Date) {
        if (a.is_null || b.is_null) return Value::typed_null(Type::Date);
        return Value::date(b.i + a.i);
    }
    if (a.type == Type::Timestamp && b.numeric()) {
        if (a.is_null || b.is_null) return Value::typed_null(Type::Timestamp);
        return Value::timestamp(a.i + b.numeric() * 0 +
            static_cast<std::int64_t>(b.as_double() * 86400000.0));
    }
    if (a.numeric() && b.numeric()) return arith(a, b, '+');
    // Strict typing: '5' + 1 is an error on SAP (P13).
    return type_err("'+' operands must both be character or both numeric");
}

Result<Value> op_sub(const Value& a, const Value& b) {
    if (a.type == Type::Date && b.type == Type::Date) {
        if (a.is_null || b.is_null) return Value::typed_null(Type::Integer);
        return Value::integer(a.i - b.i);           // days (P17e)
    }
    if (a.type == Type::Date && b.type == Type::Integer) {
        if (a.is_null || b.is_null) return Value::typed_null(Type::Date);
        return Value::date(a.i - b.i);
    }
    if (a.numeric() && b.numeric()) return arith(a, b, '-');
    return type_err("'-' operands must be numeric or dates");
}

Result<Value> op_mul(const Value& a, const Value& b) { return arith(a, b, '*'); }
Result<Value> op_div(const Value& a, const Value& b) { return arith(a, b, '/'); }
Result<Value> op_mod(const Value& a, const Value& b) { return arith(a, b, '%'); }

Result<Value> op_neg(const Value& a) {
    if (!a.numeric()) return type_err("unary '-' needs a numeric operand");
    if (a.is_null) return Value::typed_null(a.type);
    if (a.type == Type::Integer) return Value::integer(-a.i);
    return Value::real(-a.d);
}

// ---- Comparison ----------------------------------------------------------
// Returns -1/0/+1; ok=false → NULL result (SQL unknown). Trailing spaces on
// Char are ignored (P27).
static Result<int> compare(const Value& a, const Value& b, bool& ok) {
    ok = !(a.is_null || b.is_null);
    if (!ok) return 0;
    if (a.type == Type::Char && b.type == Type::Char) {
        std::size_t la = a.s.find_last_not_of(' ');
        std::size_t lb = b.s.find_last_not_of(' ');
        std::string ta = (la == std::string::npos) ? "" : a.s.substr(0, la + 1);
        std::string tb = (lb == std::string::npos) ? "" : b.s.substr(0, lb + 1);
        return ta < tb ? -1 : (ta == tb ? 0 : 1);
    }
    if (a.numeric() && b.numeric()) {
        double x = a.as_double(), y = b.as_double();
        return x < y ? -1 : (x == y ? 0 : 1);
    }
    if ((a.type == Type::Date && b.type == Type::Date) ||
        (a.type == Type::Timestamp && b.type == Type::Timestamp) ||
        (a.type == Type::Logical && b.type == Type::Logical)) {
        return a.i < b.i ? -1 : (a.i == b.i ? 0 : 1);
    }
    return type_err("comparison operands of incompatible types");
}

template <class Pred>
static Result<Value> cmp_impl(const Value& a, const Value& b, Pred pred) {
    bool ok = false;
    auto c = compare(a, b, ok);
    if (!c) return c.error();
    if (!ok) return Value::typed_null(Type::Logical);  // NULL cmp → unknown
    return Value::logical(pred(c.value()));
}

Result<Value> cmp_eq(const Value& a, const Value& b) {
    return cmp_impl(a, b, [](int c) { return c == 0; });
}
Result<Value> cmp_ne(const Value& a, const Value& b) {
    return cmp_impl(a, b, [](int c) { return c != 0; });
}
Result<Value> cmp_lt(const Value& a, const Value& b) {
    return cmp_impl(a, b, [](int c) { return c < 0; });
}
Result<Value> cmp_le(const Value& a, const Value& b) {
    return cmp_impl(a, b, [](int c) { return c <= 0; });
}
Result<Value> cmp_gt(const Value& a, const Value& b) {
    return cmp_impl(a, b, [](int c) { return c > 0; });
}
Result<Value> cmp_ge(const Value& a, const Value& b) {
    return cmp_impl(a, b, [](int c) { return c >= 0; });
}

// ---- Three-valued logic (P34) --------------------------------------------
static bool is_logicalish(const Value& v) {
    return v.type == Type::Logical || v.type == Type::Null;
}

Result<Value> op_and(const Value& a, const Value& b) {
    if (!is_logicalish(a) || !is_logicalish(b))
        return type_err("AND needs logical operands");
    bool an = a.is_null, bn = b.is_null;
    if (!an && !a.i) return Value::logical(false);   // false AND x = false
    if (!bn && !b.i) return Value::logical(false);
    if (an || bn)    return Value::typed_null(Type::Logical);
    return Value::logical(true);
}
Result<Value> op_or(const Value& a, const Value& b) {
    if (!is_logicalish(a) || !is_logicalish(b))
        return type_err("OR needs logical operands");
    bool an = a.is_null, bn = b.is_null;
    if (!an && a.i) return Value::logical(true);     // true OR x = true
    if (!bn && b.i) return Value::logical(true);
    if (an || bn)   return Value::typed_null(Type::Logical);
    return Value::logical(false);
}
Result<Value> op_not(const Value& a) {
    if (!is_logicalish(a)) return type_err("NOT needs a logical operand");
    if (a.is_null) return Value::typed_null(Type::Logical);
    return Value::logical(!a.i);
}

bool truthy(const Value& v) {
    return !v.is_null && v.type == Type::Logical && v.i != 0;
}

// ---- Assignment coercion (strict, P19/P26/P31/P32) -----------------------
util::Result<Value> coerce_assign(Type slot, const Value& v,
                                  std::size_t char_limit) {
    if (v.is_null) return Value::typed_null(slot);
    switch (slot) {
        case Type::Char:
            if (v.type != Type::Char)
                return type_err("cannot assign non-character to CHAR");
            if (char_limit > 0 && v.s.size() > char_limit) {
                Value r = v; r.s.resize(char_limit); return r;  // P26
            }
            return v;
        case Type::Integer:
            if (!v.numeric())
                return type_err("cannot assign non-numeric to INTEGER");
            if (v.type == Type::Integer) return v;
            return Value::integer(static_cast<std::int64_t>(v.d));   // P32
        case Type::Double:
            if (!v.numeric())
                return type_err("cannot assign non-numeric to NUMERIC");
            if (v.type == Type::Double) return v;
            return Value::real(static_cast<double>(v.i));
        case Type::Logical:
            if (v.type != Type::Logical)
                return type_err("cannot assign non-logical to LOGICAL");
            return v;
        case Type::Date:
            if (v.type == Type::Date) return v;
            if (v.type == Type::Timestamp)                    // CAST-like
                return Value::date(v.i / 86400000);
            return type_err("cannot assign non-date to DATE");
        case Type::Timestamp:
            if (v.type == Type::Timestamp) return v;
            if (v.type == Type::Date)
                return Value::timestamp(v.i * 86400000);
            return type_err("cannot assign non-timestamp to TIMESTAMP");
        case Type::Null: return v;  // untyped slot accepts anything
    }
    return type_err("bad assignment");
}

}  // namespace openads::script
