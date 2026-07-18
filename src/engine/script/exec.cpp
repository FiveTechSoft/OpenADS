// OpenADS script engine — executor implementation.
//
// RCB 07/17/2026: semantics oracle-verified against SAP ADS 11 (probe
// results in docs/script-engine.md §10) — strict typing, three-valued
// logic, LEAVE/CONTINUE, TRY/CATCH ALL with __errcode/__errtext, RAISE.
#include "engine/script/exec.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace openads::script {

using util::Error;
using util::Result;

namespace {

Error serr(const std::string& what) {
    return Error{kScriptError, 0, "script error: " + what, ""};
}

std::string upper(const std::string& s) {
    std::string r; r.reserve(s.size());
    for (char c : s)
        r.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(c))));
    return r;
}

// SQL LIKE with % and _ wildcards, case-insensitive (CICHAR semantics).
bool like_match(const std::string& text, const std::string& pat) {
    std::size_t ti = 0, pi = 0, star_t = std::string::npos, star_p = 0;
    auto low = [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    while (ti < text.size()) {
        if (pi < pat.size() &&
            (pat[pi] == '_' || low(pat[pi]) == low(text[ti]))) {
            ++ti; ++pi;
        } else if (pi < pat.size() && pat[pi] == '%') {
            star_p = ++pi; star_t = ti;
        } else if (star_t != std::string::npos) {
            pi = star_p; ti = ++star_t;
        } else {
            return false;
        }
    }
    while (pi < pat.size() && pat[pi] == '%') ++pi;
    return pi == pat.size();
}

}  // namespace

std::string to_sql_literal(const Value& v) {
    if (v.is_null) return "NULL";
    switch (v.type) {
        case Type::Char: {
            std::string o = "'";
            for (char c : v.s) { if (c == '\'') o += '\''; o += c; }
            o += '\'';
            return o;
        }
        case Type::Integer: case Type::Double: case Type::Logical:
            return v.type == Type::Logical ? (v.i ? "TRUE" : "FALSE")
                                           : to_display(v);
        case Type::Date: {
            int y, m, d;
            ymd_from_jdn(v.i, y, m, d);
            char buf[24];
            std::snprintf(buf, sizeof(buf), "{d '%04d-%02d-%02d'}", y, m, d);
            return buf;
        }
        case Type::Timestamp: {
            std::int64_t jdn = v.i / 86400000, ms = v.i % 86400000;
            int y, m, d;
            ymd_from_jdn(jdn, y, m, d);
            char buf[40];
            std::snprintf(buf, sizeof(buf),
                          "{ts '%04d-%02d-%02d %02d:%02d:%02d'}", y, m, d,
                          static_cast<int>(ms / 3600000),
                          static_cast<int>((ms / 60000) % 60),
                          static_cast<int>((ms / 1000) % 60));
            return buf;
        }
        case Type::Null: break;
    }
    return "NULL";
}

void Executor::set_param(const std::string& name, Value v) {
    Slot s;
    s.val = std::move(v);
    s.declared = Type::Null;   // params keep whatever type the caller gave
    scope_[upper(name)] = std::move(s);
}

Result<ExecResult> Executor::run(const Program& p) {
    auto f = exec_block(p.stmts);
    if (!f) return f.error();
    ExecResult r;
    if (f.value().k == Flow::Return) {
        r.returned = true;
        r.return_value = std::move(f.value().ret);
    }
    r.last_select = std::move(last_select_);
    return r;
}

Result<Executor::Flow> Executor::exec_block(const Block& b) {
    for (const auto& s : b) {
        auto f = exec_stmt(*s);
        if (!f) return f;
        if (f.value().k != Flow::Normal) return f;
    }
    return Flow{};
}

Result<Executor::Flow> Executor::exec_stmt(const Stmt& s) {
    switch (s.kind) {
        case StmtKind::Declare: {
            if (s.is_cursor) {
                Cursor c;
                c.bound_sql = s.raw;   // may be empty (bound at OPEN AS)
                cursors_[s.upper] = std::move(c);
                return Flow{};
            }
            Slot slot;
            slot.declared = s.decl_type;
            slot.char_limit = s.char_limit;
            slot.val = Value::typed_null(s.decl_type);
            scope_[s.upper] = std::move(slot);
            return Flow{};
        }
        case StmtKind::Assign: {
            auto it = scope_.find(s.upper);
            if (it == scope_.end())
                return serr("undeclared variable '" + s.name + "'");
            auto v = eval(*s.expr);
            if (!v) return v.error();
            auto cv = coerce_assign(
                it->second.declared == Type::Null ? v.value().type
                                                  : it->second.declared,
                v.value(), it->second.char_limit);
            if (!cv) return cv.error();
            it->second.val = std::move(cv).value();
            return Flow{};
        }
        case StmtKind::If: {
            for (const auto& br : s.branches) {
                auto c = eval(*br.first);
                if (!c) return c.error();
                if (truthy(c.value())) return exec_block(br.second);
            }
            return exec_block(s.else_block);
        }
        case StmtKind::While: {
            for (;;) {
                auto c = eval(*s.expr);
                if (!c) return c.error();
                if (!truthy(c.value())) break;
                auto f = exec_block(s.body);
                if (!f) return f;
                if (f.value().k == Flow::Leave) break;
                if (f.value().k == Flow::Return) return f;
                // Continue and Normal both re-test the condition.
            }
            return Flow{};
        }
        case StmtKind::Leave: {
            Flow f; f.k = Flow::Leave; return f;
        }
        case StmtKind::Continue: {
            Flow f; f.k = Flow::Continue; return f;
        }
        case StmtKind::Return: {
            Flow f; f.k = Flow::Return;
            if (s.expr) {
                auto v = eval(*s.expr);
                if (!v) return v.error();
                f.ret = std::move(v).value();
            }
            return f;
        }
        case StmtKind::Try: {
            // §11 F-probes: body → matching CATCH → FINALLY; FINALLY runs
            // even when the error is uncaught, before it propagates (F3).
            auto f = exec_block(s.body);
            if (!f) {
                // Find a matching CATCH: ALL (empty name) matches
                // everything; a named clause matches the RAISE name
                // carried in the error context, case-insensitively (F4-F6).
                const CatchClause* match = nullptr;
                std::string raised = upper(f.error().context);
                for (const auto& cc : s.catches) {
                    if (cc.name_upper.empty() || cc.name_upper == raised) {
                        match = &cc;
                        break;
                    }
                }
                if (match != nullptr) {
                    err_code_ = Value::integer(f.error().code);
                    err_text_ = Value::character(f.error().message);
                    auto cf = exec_block(match->block);
                    err_code_ = Value::null();
                    err_text_ = Value::null();
                    f = std::move(cf);
                }
            }
            if (s.has_finally) {
                auto ff = exec_block(s.finally_block);
                // A FINALLY error wins only if the try/catch outcome was
                // clean; otherwise the original error propagates.
                if (!ff && f) return ff;
                if (ff && ff.value().k != Flow::Normal && f &&
                    f.value().k == Flow::Normal)
                    return ff;
            }
            return f;
        }
        case StmtKind::Raise: {
            std::int64_t code = 0;
            std::string  msg  = s.name;
            if (!s.args.empty()) {
                auto c = eval(*s.args[0]);
                if (!c) return c.error();
                if (!c.value().is_null && c.value().numeric())
                    code = c.value().type == Type::Integer
                         ? c.value().i
                         : static_cast<std::int64_t>(c.value().d);
            }
            if (s.args.size() > 1) {
                auto m = eval(*s.args[1]);
                if (!m) return m.error();
                if (m.value().type == Type::Char) msg = m.value().s;
            }
            return Error{static_cast<std::int32_t>(code), kRaiseSubCode,
                         msg, s.name};
        }
        case StmtKind::Sql: {
            if (bridge_ == nullptr)
                return serr("embedded SQL requires a connection: " +
                            s.raw.substr(0, 40));
            auto sub = substitute(s.raw);
            if (!sub) return sub.error();
            auto cur = bridge_->exec(sub.value());
            if (!cur) return cur.error();
            // §10 mechanism: the LAST statement-position SELECT's cursor is
            // the script's result.
            if (cur.value()) {
                std::size_t b = s.raw.find_first_not_of(" \t\r\n");
                if (b != std::string::npos && s.raw.size() - b >= 6) {
                    std::string kw = upper(s.raw.substr(b, 6));
                    if (kw == "SELECT")
                        last_select_ = std::move(cur).value();
                }
            }
            return Flow{};
        }
        case StmtKind::ExecImmediate: {
            if (bridge_ == nullptr)
                return serr("EXECUTE IMMEDIATE requires a connection");
            auto v = eval(*s.expr);
            if (!v) return v.error();
            if (v.value().is_null || v.value().type != Type::Char)
                return serr("EXECUTE IMMEDIATE needs a character statement");
            // Dynamic SQL goes through the same bridge, so the caller's
            // rewrites (__output, trigger row images) apply to it too.
            // Oracle-checked: the inner script's SELECT cursor does NOT
            // become the outer statement's result (EI probe, 2026-07-18) —
            // the cursor is discarded here.
            auto cur = bridge_->exec(v.value().s);
            if (!cur) return cur.error();
            return Flow{};
        }
        // ---- Cursor statements (§11 C-probes) ----------------------------
        case StmtKind::OpenCursor: {
            auto it = cursors_.find(s.upper);
            if (it == cursors_.end())
                return serr("the variable is not found: " + s.name);  // 2218
            Cursor& c = it->second;
            if (c.cur != nullptr)
                return serr("cursor is already opened: " + s.name);   // 2221
            if (!s.raw.empty())
                c.bound_sql = s.raw;   // OPEN … AS rebinds (C15)
            if (c.bound_sql.empty())
                return serr("cursor is not defined: " + s.name);      // 2219
            if (bridge_ == nullptr)
                return serr("cursors require a connection");
            // Variables AND other cursors' fields substitute as literals at
            // OPEN time (C33b: OPEN l AS EXECUTE PROCEDURE p(c.name)).
            auto sub = substitute(c.bound_sql);
            if (!sub) return sub.error();
            auto cur = bridge_->exec(sub.value());
            if (!cur) return cur.error();
            if (!cur.value())
                return serr("cursor statement returned no result: " +
                            s.name);
            c.cur = std::move(cur).value();
            c.on_row = false;
            c.col_upper.clear();
            std::size_t nf = c.cur->field_count();
            c.col_upper.reserve(nf);
            for (std::size_t k = 0; k < nf; ++k)
                c.col_upper.push_back(upper(c.cur->field_name(k)));
            return Flow{};
        }
        case StmtKind::FetchCursor: {
            // Statement form: advance, discard the row-available flag.
            // Fetching past EOF is harmless (C6) — only field access errors.
            auto it = cursors_.find(s.upper);
            if (it == cursors_.end())
                return serr("the variable is not found: " + s.name);
            if (it->second.cur == nullptr)
                return serr("error using a closed cursor: " + s.name); // 2220
            it->second.on_row = it->second.cur->next();
            return Flow{};
        }
        case StmtKind::CloseCursor: {
            auto it = cursors_.find(s.upper);
            if (it == cursors_.end())
                return serr("the variable is not found: " + s.name);
            if (it->second.cur == nullptr)
                return serr("error using a closed cursor: " + s.name); // 2220
            it->second.cur.reset();
            it->second.on_row = false;
            it->second.col_upper.clear();
            return Flow{};
        }
    }
    return serr("unhandled statement");
}

// Current-row field of an open cursor, with the SAP error progression
// (§11): closed → 2220, before-first/after-last → 2223, unknown field →
// alias-not-found.
Result<Value> Executor::cursor_field(const std::string& upper_name,
                                     const std::string& disp_name,
                                     const std::string& field) {
    auto it = cursors_.find(upper_name);
    if (it == cursors_.end())
        return serr("unknown identifier '" + disp_name + "'");
    Cursor& c = it->second;
    if (c.cur == nullptr)
        return serr("error using a closed cursor: " + disp_name);      // 2220
    if (!c.on_row)
        return serr("the cursor is before first row or after last row. "
                    "Referencing: " + disp_name);                      // 2223
    std::string want = upper(field);
    for (std::size_t k = 0; k < c.col_upper.size(); ++k)
        if (c.col_upper[k] == want) return c.cur->field(k);
    return serr("table or alias not found: " + disp_name + "." + field);
}

// ---- Variable substitution into raw SQL (design §4.2 fallback mode) ------
// Whole-word identifiers matching scope variables are replaced by literals;
// `c.field` references to OPEN script cursors are replaced by the current
// row value (C21c/C22/C33b); quoted strings and [bracketed] names pass
// through untouched. A SQL alias that merely shadows a cursor name is not a
// concern: SAP itself resolves the cursor first (C22 works because the
// cursor value wins).
Result<std::string> Executor::substitute(const std::string& raw) {
    std::string out;
    out.reserve(raw.size() + 16);
    std::size_t i = 0, n = raw.size();
    while (i < n) {
        char c = raw[i];
        if (c == '\'') {                       // string literal
            std::size_t j = i + 1;
            while (j < n) {
                if (raw[j] == '\'' && j + 1 < n && raw[j + 1] == '\'') j += 2;
                else if (raw[j] == '\'') { ++j; break; }
                else ++j;
            }
            out.append(raw, i, j - i);
            i = j;
            continue;
        }
        if (c == '[') {                        // [bracketed identifier]
            std::size_t j = raw.find(']', i);
            j = (j == std::string::npos) ? n : j + 1;
            out.append(raw, i, j - i);
            i = j;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' ||
            c == '@') {
            std::size_t j = i + 1;
            while (j < n && (std::isalnum(static_cast<unsigned char>(raw[j])) ||
                             raw[j] == '_'))
                ++j;
            std::string word = raw.substr(i, j - i);
            bool prev_dot = (i > 0 && raw[i - 1] == '.');
            // cursor.field / cursor.[field] → current-row literal.
            if (!prev_dot && j < n && raw[j] == '.' &&
                cursors_.count(upper(word)) != 0) {
                std::size_t f0 = j + 1, fe = f0;
                std::string fld;
                if (fe < n && raw[fe] == '[') {
                    std::size_t rb = raw.find(']', fe);
                    if (rb != std::string::npos) {
                        fld = raw.substr(fe + 1, rb - fe - 1);
                        fe = rb + 1;
                    }
                } else {
                    while (fe < n &&
                           (std::isalnum(static_cast<unsigned char>(raw[fe])) ||
                            raw[fe] == '_'))
                        ++fe;
                    fld = raw.substr(f0, fe - f0);
                }
                if (!fld.empty()) {
                    auto v = cursor_field(upper(word), word, fld);
                    if (!v) return v.error();
                    out += to_sql_literal(v.value());
                    i = fe;
                    continue;
                }
            }
            auto it = scope_.find(upper(word));
            // Don't substitute if it's a qualified name part (x.y) or a
            // function call (name().
            bool qualified = (j < n && raw[j] == '.') || prev_dot;
            std::size_t k = j;
            while (k < n && raw[k] == ' ') ++k;
            bool is_call = (k < n && raw[k] == '(');
            if (it != scope_.end() && !qualified && !is_call)
                out += to_sql_literal(it->second.val);
            else
                out += word;
            i = j;
            continue;
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

Result<Value> Executor::eval_subquery(const std::string& raw) {
    if (bridge_ == nullptr)
        return serr("subquery requires a connection");
    auto sub = substitute(raw);
    if (!sub) return sub.error();
    auto cur = bridge_->exec(sub.value());
    if (!cur) return cur.error();
    if (!cur.value() || !cur.value()->next() ||
        cur.value()->field_count() == 0)
        return Value::null();                  // empty result → NULL
    return cur.value()->field(0);
}

// ---- Expression evaluation ----------------------------------------------

Result<Value> Executor::eval(const Expr& e) {
    switch (e.kind) {
        case ExprKind::Literal: return e.lit;
        case ExprKind::Var: {
            auto it = scope_.find(e.upper);
            if (it != scope_.end()) return it->second.val;
            if (e.upper == "__ERRCODE") return err_code_;
            if (e.upper == "__ERRTEXT") return err_text_;
            return serr("unknown identifier '" + e.name + "'");
        }
        case ExprKind::Unary: {
            auto a = eval(*e.a);
            if (!a) return a;
            switch (e.un) {
                case UnOp::Neg: return op_neg(a.value());
                case UnOp::Not: return op_not(a.value());
                case UnOp::IsNull:
                    return Value::logical(a.value().is_null);
                case UnOp::IsNotNull:
                    return Value::logical(!a.value().is_null);
            }
            return serr("bad unary");
        }
        case ExprKind::Binary: {
            // AND/OR need lazy semantics only for errors, not truth — SQL
            // three-valued logic is total, so evaluating both is correct.
            auto a = eval(*e.a);
            if (!a) return a;
            auto b = eval(*e.b);
            if (!b) return b;
            switch (e.bin) {
                case BinOp::Add: return op_add(a.value(), b.value());
                case BinOp::Sub: return op_sub(a.value(), b.value());
                case BinOp::Mul: return op_mul(a.value(), b.value());
                case BinOp::Div: return op_div(a.value(), b.value());
                case BinOp::Mod: return op_mod(a.value(), b.value());
                case BinOp::Eq:  return cmp_eq(a.value(), b.value());
                case BinOp::Ne:  return cmp_ne(a.value(), b.value());
                case BinOp::Lt:  return cmp_lt(a.value(), b.value());
                case BinOp::Le:  return cmp_le(a.value(), b.value());
                case BinOp::Gt:  return cmp_gt(a.value(), b.value());
                case BinOp::Ge:  return cmp_ge(a.value(), b.value());
                case BinOp::And: return op_and(a.value(), b.value());
                case BinOp::Or:  return op_or(a.value(), b.value());
                case BinOp::Like: {
                    if (a.value().is_null || b.value().is_null)
                        return Value::typed_null(Type::Logical);
                    if (a.value().type != Type::Char ||
                        b.value().type != Type::Char)
                        return serr("LIKE needs character operands");
                    bool m = like_match(a.value().s, b.value().s);
                    return Value::logical(e.negated ? !m : m);
                }
            }
            return serr("bad binary");
        }
        case ExprKind::InList: {
            auto a = eval(*e.a);
            if (!a) return a;
            bool any_null = a.value().is_null;
            for (const auto& item : e.args) {
                auto v = eval(*item);
                if (!v) return v;
                auto c = cmp_eq(a.value(), v.value());
                if (!c) return c;
                if (c.value().is_null) { any_null = true; continue; }
                if (c.value().i)
                    return Value::logical(!e.negated);
            }
            if (any_null) return Value::typed_null(Type::Logical);
            return Value::logical(e.negated);
        }
        case ExprKind::Between: {
            auto a = eval(*e.a);
            if (!a) return a;
            auto lo = eval(*e.b);
            if (!lo) return lo;
            auto hi = eval(*e.c);
            if (!hi) return hi;
            auto ge = cmp_ge(a.value(), lo.value());
            if (!ge) return ge;
            auto le = cmp_le(a.value(), hi.value());
            if (!le) return le;
            auto r = op_and(ge.value(), le.value());
            if (!r) return r;
            if (r.value().is_null) return r;
            return Value::logical(e.negated ? !r.value().i
                                            : r.value().i != 0);
        }
        case ExprKind::Case: {
            if (e.case_operand) {
                auto op = eval(*e.case_operand);
                if (!op) return op;
                for (const auto& w : e.whens) {
                    auto v = eval(*w.first);
                    if (!v) return v;
                    auto c = cmp_eq(op.value(), v.value());
                    if (!c) return c;
                    if (!c.value().is_null && c.value().i)
                        return eval(*w.second);
                }
            } else {
                for (const auto& w : e.whens) {
                    auto c = eval(*w.first);
                    if (!c) return c;
                    if (truthy(c.value())) return eval(*w.second);
                }
            }
            if (e.else_expr) return eval(*e.else_expr);
            return Value::null();
        }
        case ExprKind::Subquery: return eval_subquery(e.raw);
        case ExprKind::FnCall:   return eval_call(e);
        case ExprKind::CursorField:
            return cursor_field(e.upper, e.name, e.field);
        case ExprKind::Fetch: {
            // Boolean FETCH (C24/C25): advances the cursor, true while a
            // row was produced. Past-EOF fetches are harmless (C6).
            auto it = cursors_.find(e.upper);
            if (it == cursors_.end())
                return serr("the variable is not found: " + e.name);
            if (it->second.cur == nullptr)
                return serr("error using a closed cursor: " + e.name);
            it->second.on_row = it->second.cur->next();
            return Value::logical(it->second.on_row);
        }
    }
    return serr("bad expression");
}

Result<Value> Executor::eval_call(const Expr& e) {
    const std::string& fn = e.upper;

    // IIF evaluates lazily — only the taken branch (and branches must agree
    // in type on SAP (P33), which lazy evaluation checks implicitly at use).
    if (fn == "IIF" && e.args.size() == 3) {
        auto c = eval(*e.args[0]);
        if (!c) return c;
        return truthy(c.value()) ? eval(*e.args[1]) : eval(*e.args[2]);
    }

    // Evaluate args eagerly for everything else.
    std::vector<Value> a;
    a.reserve(e.args.size());
    for (const auto& arg : e.args) {
        auto v = eval(*arg);
        if (!v) return v;
        a.push_back(std::move(v).value());
    }

    auto need = [&](std::size_t n) { return a.size() >= n; };
    auto int_at = [&](std::size_t i) -> std::int64_t {
        return a[i].type == Type::Integer
             ? a[i].i : static_cast<std::int64_t>(a[i].as_double());
    };

    if (fn == "CREATETIMESTAMP" && need(3)) {
        std::int64_t jdn = jdn_from_ymd(static_cast<int>(int_at(0)),
                                        static_cast<int>(int_at(1)),
                                        static_cast<int>(int_at(2)));
        std::int64_t h  = need(4) ? int_at(3) : 0;
        std::int64_t mi = need(5) ? int_at(4) : 0;
        std::int64_t s  = need(6) ? int_at(5) : 0;
        std::int64_t ms = need(7) ? int_at(6) : 0;
        return Value::timestamp(jdn * 86400000 +
                                (h * 3600 + mi * 60 + s) * 1000 + ms);
    }
    if (fn == "CURDATE" || fn == "NOW" || fn == "CURTIMESTAMP") {
        std::time_t t = std::time(nullptr);
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        std::int64_t jdn = jdn_from_ymd(tmv.tm_year + 1900, tmv.tm_mon + 1,
                                        tmv.tm_mday);
        if (fn == "CURDATE") return Value::date(jdn);
        return Value::timestamp(jdn * 86400000 +
            (static_cast<std::int64_t>(tmv.tm_hour) * 3600 +
             tmv.tm_min * 60 + tmv.tm_sec) * 1000);
    }
    if (fn == "USER") return Value::character(user_);
    if ((fn == "CHAR" || fn == "CHR") && need(1) && a[0].numeric()) {
        // Char(13) — ASCII code to 1-char string (pmsys audit proc builds
        // CRLF this way). The type name CHAR never reaches here (it only
        // appears inside DECLARE / CAST, both handled by the parser).
        return Value::character(std::string(
            1, static_cast<char>(int_at(0) & 0xFF)));
    }
    if ((fn == "YEAR" || fn == "MONTH" || fn == "DAY") && need(1)) {
        const Value& v = a[0];
        if (v.is_null) return Value::typed_null(Type::Integer);
        std::int64_t jdn = 0;
        if (v.type == Type::Date) jdn = v.i;
        else if (v.type == Type::Timestamp) jdn = v.i / 86400000;
        else return serr(fn + " needs a date");
        int y, m, d;
        ymd_from_jdn(jdn, y, m, d);
        return Value::integer(fn == "YEAR" ? y : fn == "MONTH" ? m : d);
    }
    if (fn == "SUBSTRING" && need(3)) {
        if (a[0].is_null) return Value::typed_null(Type::Char);
        std::int64_t p = int_at(1), len = int_at(2);
        if (a[0].type != Type::Char) return serr("SUBSTRING needs a string");
        const std::string& s = a[0].s;
        if (p < 1) p = 1;
        if (static_cast<std::size_t>(p) > s.size() || len <= 0)
            return Value::character("");
        return Value::character(
            s.substr(static_cast<std::size_t>(p - 1),
                     static_cast<std::size_t>(len)));
    }
    if ((fn == "LEFT" || fn == "RIGHT") && need(2)) {
        if (a[0].is_null) return Value::typed_null(Type::Char);
        if (a[0].type != Type::Char) return serr(fn + " needs a string");
        const std::string& s = a[0].s;
        std::size_t n2 = static_cast<std::size_t>(
            int_at(1) < 0 ? 0 : int_at(1));
        if (n2 >= s.size()) return Value::character(s);
        return Value::character(fn == "LEFT" ? s.substr(0, n2)
                                             : s.substr(s.size() - n2));
    }
    if (fn == "REPEAT" && need(2)) {
        if (a[0].is_null) return Value::typed_null(Type::Char);
        if (a[0].type != Type::Char) return serr("REPEAT needs a string");
        std::string o;
        for (std::int64_t k = 0; k < int_at(1); ++k) o += a[0].s;
        return Value::character(o);
    }
    if ((fn == "TRIM" || fn == "LTRIM" || fn == "RTRIM" ||
         fn == "ALLTRIM") && need(1)) {
        if (a[0].is_null) return Value::typed_null(Type::Char);
        if (a[0].type != Type::Char) return serr("TRIM needs a string");
        std::string s = a[0].s;
        if (fn != "RTRIM") {
            auto p = s.find_first_not_of(' ');
            s = (p == std::string::npos) ? "" : s.substr(p);
        }
        if (fn != "LTRIM") {
            auto p = s.find_last_not_of(' ');
            if (p == std::string::npos) s.clear();
            else s.resize(p + 1);
        }
        return Value::character(s);
    }
    if ((fn == "UPPER" || fn == "UCASE" || fn == "LOWER" || fn == "LCASE") &&
        need(1)) {
        if (a[0].is_null) return Value::typed_null(Type::Char);
        if (a[0].type != Type::Char) return serr(fn + " needs a string");
        std::string s = a[0].s;
        bool up = (fn[0] == 'U');
        for (auto& c : s)
            c = static_cast<char>(
                up ? std::toupper(static_cast<unsigned char>(c))
                   : std::tolower(static_cast<unsigned char>(c)));
        return Value::character(s);
    }
    if ((fn == "LENGTH" || fn == "LEN" || fn == "CHAR_LENGTH") && need(1)) {
        if (a[0].is_null) return Value::typed_null(Type::Integer);
        if (a[0].type != Type::Char) return serr("LENGTH needs a string");
        // Trailing blanks don't count (N2/N3: LENGTH of a CHAR(10) holding
        // 'a' is 1) — interior blanks do (N5).
        std::size_t len = a[0].s.find_last_not_of(' ');
        return Value::integer(len == std::string::npos
                              ? 0 : static_cast<std::int64_t>(len + 1));
    }
    if (fn == "POSITION" && need(2)) {
        if (a[0].is_null || a[1].is_null)
            return Value::typed_null(Type::Integer);
        if (a[0].type != Type::Char || a[1].type != Type::Char)
            return serr("POSITION needs strings");
        auto p = a[1].s.find(a[0].s);
        return Value::integer(
            p == std::string::npos ? 0 : static_cast<std::int64_t>(p + 1));
    }
    if (fn == "ABS" && need(1)) {
        if (!a[0].numeric()) return serr("ABS needs a number");
        if (a[0].is_null) return Value::typed_null(a[0].type);
        if (a[0].type == Type::Integer)
            return Value::integer(a[0].i < 0 ? -a[0].i : a[0].i);
        return Value::real(std::fabs(a[0].d));
    }
    if (fn == "MOD" && need(2)) return op_mod(a[0], a[1]);
    if (fn == "STR" && need(1)) {
        if (a[0].is_null) return Value::typed_null(Type::Char);
        if (!a[0].numeric()) return serr("STR needs a number");
        int len = need(2) ? static_cast<int>(int_at(1)) : 10;
        int dec = need(3) ? static_cast<int>(int_at(2)) : 0;
        char fmt[16], buf[64];
        std::snprintf(fmt, sizeof(fmt), "%%%d.%df", len, dec);
        std::snprintf(buf, sizeof(buf), fmt, a[0].as_double());
        return Value::character(buf);
    }
    if (fn == "VAL" && need(1)) {
        if (a[0].is_null) return Value::typed_null(Type::Double);
        if (a[0].type != Type::Char) return serr("VAL needs a string");
        return Value::real(std::strtod(a[0].s.c_str(), nullptr));
    }
    if ((fn == "CAST" || fn == "CONVERT") && need(1)) {
        const Value& v = a[0];
        const std::string& t = e.type_name;
        if (t == "SQL_CHAR" || t == "SQL_VARCHAR" || t == "SQL_LONGVARCHAR") {
            if (v.is_null) return Value::typed_null(Type::Char);
            return Value::character(to_display(v));
        }
        if (t == "SQL_INTEGER" || t == "SQL_SMALLINT" || t == "SQL_TINYINT" ||
            t == "SQL_BIGINT") {
            if (v.is_null) return Value::typed_null(Type::Integer);
            if (v.numeric())
                return Value::integer(
                    static_cast<std::int64_t>(v.as_double()));
            if (v.type == Type::Char) {
                char* end = nullptr;
                double d = std::strtod(v.s.c_str(), &end);
                if (end == v.s.c_str())
                    return serr("cannot convert '" + v.s + "' to integer");
                return Value::integer(static_cast<std::int64_t>(d));
            }
            return serr("bad conversion to integer");
        }
        if (t == "SQL_NUMERIC" || t == "SQL_DOUBLE" || t == "SQL_FLOAT" ||
            t == "SQL_REAL" || t == "SQL_DECIMAL" || t == "SQL_MONEY") {
            if (v.is_null) return Value::typed_null(Type::Double);
            if (v.numeric()) return Value::real(v.as_double());
            if (v.type == Type::Char) {
                char* end = nullptr;
                double d = std::strtod(v.s.c_str(), &end);
                if (end == v.s.c_str())
                    return serr("cannot convert '" + v.s + "' to number");
                return Value::real(d);
            }
            return serr("bad conversion to number");
        }
        // 'YYYY-MM-DD[ HH:MM:SS]' or 'MM/DD/YYYY' text forms accepted for
        // date/timestamp conversion (pmsys DaysInMonth builds the string and
        // CONVERTs it to SQL_TIMESTAMP).
        auto parse_dt = [](const std::string& s, std::int64_t& jdn,
                           std::int64_t& ms) {
            std::string x = s;
            while (!x.empty() && x.back() == ' ') x.pop_back();
            std::size_t f = x.find_first_not_of(' ');
            if (f != std::string::npos) x = x.substr(f);
            int y = 0, mo = 0, d = 0;
            if (x.size() >= 10 && x[4] == '-' && x[7] == '-') {
                y  = std::atoi(x.substr(0, 4).c_str());
                mo = std::atoi(x.substr(5, 2).c_str());
                d  = std::atoi(x.substr(8, 2).c_str());
            } else if (x.size() >= 10 && x[2] == '/' && x[5] == '/') {
                mo = std::atoi(x.substr(0, 2).c_str());
                d  = std::atoi(x.substr(3, 2).c_str());
                y  = std::atoi(x.substr(6, 4).c_str());
            } else return false;
            if (y == 0 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;
            jdn = jdn_from_ymd(y, mo, d);
            ms = 0;
            if (x.size() >= 19 && (x[10] == ' ' || x[10] == 'T')) {
                int hh = std::atoi(x.substr(11, 2).c_str());
                int mi = std::atoi(x.substr(14, 2).c_str());
                int ss = std::atoi(x.substr(17, 2).c_str());
                ms = (static_cast<std::int64_t>(hh) * 3600 + mi * 60 + ss) *
                     1000;
            }
            return true;
        };
        if (t == "SQL_DATE") {
            if (v.is_null) return Value::typed_null(Type::Date);
            if (v.type == Type::Date) return v;
            if (v.type == Type::Timestamp)
                return Value::date(v.i / 86400000);     // P17f
            if (v.type == Type::Char) {
                std::int64_t jdn = 0, ms = 0;
                if (parse_dt(v.s, jdn, ms)) return Value::date(jdn);
                return serr("cannot convert '" + v.s + "' to date");
            }
            return serr("bad conversion to date");
        }
        if (t == "SQL_TIMESTAMP") {
            if (v.is_null) return Value::typed_null(Type::Timestamp);
            if (v.type == Type::Timestamp) return v;
            if (v.type == Type::Date)
                return Value::timestamp(v.i * 86400000);
            if (v.type == Type::Char) {
                std::int64_t jdn = 0, ms = 0;
                if (parse_dt(v.s, jdn, ms))
                    return Value::timestamp(jdn * 86400000 + ms);
                return serr("cannot convert '" + v.s + "' to timestamp");
            }
            return serr("bad conversion to timestamp");
        }
        return serr("unknown conversion type " + t);
    }

    if (fn == "IFNULL" && need(2))
        return a[0].is_null ? a[1] : a[0];

    // ODBC TIMESTAMPADD / TIMESTAMPDIFF. DATE arguments promote to
    // timestamps; MONTH/YEAR use calendar boundaries (TIMESTAMPDIFF
    // counts boundary crossings: 2020-01→2024-06 = 53 months), matching
    // the SAP oracle (pmsys MonthsOnTheMarket).
    if ((fn == "TIMESTAMPADD" || fn == "TIMESTAMPDIFF") && need(2)) {
        auto as_ts = [](const Value& v, std::int64_t& out) {
            if (v.type == Type::Timestamp) { out = v.i; return true; }
            if (v.type == Type::Date) { out = v.i * 86400000; return true; }
            return false;
        };
        const std::string& unit = e.type_name;
        if (fn == "TIMESTAMPDIFF") {
            if (a[0].is_null || a[1].is_null)
                return Value::typed_null(Type::Integer);
            std::int64_t t1 = 0, t2 = 0;
            if (!as_ts(a[0], t1) || !as_ts(a[1], t2))
                return serr("TIMESTAMPDIFF needs date/timestamp arguments");
            if (unit == "SQL_TSI_YEAR" || unit == "SQL_TSI_MONTH" ||
                unit == "SQL_TSI_QUARTER") {
                int y1, m1, d1, y2, m2, d2;
                ymd_from_jdn(t1 / 86400000, y1, m1, d1);
                ymd_from_jdn(t2 / 86400000, y2, m2, d2);
                std::int64_t months =
                    (static_cast<std::int64_t>(y2) - y1) * 12 + (m2 - m1);
                if (unit == "SQL_TSI_YEAR")
                    return Value::integer(static_cast<std::int64_t>(y2) - y1);
                if (unit == "SQL_TSI_QUARTER")
                    return Value::integer(months / 3);
                return Value::integer(months);
            }
            std::int64_t ms = t2 - t1;
            if (unit == "SQL_TSI_DAY")
                return Value::integer(t2 / 86400000 - t1 / 86400000);
            if (unit == "SQL_TSI_WEEK")
                return Value::integer((t2 / 86400000 - t1 / 86400000) / 7);
            if (unit == "SQL_TSI_HOUR")   return Value::integer(ms / 3600000);
            if (unit == "SQL_TSI_MINUTE") return Value::integer(ms / 60000);
            if (unit == "SQL_TSI_SECOND") return Value::integer(ms / 1000);
            return serr("unknown interval " + unit);
        }
        // TIMESTAMPADD(unit, n, ts)
        if (!need(2)) return serr("TIMESTAMPADD needs 2 arguments");
        if (a[0].is_null || a[1].is_null)
            return Value::typed_null(Type::Timestamp);
        if (!a[0].numeric())
            return serr("TIMESTAMPADD count must be numeric");
        std::int64_t n2 = a[0].type == Type::Integer
                        ? a[0].i
                        : static_cast<std::int64_t>(a[0].d);
        std::int64_t ts = 0;
        if (!as_ts(a[1], ts))
            return serr("TIMESTAMPADD needs a date/timestamp argument");
        bool was_date = a[1].type == Type::Date;
        if (unit == "SQL_TSI_YEAR" || unit == "SQL_TSI_MONTH" ||
            unit == "SQL_TSI_QUARTER") {
            std::int64_t add_m = unit == "SQL_TSI_YEAR" ? n2 * 12
                               : unit == "SQL_TSI_QUARTER" ? n2 * 3 : n2;
            int y, m, d;
            ymd_from_jdn(ts / 86400000, y, m, d);
            std::int64_t total = (static_cast<std::int64_t>(y) * 12 + m - 1) +
                                 add_m;
            int ny = static_cast<int>(total / 12);
            int nm = static_cast<int>(total % 12) + 1;
            // Clamp day to the target month's length (Jan 31 +1M → Feb 28).
            std::int64_t first = jdn_from_ymd(ny, nm, 1);
            std::int64_t next_first = nm == 12 ? jdn_from_ymd(ny + 1, 1, 1)
                                               : jdn_from_ymd(ny, nm + 1, 1);
            int dim = static_cast<int>(next_first - first);
            if (d > dim) d = dim;
            std::int64_t njdn = jdn_from_ymd(ny, nm, d);
            std::int64_t nts = njdn * 86400000 + ts % 86400000;
            return was_date ? Value::date(njdn) : Value::timestamp(nts);
        }
        std::int64_t ms = 0;
        if      (unit == "SQL_TSI_DAY")    ms = n2 * 86400000;
        else if (unit == "SQL_TSI_WEEK")   ms = n2 * 7 * 86400000;
        else if (unit == "SQL_TSI_HOUR")   ms = n2 * 3600000;
        else if (unit == "SQL_TSI_MINUTE") ms = n2 * 60000;
        else if (unit == "SQL_TSI_SECOND") ms = n2 * 1000;
        else return serr("unknown interval " + unit);
        std::int64_t nts = ts + ms;
        return was_date && ms % 86400000 == 0
             ? Value::date(nts / 86400000) : Value::timestamp(nts);
    }

    // Not a builtin: try a DD user-defined function through the bridge
    // (direct recursion, no SQL round-trip — design §4.5).
    if (bridge_ != nullptr && bridge_->has_udf(e.name))
        return bridge_->call_udf(e.name, a);

    return serr("unknown function '" + e.name + "'");
}

}  // namespace openads::script
