#include "sql/parser.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <memory>
#include <string>

namespace openads::sql {

namespace {

class Cursor {
public:
    explicit Cursor(const std::string& s) : s_(s) {}

    void skip_ws() {
        while (pos_ < s_.size() &&
               std::isspace(static_cast<unsigned char>(s_[pos_]))) {
            ++pos_;
        }
    }

    bool eof() const { return pos_ >= s_.size(); }

    bool peek_keyword(const char* kw) const {
        std::size_t len = 0;
        while (kw[len] != '\0') ++len;
        std::size_t p = pos_;
        while (p < s_.size() && std::isspace(
                static_cast<unsigned char>(s_[p]))) ++p;
        if (p + len > s_.size()) return false;
        for (std::size_t i = 0; i < len; ++i) {
            char a = static_cast<char>(std::tolower(
                static_cast<unsigned char>(s_[p + i])));
            char b = static_cast<char>(std::tolower(
                static_cast<unsigned char>(kw[i])));
            if (a != b) return false;
        }
        if (p + len < s_.size()) {
            char c = s_[p + len];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                return false;
            }
        }
        return true;
    }

    bool match_keyword(const char* kw) {
        if (!peek_keyword(kw)) return false;
        skip_ws();
        std::size_t len = 0;
        while (kw[len] != '\0') ++len;
        pos_ += len;
        return true;
    }

    bool match_char(char c) {
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == c) { ++pos_; return true; }
        return false;
    }

    // SAP ADS cursor/optimizer hint: `{static}`, `{ index ... }` etc.
    // appearing right after SELECT. Consumed and discarded — OpenADS
    // picks its own access path. Hints do not nest; stop at the first '}'.
    void skip_optimizer_hint() {
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == '{') {
            ++pos_;  // consume '{'
            while (pos_ < s_.size() && s_[pos_] != '}') ++pos_;
            if (pos_ < s_.size()) ++pos_;  // consume '}'
        }
    }

    bool match_seq(const char* seq) {
        skip_ws();
        std::size_t len = 0;
        while (seq[len] != '\0') ++len;
        if (pos_ + len > s_.size()) return false;
        for (std::size_t i = 0; i < len; ++i) {
            if (s_[pos_ + i] != seq[i]) return false;
        }
        pos_ += len;
        return true;
    }

    std::string read_identifier_or_filename() {
        skip_ws();
        std::string out;
        // SAP ADS bracket-quoted name: [articulo.dat] / [name with spaces].
        // Mirrors read_identifier()'s bracket handling so FROM can name a
        // free table/file the legacy ADS way.
        if (pos_ < s_.size() && s_[pos_] == '[') {
            ++pos_;  // consume '['
            while (pos_ < s_.size() && s_[pos_] != ']')
                out.push_back(s_[pos_++]);
            if (pos_ < s_.size()) ++pos_;  // consume ']'
            return out;
        }
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (std::isalnum(static_cast<unsigned char>(c)) ||
                c == '_' || c == '.' || c == '\\' || c == '/') {
                out.push_back(c);
                ++pos_;
            } else {
                break;
            }
        }
        return out;
    }

    // Reads an identifier, optionally reporting the dropped alias and a
    // qualified-wildcard marker. `out_alias` (if non-null) receives the
    // `<alias>` of a qualified ref `<alias>.<col>`. `out_wildcard` (if
    // non-null) enables `<alias>.*` recognition: when present and the ref
    // is `alias.*`, the '*' is consumed, *out_wildcard is set, and the
    // alias is returned as the name. When `out_wildcard` is null the old
    // behaviour is preserved exactly (the '*' is left unconsumed), so all
    // existing callers are unaffected.
    std::string read_identifier(std::string* out_alias = nullptr,
                                bool* out_wildcard = nullptr) {
        skip_ws();
        std::string out;
        // SAP ADS bracket-quoted identifier: [reserved_word] or [name with spaces]
        if (pos_ < s_.size() && s_[pos_] == '[') {
            ++pos_;  // consume '['
            while (pos_ < s_.size() && s_[pos_] != ']')
                out.push_back(s_[pos_++]);
            if (pos_ < s_.size()) ++pos_;  // consume ']'
            return out;
        }
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                out.push_back(c);
                ++pos_;
            } else {
                break;
            }
        }
        // M10.51 — qualified column ref: <alias>.<col>. Drop the
        // alias and return only the column name; the engine resolves
        // bare names against the (single, possibly joined) cursor.
        if (pos_ < s_.size() && s_[pos_] == '.') {
            // ADS dialect — qualified wildcard `<alias>.*` (only when the
            // caller opted in via out_wildcard; otherwise leave '*' alone).
            if (out_wildcard != nullptr && pos_ + 1 < s_.size() &&
                s_[pos_ + 1] == '*') {
                if (out_alias) *out_alias = out;
                *out_wildcard = true;
                pos_ += 2;   // consume '.' and '*'
                return out;  // name == alias; caller keys on the wildcard flag
            }
            ++pos_;
            if (out_alias) *out_alias = out;   // remember the dropped alias
            std::string col;
            while (pos_ < s_.size()) {
                char c = s_[pos_];
                if (std::isalnum(static_cast<unsigned char>(c)) ||
                    c == '_') {
                    col.push_back(c);
                    ++pos_;
                } else {
                    break;
                }
            }
            if (!col.empty()) out = std::move(col);
        }
        return out;
    }

    util::Result<std::string> read_string_literal() {
        skip_ws();
        if (pos_ >= s_.size() || s_[pos_] != '\'') {
            return util::Error{7200, 0, "expected string literal", ""};
        }
        ++pos_;
        std::string out;
        while (pos_ < s_.size()) {
            if (s_[pos_] == '\'') {
                // SQL-standard: a doubled '' inside a literal is one '.
                if (pos_ + 1 < s_.size() && s_[pos_ + 1] == '\'') {
                    out.push_back('\'');
                    pos_ += 2;
                    continue;
                }
                break;   // closing quote
            }
            out.push_back(s_[pos_]);
            ++pos_;
        }
        if (pos_ >= s_.size()) {
            return util::Error{7200, 0, "unterminated string literal", ""};
        }
        ++pos_;   // consume closing quote
        return out;
    }

    // ADS / ODBC temporal literal escape: {d 'YYYY-MM-DD'}, {t 'HH:MM:SS'},
    // {ts 'YYYY-MM-DD HH:MM:SS'}. Returns the inner value reduced to digits
    // only (e.g. '2026-01-01' -> '20260101') so it string-compares against a
    // DBF Date field's raw YYYYMMDD bytes. Assumes the current char is '{'.
    util::Result<std::string> read_odbc_temporal_literal() {
        skip_ws();
        if (pos_ >= s_.size() || s_[pos_] != '{') {
            return util::Error{7200, 0,
                "expected '{' for temporal literal", ""};
        }
        ++pos_;                       // consume '{'
        skip_ws();
        // Type marker d / t / ts (case-insensitive) — consumed and ignored.
        while (pos_ < s_.size() &&
               std::isalpha(static_cast<unsigned char>(s_[pos_]))) {
            ++pos_;
        }
        auto lit = read_string_literal();
        if (!lit) return lit.error();
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;                   // consume '}'
        } else {
            return util::Error{7200, 0,
                "expected '}' to close temporal literal", ""};
        }
        std::string digits;
        for (char ch : lit.value()) {
            if (std::isdigit(static_cast<unsigned char>(ch)))
                digits.push_back(ch);
        }
        return digits;
    }

    // Decimal numeric literal — optional sign, digits, optional `.` +
    // more digits. Returns the parsed double via stoul/stod. The
    // integer-only branch (no decimal point) stays compatible with
    // ASCII-numeric DBF columns.
    util::Result<double> read_numeric_literal() {
        skip_ws();
        std::size_t start = pos_;
        if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
        bool any = false;
        while (pos_ < s_.size() &&
               std::isdigit(static_cast<unsigned char>(s_[pos_]))) {
            ++pos_; any = true;
        }
        if (pos_ < s_.size() && s_[pos_] == '.') {
            ++pos_;
            while (pos_ < s_.size() &&
                   std::isdigit(static_cast<unsigned char>(s_[pos_]))) {
                ++pos_; any = true;
            }
        }
        if (!any) {
            pos_ = start;
            return util::Error{7200, 0, "expected numeric literal", ""};
        }
        std::string tok(s_.substr(start, pos_ - start));
        return std::strtod(tok.c_str(), nullptr);
    }

    bool peek_char(char c) {
        skip_ws();
        return pos_ < s_.size() && s_[pos_] == c;
    }

    // Next non-ws char is a decimal digit. Used to spot a constant
    // WHERE operand (`1 = 1`); xBase column names never start with a
    // digit, so a digit-led LHS is unambiguously a literal.
    bool peek_digit() {
        skip_ws();
        return pos_ < s_.size() &&
               std::isdigit(static_cast<unsigned char>(s_[pos_]));
    }

    // Advance past one raw character (no whitespace skip) and return
    // it. Used by the CREATE INDEX expression scanner that wants to
    // capture the source text verbatim until the matching ')'.
    char consume_char() {
        if (pos_ >= s_.size()) return '\0';
        return s_[pos_++];
    }

private:
    const std::string& s_;
    std::size_t        pos_ = 0;
};

util::Result<std::unique_ptr<WhereExpr>>
parse_or_expr(Cursor& c, const std::string& sql);

util::Result<std::unique_ptr<WhereExpr>>
parse_cmp(Cursor& c, const std::string& sql) {
    auto node = std::make_unique<WhereExpr>();
    node->kind = WhereExpr::Kind::Cmp;

    // ADS dialect — constant predicate `<num> <op> <num>` (e.g. the
    // `WHERE 1 = 1` boilerplate SQL generators emit). xBase column names
    // never start with a digit, so a digit-led LHS is a literal. Evaluate
    // it at parse time and fold to an empty AND (always-true) / empty OR
    // (always-false) node so the executor never looks up a "1" column.
    if (c.peek_digit()) {
        auto lhs = c.read_numeric_literal();
        if (!lhs) return lhs.error();
        WhereOp op;
        if      (c.match_seq("<=")) op = WhereOp::Le;
        else if (c.match_seq(">=")) op = WhereOp::Ge;
        else if (c.match_seq("<>")) op = WhereOp::Ne;
        else if (c.match_seq("!=")) op = WhereOp::Ne;
        else if (c.match_char('=')) op = WhereOp::Eq;
        else if (c.match_char('<')) op = WhereOp::Lt;
        else if (c.match_char('>')) op = WhereOp::Gt;
        else return util::Error{7200, 0,
            "expected comparison operator after constant", sql};
        auto rhs = c.read_numeric_literal();
        if (!rhs) return rhs.error();
        double a = lhs.value(), b = rhs.value();
        bool truth = false;
        switch (op) {
            case WhereOp::Eq: truth = (a == b); break;
            case WhereOp::Ne: truth = (a != b); break;
            case WhereOp::Lt: truth = (a <  b); break;
            case WhereOp::Gt: truth = (a >  b); break;
            case WhereOp::Le: truth = (a <= b); break;
            case WhereOp::Ge: truth = (a >= b); break;
            default: break;
        }
        // Empty AND ⇒ always true; empty OR ⇒ always false (the executor's
        // And/Or handlers fold zero children to true/false respectively).
        node->kind = truth ? WhereExpr::Kind::And : WhereExpr::Kind::Or;
        return node;
    }

    if (c.match_keyword("CONTAINS")) {
        if (!c.match_char('(')) {
            return util::Error{7200, 0, "expected '(' after CONTAINS", sql};
        }
        node->cmp.column = c.read_identifier();
        if (node->cmp.column.empty()) {
            return util::Error{7200, 0,
                "expected column name in CONTAINS", sql};
        }
        if (!c.match_char(',')) {
            return util::Error{7200, 0,
                "expected ',' between CONTAINS column and query", sql};
        }
        auto lit = c.read_string_literal();
        if (!lit) return lit.error();
        node->cmp.literal = std::move(lit).value();
        if (!c.match_char(')')) {
            return util::Error{7200, 0, "expected ')' to close CONTAINS", sql};
        }
        node->cmp.op = WhereOp::Contains;
        return node;
    }

    // ADS dialect — optional case-folding wrapper on the LHS:
    // `UPPER(<col>) <op> <lit>` / `LOWER(<col>) <op> <lit>`. The function
    // name is only treated as such when an opening paren follows; a column
    // literally named UPPER/LOWER (no paren) still reads as a column.
    {
        std::string head_alias;
        std::string head = c.read_identifier(&head_alias);
        std::string upper;
        upper.reserve(head.size());
        for (char ch : head) {
            upper.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(ch))));
        }
        if ((upper == "UPPER" || upper == "LOWER") && c.match_char('(')) {
            node->cmp.lhs_fn =
                (upper == "UPPER") ? WhereFn::Upper : WhereFn::Lower;
            // drops the <alias>. prefix but keeps the alias for the N-way path
            node->cmp.column = c.read_identifier(&node->cmp.column_alias);
            if (node->cmp.column.empty()) {
                return util::Error{7200, 0,
                    "expected column inside UPPER()/LOWER()", sql};
            }
            if (!c.match_char(')')) {
                return util::Error{7200, 0,
                    "expected ')' after UPPER()/LOWER() column", sql};
            }
        } else {
            node->cmp.column       = std::move(head);
            node->cmp.column_alias = std::move(head_alias);
        }
    }
    if (node->cmp.column.empty()) {
        return util::Error{7200, 0,
            "expected column name in WHERE clause", sql};
    }
    c.skip_ws();

    // M10.15: `<col> IN (<lit>, <lit>, …)` or `<col> IN (<sub-SELECT>)`.
    if (c.match_keyword("IN")) {
        if (!c.match_char('(')) {
            return util::Error{7200, 0,
                "expected '(' after IN", sql};
        }
        node->kind = WhereExpr::Kind::In;
        node->in_clause.column = node->cmp.column;
        // (cmp's other fields stay default-zeroed; the In branch
        // ignores them.)
        if (c.match_keyword("SELECT")) {
            // Re-parse the nested SELECT — capture text up to the
            // matching ')'. consume_char (no whitespace skip) is the
            // only reader so the inner buffer keeps every space and
            // tab byte the inner parser will need.
            std::string inner = "SELECT ";
            int depth = 1;
            while (depth > 0) {
                if (c.eof()) {
                    return util::Error{7200, 0,
                        "unterminated IN subquery", sql};
                }
                char ch = c.consume_char();
                if (ch == '(') { ++depth; inner.push_back('('); continue; }
                if (ch == ')') {
                    --depth;
                    if (depth == 0) break;
                    inner.push_back(')');
                    continue;
                }
                inner.push_back(ch);
            }
            auto sub = parse_select(inner);
            if (!sub) return sub.error();
            node->in_clause.subquery =
                std::make_unique<SelectStmt>(std::move(sub).value());
        } else {
            // Literal list.
            for (;;) {
                auto lit = c.read_string_literal();
                if (!lit) return lit.error();
                node->in_clause.literals.push_back(std::move(lit).value());
                if (c.match_char(',')) continue;
                break;
            }
            if (!c.match_char(')')) {
                return util::Error{7200, 0,
                    "expected ')' to close IN list", sql};
            }
        }
        return node;
    }

    // M10.44 — `<col> IS [NOT] NULL`. No RHS. For DBFs without an
    // explicit NULL bitmap (M11.6), the executor treats all-spaces
    // string cells as NULL.
    if (c.match_keyword("IS")) {
        bool not_null = c.match_keyword("NOT");
        if (!c.match_keyword("NULL")) {
            return util::Error{7200, 0,
                "expected NULL after IS [NOT]", sql};
        }
        node->cmp.op = not_null ? WhereOp::IsNotNull : WhereOp::IsNull;
        return node;
    }

    // M10.33 — `<col> BETWEEN <lit> AND <lit>` and `<col> LIKE '<pattern>'`.
    if (c.match_keyword("BETWEEN")) {
        node->cmp.op = WhereOp::Between;
        // Lower bound.
        if (c.peek_char('\'')) {
            auto lit = c.read_string_literal();
            if (!lit) return lit.error();
            node->cmp.literal    = std::move(lit).value();
            node->cmp.is_numeric = false;
        } else {
            auto n = c.read_numeric_literal();
            if (!n) return n.error();
            node->cmp.is_numeric = true;
            node->cmp.number     = n.value();
            char tmp[64];
            std::snprintf(tmp, sizeof(tmp), "%.17g", n.value());
            node->cmp.literal    = tmp;
        }
        if (!c.match_keyword("AND")) {
            return util::Error{7200, 0,
                "expected AND between BETWEEN bounds", sql};
        }
        // Upper bound.
        if (c.peek_char('\'')) {
            auto lit = c.read_string_literal();
            if (!lit) return lit.error();
            node->cmp.literal2 = std::move(lit).value();
        } else {
            auto n = c.read_numeric_literal();
            if (!n) return n.error();
            node->cmp.number2 = n.value();
            char tmp[64];
            std::snprintf(tmp, sizeof(tmp), "%.17g", n.value());
            node->cmp.literal2 = tmp;
        }
        return node;
    }
    if (c.match_keyword("LIKE")) {
        node->cmp.op = WhereOp::Like;
        if (!c.peek_char('\'')) {
            return util::Error{7200, 0,
                "expected string literal after LIKE", sql};
        }
        auto lit = c.read_string_literal();
        if (!lit) return lit.error();
        node->cmp.literal    = std::move(lit).value();
        node->cmp.is_numeric = false;
        return node;
    }

    if      (c.match_seq("<=")) node->cmp.op = WhereOp::Le;
    else if (c.match_seq(">=")) node->cmp.op = WhereOp::Ge;
    else if (c.match_seq("<>")) node->cmp.op = WhereOp::Ne;
    else if (c.match_seq("!=")) node->cmp.op = WhereOp::Ne;
    else if (c.match_char('=')) node->cmp.op = WhereOp::Eq;
    else if (c.match_char('<')) node->cmp.op = WhereOp::Lt;
    else if (c.match_char('>')) node->cmp.op = WhereOp::Gt;
    else {
        return util::Error{7200, 0,
            "expected =, !=, <>, <, >, <= or >= after column name", sql};
    }

    if (c.peek_char('{')) {
        // ADS / ODBC temporal escape on the RHS: `tx_date >= {d '...'}`.
        // Reduced to digits so it string-compares against the Date field's
        // raw YYYYMMDD bytes.
        auto d = c.read_odbc_temporal_literal();
        if (!d) return d.error();
        node->cmp.literal    = std::move(d).value();
        node->cmp.is_numeric = false;
    } else if (c.peek_char('\'')) {
        auto lit = c.read_string_literal();
        if (!lit) return lit.error();
        node->cmp.literal    = std::move(lit).value();
        node->cmp.is_numeric = false;
    } else if (c.peek_char('(')) {
        // M10.18: scalar subquery — `(SELECT <col> FROM <t>)`. The
        // value is materialised at compile time and stored back in
        // node->cmp.literal / number.
        c.match_char('(');
        if (!c.match_keyword("SELECT")) {
            return util::Error{7200, 0,
                "expected SELECT inside scalar subquery", sql};
        }
        std::string inner = "SELECT ";
        int depth = 1;
        while (depth > 0) {
            if (c.eof()) {
                return util::Error{7200, 0,
                    "unterminated scalar subquery", sql};
            }
            char ch = c.consume_char();
            if (ch == '(') { ++depth; inner.push_back('('); continue; }
            if (ch == ')') {
                --depth;
                if (depth == 0) break;
                inner.push_back(')');
                continue;
            }
            inner.push_back(ch);
        }
        auto sub = parse_select(inner);
        if (!sub) return sub.error();
        node->cmp.subquery =
            std::make_unique<SelectStmt>(std::move(sub).value());
    } else {
        auto n = c.read_numeric_literal();
        if (n) {
            node->cmp.is_numeric = true;
            node->cmp.number     = n.value();
            char tmp[64];
            std::snprintf(tmp, sizeof(tmp), "%.17g", n.value());
            node->cmp.literal    = tmp;
        } else {
            // M10.24: bare identifier on RHS — outer-column reference
            // for a correlated subquery (`b.x = a.y`), OR a zero-arg
            // function call like CURDATE() evaluated at parse time.
            std::string id_alias;
            std::string id = c.read_identifier(&id_alias);
            if (id.empty()) {
                return util::Error{7200, 0,
                    "expected string literal, number, or column "
                    "reference on RHS of comparison", sql};
            }
            if (c.peek_char('(')) {
                // function call — consume the argument list
                c.match_char('(');
                int fn_depth = 1;
                while (fn_depth > 0) {
                    if (c.eof()) {
                        return util::Error{7200, 0,
                            "unterminated function call in WHERE clause", sql};
                    }
                    char ch = c.consume_char();
                    if      (ch == '(') ++fn_depth;
                    else if (ch == ')') --fn_depth;
                }
                // Evaluate known zero-arg date/time functions to string literals.
                std::string uid;
                uid.reserve(id.size());
                for (char ch : id)
                    uid.push_back(static_cast<char>(
                        std::toupper(static_cast<unsigned char>(ch))));
                char datebuf[80] = {};
                std::time_t t = std::time(nullptr);
                std::tm tm_l{};
#ifdef _WIN32
                localtime_s(&tm_l, &t);
#else
                localtime_r(&t, &tm_l);
#endif
                if (uid == "CURDATE" || uid == "TODAY" ||
                    uid == "DATE"    || uid == "GETDATE") {
                    std::snprintf(datebuf, sizeof(datebuf), "%04d-%02d-%02d",
                                  tm_l.tm_year + 1900,
                                  tm_l.tm_mon  + 1,
                                  tm_l.tm_mday);
                    node->cmp.literal    = datebuf;
                    node->cmp.is_numeric = false;
                } else if (uid == "NOW") {
                    std::snprintf(datebuf, sizeof(datebuf),
                                  "%04d-%02d-%02d %02d:%02d:%02d",
                                  tm_l.tm_year + 1900,
                                  tm_l.tm_mon  + 1,
                                  tm_l.tm_mday,
                                  tm_l.tm_hour,
                                  tm_l.tm_min,
                                  tm_l.tm_sec);
                    node->cmp.literal    = datebuf;
                    node->cmp.is_numeric = false;
                } else {
                    return util::Error{7200, 0,
                        "unsupported function call on RHS of WHERE comparison: " + id, sql};
                }
            } else {
                node->cmp.is_outer_ref       = true;
                node->cmp.outer_column       = id;
                node->cmp.outer_column_alias = id_alias;   // for N-way joins
                node->cmp.is_numeric         = false;
                node->cmp.literal            = id;     // diagnostic fallback
            }
        }
    }
    return node;
}

util::Result<std::unique_ptr<WhereExpr>>
parse_primary(Cursor& c, const std::string& sql) {
    if (c.match_keyword("NOT")) {
        auto inner = parse_primary(c, sql);
        if (!inner) return inner.error();
        auto n = std::make_unique<WhereExpr>();
        n->kind  = WhereExpr::Kind::Not;
        n->child = std::move(inner).value();
        return n;
    }
    if (c.match_keyword("EXISTS")) {
        if (!c.match_char('(')) {
            return util::Error{7200, 0, "expected '(' after EXISTS", sql};
        }
        if (!c.match_keyword("SELECT")) {
            return util::Error{7200, 0,
                "expected SELECT inside EXISTS", sql};
        }
        std::string inner = "SELECT ";
        int depth = 1;
        while (depth > 0) {
            if (c.eof()) {
                return util::Error{7200, 0,
                    "unterminated EXISTS subquery", sql};
            }
            char ch = c.consume_char();
            if (ch == '(') { ++depth; inner.push_back('('); continue; }
            if (ch == ')') {
                --depth;
                if (depth == 0) break;
                inner.push_back(')');
                continue;
            }
            inner.push_back(ch);
        }
        auto sub = parse_select(inner);
        if (!sub) return sub.error();
        auto n = std::make_unique<WhereExpr>();
        n->kind = WhereExpr::Kind::Exists;
        n->exists_subquery =
            std::make_unique<SelectStmt>(std::move(sub).value());
        return n;
    }
    if (c.match_char('(')) {
        auto inner = parse_or_expr(c, sql);
        if (!inner) return inner.error();
        if (!c.match_char(')')) {
            return util::Error{7200, 0,
                "expected ')' to close grouped WHERE expression", sql};
        }
        return inner;
    }
    return parse_cmp(c, sql);
}

util::Result<std::unique_ptr<WhereExpr>>
parse_and_expr(Cursor& c, const std::string& sql) {
    auto first = parse_primary(c, sql);
    if (!first) return first.error();
    std::unique_ptr<WhereExpr> lhs = std::move(first).value();
    while (c.match_keyword("AND")) {
        auto rhs = parse_primary(c, sql);
        if (!rhs) return rhs.error();
        auto node = std::make_unique<WhereExpr>();
        node->kind = WhereExpr::Kind::And;
        node->children.push_back(std::move(lhs));
        node->children.push_back(std::move(rhs).value());
        lhs = std::move(node);
    }
    return lhs;
}

util::Result<std::unique_ptr<WhereExpr>>
parse_or_expr(Cursor& c, const std::string& sql) {
    auto first = parse_and_expr(c, sql);
    if (!first) return first.error();
    std::unique_ptr<WhereExpr> lhs = std::move(first).value();
    while (c.match_keyword("OR")) {
        auto rhs = parse_and_expr(c, sql);
        if (!rhs) return rhs.error();
        auto node = std::make_unique<WhereExpr>();
        node->kind = WhereExpr::Kind::Or;
        node->children.push_back(std::move(lhs));
        node->children.push_back(std::move(rhs).value());
        lhs = std::move(node);
    }
    return lhs;
}

} // namespace

util::Result<SelectStmt> parse_select(const std::string& sql) {
    // ADS dialect — second table of a two-table comma-join, captured in the
    // FROM clause and lowered into stmt.inner_join after the WHERE is parsed.
    std::string comma_join_table;
    // M10.48 — Common Table Expression: `WITH name AS (SELECT ...)
    // SELECT ... FROM name`. Inline-substitute by replacing the CTE
    // name in the body with `(<inner SQL>)` so the body parses as a
    // regular SELECT-with-derived-table. Single CTE for first cut.
    {
        Cursor probe(sql);
        if (probe.match_keyword("WITH")) {
            std::string name = probe.read_identifier();
            if (name.empty() || !probe.match_keyword("AS")) {
                return util::Error{7200, 0,
                    "expected `<name> AS (SELECT ...)` after WITH", sql};
            }
            if (!probe.match_char('(')) {
                return util::Error{7200, 0,
                    "expected '(' after WITH ... AS", sql};
            }
            std::string inner;
            int depth = 1;
            while (depth > 0) {
                if (probe.eof()) {
                    return util::Error{7200, 0,
                        "unterminated CTE body", sql};
                }
                char ch = probe.consume_char();
                if (ch == '(') { ++depth; inner.push_back('('); continue; }
                if (ch == ')') {
                    --depth;
                    if (depth == 0) break;
                    inner.push_back(')');
                    continue;
                }
                inner.push_back(ch);
            }
            // Pull the rest of the SQL — everything after the CTE
            // body's closing ')' is the body SELECT. Locate via raw
            // substring search.
            std::string upper_sql;
            upper_sql.reserve(sql.size());
            for (char ch2 : sql) upper_sql.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(ch2))));
            std::size_t with_pos = upper_sql.find("WITH");
            std::size_t open = sql.find('(', with_pos);
            int d = 1; std::size_t k = open + 1;
            while (k < sql.size() && d > 0) {
                if (sql[k] == '(') ++d;
                else if (sql[k] == ')') --d;
                ++k;
            }
            std::string body = sql.substr(k);
            // Replace the CTE's name as a whole-word token in `body`
            // with `(<inner>)`. Case-sensitive whole-word match.
            std::string body_upper;
            body_upper.reserve(body.size());
            for (char ch2 : body) body_upper.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(ch2))));
            std::string name_upper;
            name_upper.reserve(name.size());
            for (char ch2 : name) name_upper.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(ch2))));
            std::string rebuilt;
            rebuilt.reserve(body.size() + inner.size() + 4);
            std::size_t i = 0;
            while (i < body.size()) {
                if (body_upper.compare(i, name_upper.size(),
                                       name_upper) == 0) {
                    bool lb = (i == 0) ||
                        (!std::isalnum(
                            static_cast<unsigned char>(body[i - 1])) &&
                         body[i - 1] != '_');
                    std::size_t end = i + name_upper.size();
                    bool rb = (end == body.size()) ||
                        (!std::isalnum(
                            static_cast<unsigned char>(body[end])) &&
                         body[end] != '_');
                    if (lb && rb) {
                        rebuilt.push_back('(');
                        rebuilt.append(inner);
                        rebuilt.push_back(')');
                        i = end;
                        continue;
                    }
                }
                rebuilt.push_back(body[i]);
                ++i;
            }
            return parse_select(rebuilt);
        }
    }

    Cursor c(sql);
    if (!c.match_keyword("SELECT")) {
        return util::Error{7200, 0, "expected SELECT", sql};
    }
    SelectStmt stmt;
    // ADS dialect — optional `{static}` / `{...}` cursor hint after SELECT.
    c.skip_optimizer_hint();
    // M10.31 — optional DISTINCT immediately after SELECT.
    if (c.match_keyword("DISTINCT")) stmt.distinct = true;
    // TOP N — Transact-SQL/xBase synonym for LIMIT N. Consume before
    // the projection list so TOP/1 aren't read as column names.
    if (c.match_keyword("TOP")) {
        auto top_n = c.read_numeric_literal();
        if (top_n && stmt.limit < 0) stmt.limit = static_cast<std::int64_t>(top_n.value());
    }
    if (c.match_char('*')) {
        // SELECT * — projection stays empty (every column visible).
    } else {
        // Projection list (M10.8) or aggregate list (M10.10).
        // Aggregates take the form `KIND(*)` or `KIND(col)` where
        // KIND is one of COUNT / SUM / AVG / MIN / MAX. The first
        // item decides the mode for the whole list — mixing plain
        // columns and aggregates in the same SELECT is not
        // supported in this milestone.
        bool aggregate_mode = false;
        for (;;) {
            // M10.38 — projection item may be `CASE WHEN ... END`.
            if (c.match_keyword("CASE")) {
                if (aggregate_mode) {
                    return util::Error{7200, 0,
                        "mixing CASE + aggregates in SELECT not supported", sql};
                }
                CaseExpr ce;
                while (c.match_keyword("WHEN")) {
                    auto cond = parse_or_expr(c, sql);
                    if (!cond) return cond.error();
                    if (!c.match_keyword("THEN")) {
                        return util::Error{7200, 0,
                            "expected THEN in CASE", sql};
                    }
                    CaseLiteral lit;
                    if (c.peek_char('\'')) {
                        auto s = c.read_string_literal();
                        if (!s) return s.error();
                        lit.text       = std::move(s).value();
                        lit.is_numeric = false;
                    } else {
                        auto n = c.read_numeric_literal();
                        if (!n) return n.error();
                        lit.is_numeric = true;
                        lit.number     = n.value();
                        char tmp[64];
                        std::snprintf(tmp, sizeof(tmp), "%g", n.value());
                        lit.text       = tmp;
                    }
                    CaseBranch br;
                    br.cond       = std::move(cond).value();
                    br.then_value = std::move(lit);
                    ce.branches.push_back(std::move(br));
                }
                if (c.match_keyword("ELSE")) {
                    CaseLiteral lit;
                    if (c.peek_char('\'')) {
                        auto s = c.read_string_literal();
                        if (!s) return s.error();
                        lit.text       = std::move(s).value();
                        lit.is_numeric = false;
                    } else {
                        auto n = c.read_numeric_literal();
                        if (!n) return n.error();
                        lit.is_numeric = true;
                        lit.number     = n.value();
                        char tmp[64];
                        std::snprintf(tmp, sizeof(tmp), "%g", n.value());
                        lit.text       = tmp;
                    }
                    ce.has_else   = true;
                    ce.else_value = std::move(lit);
                }
                if (!c.match_keyword("END")) {
                    return util::Error{7200, 0,
                        "expected END to close CASE", sql};
                }
                if (c.match_keyword("AS")) {
                    ce.alias = c.read_identifier();
                }
                std::size_t idx = stmt.case_items.size();
                stmt.case_items.push_back(std::move(ce));
                char placeholder[32];
                std::snprintf(placeholder, sizeof(placeholder),
                              "$CASE_%zu", idx);
                stmt.projection.push_back(placeholder);
                if (c.match_char(',')) continue;
                break;
            }
            std::string head_alias;
            bool        head_wild = false;
            std::string head = c.read_identifier(&head_alias, &head_wild);
            if (head_wild) {
                // ADS dialect — qualified wildcard `<alias>.*` projection
                // item (e.g. `mov.*`). Recorded for the N-way join path;
                // the single/two-table paths use `projection` and never
                // emit this form.
                if (aggregate_mode) {
                    return util::Error{7200, 0,
                        "mixing wildcard + aggregates in SELECT not supported",
                        sql};
                }
                stmt.select_items.push_back(
                    SelectItem{head_alias, std::string(), true});
                if (c.match_char(',')) continue;
                break;
            }
            if (head.empty()) {
                return util::Error{7200, 0,
                    "expected '*' or column name in SELECT", sql};
            }
            std::string upper;
            upper.reserve(head.size());
            for (char ch : head) {
                upper.push_back(static_cast<char>(std::toupper(
                    static_cast<unsigned char>(ch))));
            }
            bool is_agg_call = (upper == "COUNT" || upper == "SUM" ||
                                upper == "AVG"   || upper == "MIN" ||
                                upper == "MAX") && c.peek_char('(');

            // M10.47 / M10.49 / M10.50 — window functions:
            // ROW_NUMBER / RANK / DENSE_RANK. OVER clause may carry
            // PARTITION BY <col>[, ...] and ORDER BY <col> [ASC|DESC].
            bool is_window =
                (upper == "ROW_NUMBER" || upper == "RANK" ||
                 upper == "DENSE_RANK") && c.peek_char('(');
            if (is_window) {
                if (aggregate_mode) {
                    return util::Error{7200, 0,
                        "mixing window fn + aggregates in SELECT not supported",
                        sql};
                }
                c.match_char('(');
                if (!c.match_char(')')) {
                    return util::Error{7200, 0,
                        "expected ')' after window function name", sql};
                }
                WindowFnCall wf;
                if      (upper == "ROW_NUMBER") wf.kind = WindowFnKind::RowNumber;
                else if (upper == "RANK")       wf.kind = WindowFnKind::Rank;
                else                            wf.kind = WindowFnKind::DenseRank;
                if (c.match_keyword("OVER")) {
                    if (!c.match_char('(')) {
                        return util::Error{7200, 0,
                            "expected '(' after OVER", sql};
                    }
                    if (c.match_keyword("PARTITION")) {
                        if (!c.match_keyword("BY")) {
                            return util::Error{7200, 0,
                                "expected BY after PARTITION", sql};
                        }
                        for (;;) {
                            std::string col = c.read_identifier();
                            if (col.empty()) {
                                return util::Error{7200, 0,
                                    "expected column in PARTITION BY", sql};
                            }
                            wf.partition_by.push_back(std::move(col));
                            if (!c.match_char(',')) break;
                        }
                    }
                    if (c.match_keyword("ORDER")) {
                        if (!c.match_keyword("BY")) {
                            return util::Error{7200, 0,
                                "expected BY after ORDER (window OVER)", sql};
                        }
                        OrderBy ob;
                        ob.column = c.read_identifier();
                        if (ob.column.empty()) {
                            return util::Error{7200, 0,
                                "expected column in ORDER BY (window)", sql};
                        }
                        if      (c.match_keyword("DESC")) ob.descending = true;
                        else if (c.match_keyword("ASC"))  ob.descending = false;
                        wf.order_by = std::move(ob);
                    }
                    if (!c.match_char(')')) {
                        return util::Error{7200, 0,
                            "expected ')' to close OVER", sql};
                    }
                }
                if (c.match_keyword("AS")) {
                    wf.alias = c.read_identifier();
                }
                std::size_t wi = stmt.window_items.size();
                stmt.window_items.push_back(std::move(wf));
                char placeholder[32];
                std::snprintf(placeholder, sizeof(placeholder),
                              "$WIN_%zu", wi);
                stmt.projection.push_back(placeholder);
                if (c.match_char(',')) continue;
                break;
            }

            // M10.39 / M10.43 / M10.45 — scalar function call.
            // Single-arg: UPPER/LOWER/LEN/TRIM/LTRIM/RTRIM(col).
            // Multi-arg: SUBSTR(col,start,len), CONCAT(a,b),
            // REPLACE(col,old,new), DATEDIFF(a,b), DATEADD(col,n).
            // Zero-arg: NOW(), TODAY(), DATE(), TIME().
            bool is_zero_arg   = (upper == "NOW"    || upper == "TODAY"  ||
                                  upper == "DATE"   || upper == "TIME"   ||
                                  upper == "CURDATE" || upper == "GETDATE")
                                  && c.peek_char('(');
            bool is_single_arg = (upper == "UPPER" || upper == "LOWER" ||
                                  upper == "LEN"   || upper == "TRIM"  ||
                                  upper == "LTRIM" || upper == "RTRIM") &&
                                  c.peek_char('(');
            bool is_multi_arg  = (upper == "SUBSTR"   || upper == "CONCAT"  ||
                                  upper == "REPLACE"  || upper == "DATEDIFF" ||
                                  upper == "DATEADD"  || upper == "NULLIF"   ||
                                  upper == "COALESCE" || upper == "IFNULL")
                                  && c.peek_char('(');
            if (is_zero_arg) {
                if (aggregate_mode) {
                    return util::Error{7200, 0,
                        "mixing scalar fn + aggregates in SELECT not supported",
                        sql};
                }
                c.match_char('('); c.match_char(')');
                ScalarFnCall fn;
                if      (upper == "NOW")   fn.kind = ScalarFnKind::Now;
                else if (upper == "TODAY") fn.kind = ScalarFnKind::Today;
                else if (upper == "DATE"  || upper == "CURDATE" ||
                         upper == "GETDATE") fn.kind = ScalarFnKind::Date;
                else                       fn.kind = ScalarFnKind::Time;
                if (c.match_keyword("AS")) fn.alias = c.read_identifier();
                std::size_t fi = stmt.fn_items.size();
                stmt.fn_items.push_back(std::move(fn));
                char placeholder[32];
                std::snprintf(placeholder, sizeof(placeholder), "$FN_%zu", fi);
                stmt.projection.push_back(placeholder);
                if (c.match_char(',')) continue;
                break;
            }
            if (is_single_arg || is_multi_arg) {
                if (aggregate_mode) {
                    return util::Error{7200, 0,
                        "mixing scalar fn + aggregates in SELECT not supported",
                        sql};
                }
                if (!c.match_char('(')) {
                    return util::Error{7200, 0,
                        "expected '(' after scalar function name", sql};
                }
                ScalarFnCall fn;
                if      (upper == "UPPER")    fn.kind = ScalarFnKind::Upper;
                else if (upper == "LOWER")    fn.kind = ScalarFnKind::Lower;
                else if (upper == "LEN")      fn.kind = ScalarFnKind::Len;
                else if (upper == "TRIM")     fn.kind = ScalarFnKind::Trim;
                else if (upper == "LTRIM")    fn.kind = ScalarFnKind::Ltrim;
                else if (upper == "RTRIM")    fn.kind = ScalarFnKind::Rtrim;
                else if (upper == "SUBSTR")   fn.kind = ScalarFnKind::Substr;
                else if (upper == "CONCAT")   fn.kind = ScalarFnKind::Concat;
                else if (upper == "REPLACE")  fn.kind = ScalarFnKind::Replace;
                else if (upper == "DATEDIFF") fn.kind = ScalarFnKind::DateDiff;
                else if (upper == "DATEADD")  fn.kind = ScalarFnKind::DateAdd;
                else if (upper == "NULLIF")   fn.kind = ScalarFnKind::NullIf;
                else if (upper == "COALESCE") fn.kind = ScalarFnKind::Coalesce;
                else                          fn.kind = ScalarFnKind::IfNull;
                if (is_single_arg) {
                    fn.column = c.read_identifier();
                    if (fn.column.empty()) {
                        return util::Error{7200, 0,
                            "expected column name inside scalar function", sql};
                    }
                } else {
                    for (;;) {
                        ScalarFnArg arg;
                        if (c.peek_char('\'')) {
                            auto s = c.read_string_literal();
                            if (!s) return s.error();
                            arg.is_column  = false;
                            arg.is_numeric = false;
                            arg.text       = std::move(s).value();
                        } else {
                            // Try numeric literal first.
                            auto n = c.read_numeric_literal();
                            if (n) {
                                arg.is_column  = false;
                                arg.is_numeric = true;
                                arg.number     = n.value();
                            } else {
                                std::string id = c.read_identifier();
                                if (id.empty()) {
                                    return util::Error{7200, 0,
                                        "expected arg in scalar function call",
                                        sql};
                                }
                                arg.is_column = true;
                                arg.column    = std::move(id);
                            }
                        }
                        fn.args.push_back(std::move(arg));
                        if (c.match_char(',')) continue;
                        break;
                    }
                }
                if (!c.match_char(')')) {
                    return util::Error{7200, 0,
                        "expected ')' to close scalar function", sql};
                }
                if (c.match_keyword("AS")) {
                    fn.alias = c.read_identifier();
                }
                std::size_t fi = stmt.fn_items.size();
                stmt.fn_items.push_back(std::move(fn));
                char placeholder[32];
                std::snprintf(placeholder, sizeof(placeholder),
                              "$FN_%zu", fi);
                stmt.projection.push_back(placeholder);
                if (c.match_char(',')) continue;
                break;
            }

            // Unknown identifier followed by '(' → user-defined function call.
            // Consume the argument list so the parser stays in sync.
            if (!is_zero_arg && !is_single_arg && !is_multi_arg && !is_agg_call
                && c.peek_char('(')) {
                if (aggregate_mode) {
                    return util::Error{7200, 0,
                        "UDF calls cannot be mixed with aggregates", sql};
                }
                c.match_char('(');
                ScalarFnCall fn;
                fn.kind    = ScalarFnKind::Udf;
                fn.fn_name = head;   // save original (case-preserved) name
                // Consume arguments (comma-separated literals / identifiers)
                if (!c.peek_char(')')) {
                    for (;;) {
                        ScalarFnArg arg;
                        if (c.peek_char('\'')) {
                            auto s = c.read_string_literal();
                            if (!s) return s.error();
                            arg.is_column  = false;
                            arg.is_numeric = false;
                            arg.text       = std::move(s).value();
                        } else {
                            auto n = c.read_numeric_literal();
                            if (n) {
                                arg.is_column  = false;
                                arg.is_numeric = true;
                                arg.number     = n.value();
                            } else {
                                std::string id = c.read_identifier();
                                if (!id.empty() && c.peek_char('(')) {
                                    // nested function call (e.g. Curdate()) —
                                    // consume balanced parens and store the whole
                                    // expression as a raw text literal
                                    std::string raw = id;
                                    raw.push_back('(');
                                    c.match_char('(');
                                    int depth = 1;
                                    while (depth > 0) {
                                        if (c.eof()) {
                                            return util::Error{7200, 0,
                                                "unterminated function call in UDF argument list", sql};
                                        }
                                        char ch = c.consume_char();
                                        raw.push_back(ch);
                                        if      (ch == '(') ++depth;
                                        else if (ch == ')') --depth;
                                    }
                                    arg.is_column  = false;
                                    arg.is_numeric = false;
                                    arg.is_call    = true;
                                    arg.text       = std::move(raw);
                                } else {
                                    arg.is_column = true;
                                    arg.column    = std::move(id);
                                }
                            }
                        }
                        fn.args.push_back(std::move(arg));
                        if (c.match_char(',')) continue;
                        break;
                    }
                }
                if (!c.match_char(')')) {
                    return util::Error{7200, 0,
                        "expected ')' to close UDF argument list", sql};
                }
                if (c.match_keyword("AS")) fn.alias = c.read_identifier();
                std::size_t fi = stmt.fn_items.size();
                stmt.fn_items.push_back(std::move(fn));
                char placeholder[32];
                std::snprintf(placeholder, sizeof(placeholder), "$FN_%zu", fi);
                stmt.projection.push_back(placeholder);
                if (c.match_char(',')) continue;
                break;
            }

            if (is_agg_call) {
                if (!stmt.projection.empty()) {
                    return util::Error{7200, 0,
                        "mixing plain columns + aggregates in SELECT not supported", sql};
                }
                aggregate_mode = true;
                if (!c.match_char('(')) {
                    return util::Error{7200, 0,
                        "expected '(' after aggregate name", sql};
                }
                Aggregate agg;
                if (upper == "COUNT") agg.kind = AggregateKind::Count;
                if (upper == "SUM")   agg.kind = AggregateKind::Sum;
                if (upper == "AVG")   agg.kind = AggregateKind::Avg;
                if (upper == "MIN")   agg.kind = AggregateKind::Min;
                if (upper == "MAX")   agg.kind = AggregateKind::Max;
                if (c.match_char('*')) {
                    if (upper != "COUNT") {
                        return util::Error{7200, 0,
                            "* argument only valid for COUNT", sql};
                    }
                    agg.kind = AggregateKind::CountStar;
                } else {
                    std::string col = c.read_identifier();
                    if (col.empty()) {
                        return util::Error{7200, 0,
                            "expected column name inside aggregate", sql};
                    }
                    agg.column = std::move(col);
                }
                if (!c.match_char(')')) {
                    return util::Error{7200, 0,
                        "expected ')' to close aggregate", sql};
                }
                // M10.54 — optional FILTER (WHERE <expr>) tail.
                if (c.match_keyword("FILTER")) {
                    if (!c.match_char('(')) {
                        return util::Error{7200, 0,
                            "expected '(' after FILTER", sql};
                    }
                    if (!c.match_keyword("WHERE")) {
                        return util::Error{7200, 0,
                            "expected WHERE inside FILTER", sql};
                    }
                    auto root = parse_or_expr(c, sql);
                    if (!root) return root.error();
                    agg.filter.reset(root.value().release());
                    if (!c.match_char(')')) {
                        return util::Error{7200, 0,
                            "expected ')' to close FILTER", sql};
                    }
                }
                if (c.match_keyword("AS")) {
                    agg.alias = c.read_identifier();
                }
                stmt.aggregates.push_back(std::move(agg));
            } else {
                if (aggregate_mode) {
                    return util::Error{7200, 0,
                        "mixing plain columns + aggregates in SELECT not supported", sql};
                }
                // M10.40 — arithmetic projection: `<col> {+,-,*,/}
                // <num_or_col>`. Detect binary operator after the
                // first identifier; if absent, treat `head` as a
                // plain column reference.
                bool is_arith =
                    c.peek_char('+') || c.peek_char('-') ||
                    c.peek_char('*') || c.peek_char('/');
                if (is_arith) {
                    ArithExpr a;
                    a.lhs_column = head;
                    if      (c.match_char('+')) a.op = ArithOp::Add;
                    else if (c.match_char('-')) a.op = ArithOp::Sub;
                    else if (c.match_char('*')) a.op = ArithOp::Mul;
                    else                          { c.match_char('/');
                                                    a.op = ArithOp::Div; }
                    auto n = c.read_numeric_literal();
                    if (n) {
                        a.rhs_is_literal = true;
                        a.rhs_number     = n.value();
                    } else {
                        std::string id = c.read_identifier();
                        if (id.empty()) {
                            return util::Error{7200, 0,
                                "expected number or column on RHS of arith",
                                sql};
                        }
                        a.rhs_is_literal = false;
                        a.rhs_column     = std::move(id);
                    }
                    if (c.match_keyword("AS")) {
                        a.alias = c.read_identifier();
                    }
                    std::size_t ai = stmt.arith_items.size();
                    stmt.arith_items.push_back(std::move(a));
                    char placeholder[32];
                    std::snprintf(placeholder, sizeof(placeholder),
                                  "$ARITH_%zu", ai);
                    stmt.projection.push_back(placeholder);
                    if (c.match_char(',')) continue;
                    break;
                }
                // ADS dialect — record the alias-qualified column for the
                // N-way join path (kept index-independent from `projection`,
                // which the single/two-table paths consume).
                stmt.select_items.push_back(
                    SelectItem{head_alias, head, false});
                stmt.projection.push_back(std::move(head));
            }
            if (c.match_char(',')) continue;
            break;
        }
    }
    // ADS dialect — the N-way join executor only handles plain-column and
    // `<alias>.*` projections (captured in select_items). Flag any complex
    // projection item so that path errors clearly instead of silently
    // dropping columns. The single/two-table paths ignore this.
    stmt.projection_complex = !stmt.case_items.empty()   ||
                              !stmt.aggregates.empty()   ||
                              !stmt.fn_items.empty()     ||
                              !stmt.arith_items.empty()  ||
                              !stmt.window_items.empty();
    if (!c.match_keyword("FROM")) {
        return util::Error{7200, 0, "expected FROM", sql};
    }
    // M10.46 — `FROM (SELECT ...) [AS alias]`. Capture the inner
    // SELECT text + scan past the matching ')' so subsequent
    // WHERE / ORDER BY parse against the outer cursor.
    if (c.match_char('(')) {
        if (!c.peek_keyword("SELECT")) {
            return util::Error{7200, 0,
                "expected SELECT inside derived table", sql};
        }
        std::string inner = "SELECT ";
        c.match_keyword("SELECT");
        int depth = 1;
        while (depth > 0) {
            if (c.eof()) {
                return util::Error{7200, 0,
                    "unterminated derived table", sql};
            }
            char ch = c.consume_char();
            if (ch == '(') { ++depth; inner.push_back('('); continue; }
            if (ch == ')') {
                --depth;
                if (depth == 0) break;
                inner.push_back(')');
                continue;
            }
            inner.push_back(ch);
        }
        stmt.derived_sql = std::move(inner);
        if (c.match_keyword("AS")) {
            stmt.derived_alias = c.read_identifier();
        }
    } else {
        stmt.table = c.read_identifier_or_filename();
        // ADS dialect — optional table alias: `FROM <table> AS <alias>` or
        // the bare `FROM <table> <alias>` form. The alias is recorded in
        // from_tables; the N-way join executor needs it to disambiguate
        // same-named columns across tables. Guard the bare form so a
        // following clause keyword is not eaten as an alias.
        if (c.match_keyword("AS")) {
            stmt.table_alias = c.read_identifier();
        } else if (!c.peek_keyword("WHERE")  && !c.peek_keyword("GROUP")  &&
                   !c.peek_keyword("ORDER")  && !c.peek_keyword("HAVING") &&
                   !c.peek_keyword("LIMIT")  && !c.peek_keyword("OFFSET") &&
                   !c.peek_keyword("INNER")  && !c.peek_keyword("LEFT")   &&
                   !c.peek_keyword("RIGHT")  && !c.peek_keyword("FULL")   &&
                   !c.peek_keyword("JOIN")   && !c.peek_keyword("ON")) {
            std::string maybe_alias = c.read_identifier();
            if (!maybe_alias.empty()) stmt.table_alias = std::move(maybe_alias);
        }
        stmt.from_tables.push_back(FromTable{stmt.table, stmt.table_alias});
        // ADS dialect — SQL-89 comma-join: `FROM a, b, c, ... WHERE a.x=b.y`.
        // Legacy ADS apps write the join as a comma list with the equality
        // predicates in the WHERE. Collect every table (with its alias).
        // The two-table case keeps the proven single-JoinClause lowering
        // below; three-or-more tables route to the N-way join executor,
        // which consumes from_tables + the WHERE equalities directly.
        while (c.match_char(',')) {
            std::string tname = c.read_identifier_or_filename();
            if (tname.empty()) {
                return util::Error{7200, 0,
                    "expected table name after ',' in FROM", sql};
            }
            std::string talias;
            if (c.match_keyword("AS")) {
                talias = c.read_identifier();
            } else if (!c.peek_keyword("WHERE")  && !c.peek_keyword("GROUP") &&
                       !c.peek_keyword("ORDER")  && !c.peek_keyword("HAVING")&&
                       !c.peek_keyword("LIMIT")  && !c.peek_keyword("OFFSET")&&
                       !c.peek_keyword("INNER")  && !c.peek_keyword("LEFT")  &&
                       !c.peek_keyword("RIGHT")  && !c.peek_keyword("FULL")  &&
                       !c.peek_keyword("JOIN")   && !c.peek_keyword("ON")) {
                talias = c.read_identifier();
            }
            stmt.from_tables.push_back(FromTable{tname, talias});
        }
        // Two tables → keep the existing single-JoinClause lowering (the
        // equality is lifted out of the WHERE below). Three or more → leave
        // comma_join_table empty so that lowering is skipped and the N-way
        // executor takes over.
        if (stmt.from_tables.size() == 2) {
            comma_join_table = stmt.from_tables[1].name;
        }
    }
    bool is_left_join  = false;
    bool is_right_join = false;
    bool is_full_join  = false;
    bool saw_join_keyword = false;
    if (c.match_keyword("LEFT")) {
        c.match_keyword("OUTER");   // optional
        if (!c.match_keyword("JOIN")) {
            return util::Error{7200, 0, "expected JOIN after LEFT", sql};
        }
        is_left_join = true;
        saw_join_keyword = true;
    } else if (c.match_keyword("RIGHT")) {
        c.match_keyword("OUTER");   // optional
        if (!c.match_keyword("JOIN")) {
            return util::Error{7200, 0, "expected JOIN after RIGHT", sql};
        }
        is_right_join = true;
        saw_join_keyword = true;
    } else if (c.match_keyword("FULL")) {
        c.match_keyword("OUTER");   // optional
        if (!c.match_keyword("JOIN")) {
            return util::Error{7200, 0, "expected JOIN after FULL", sql};
        }
        is_full_join = true;
        saw_join_keyword = true;
    } else if (c.match_keyword("INNER")) {
        if (!c.match_keyword("JOIN")) {
            return util::Error{7200, 0, "expected JOIN after INNER", sql};
        }
        saw_join_keyword = true;
    } else if (c.match_keyword("JOIN")) {
        // Bare JOIN — treated as INNER per SQL convention.
        saw_join_keyword = true;
    }
    if (saw_join_keyword && !comma_join_table.empty()) {
        return util::Error{7200, 0,
            "cannot mix comma-join with an explicit JOIN clause", sql};
    }
    if (saw_join_keyword) {
        JoinClause j;
        j.is_left  = is_left_join;
        j.is_right = is_right_join;
        j.is_full  = is_full_join;
        j.table = c.read_identifier_or_filename();
        if (j.table.empty()) {
            return util::Error{7200, 0,
                "expected table name after INNER JOIN", sql};
        }
        if (!c.match_keyword("ON")) {
            return util::Error{7200, 0,
                "expected ON for INNER JOIN", sql};
        }
        // Parse `<lcol> = <rcol>`. Both columns are bare identifiers
        // — qualified `tbl.col` syntax lands in a later milestone.
        j.left_column = c.read_identifier();
        if (j.left_column.empty()) {
            return util::Error{7200, 0,
                "expected left join column", sql};
        }
        if (!c.match_char('=')) {
            return util::Error{7200, 0,
                "expected '=' in JOIN ON clause", sql};
        }
        j.right_column = c.read_identifier();
        if (j.right_column.empty()) {
            return util::Error{7200, 0,
                "expected right join column", sql};
        }
        stmt.inner_join = std::move(j);
    }
    if (stmt.table.empty() && stmt.derived_sql.empty()) {
        return util::Error{7200, 0, "expected table name", sql};
    }

    // Optional WHERE — full boolean tree with AND / OR / NOT / parens
    // and either string or numeric literals.
    if (c.match_keyword("WHERE")) {
        auto root = parse_or_expr(c, sql);
        if (!root) return root.error();
        stmt.where = std::move(root).value();
    }

    // ADS dialect — lower the two-table comma-join into stmt.inner_join.
    // The join key lives in the WHERE as a `col = col` equality, which the
    // comparison parser records as `op == Eq && is_outer_ref` (column RHS).
    // Lift the single such predicate into the JoinClause and blank that node
    // to an always-true empty-AND so the rest of the WHERE stays a row
    // filter. Zero candidates is a cartesian product (unsupported); more
    // than one is a composite key the single-equality JoinClause can't hold.
    if (!comma_join_table.empty()) {
        std::vector<WhereExpr*> keys;
        std::function<void(WhereExpr*)> collect = [&](WhereExpr* n) {
            if (n == nullptr) return;
            if (n->kind == WhereExpr::Kind::Cmp) {
                if (n->cmp.op == WhereOp::Eq && n->cmp.is_outer_ref) {
                    keys.push_back(n);
                }
                return;   // do not descend into subqueries of a Cmp leaf
            }
            for (auto& ch : n->children) collect(ch.get());
            collect(n->child.get());
            // In/Exists carry their own nested SELECT context — skip.
        };
        collect(stmt.where.get());
        if (keys.empty()) {
            return util::Error{7200, 0,
                "comma-join requires an equality join predicate "
                "(cartesian products are not supported)", sql};
        }
        if (keys.size() > 1) {
            return util::Error{7200, 0,
                "comma-join supports a single equality join key; "
                "use INNER JOIN ... ON for composite keys", sql};
        }
        WhereExpr* k = keys.front();
        JoinClause j;
        j.table        = comma_join_table;
        j.left_column  = k->cmp.column;
        j.right_column = k->cmp.outer_column;
        stmt.inner_join = std::move(j);
        // Blank the lifted predicate to an always-true empty-AND node.
        k->cmp      = WhereCmp{};
        k->kind     = WhereExpr::Kind::And;
        k->children.clear();
        k->child.reset();
    }

    // M10.25 — GROUP BY <col>[, <col>...] [HAVING <agg> <op> <num>].
    // Sits between WHERE and ORDER BY in the SQL grammar.
    if (c.match_keyword("GROUP")) {
        if (!c.match_keyword("BY")) {
            return util::Error{7200, 0, "expected BY after GROUP", sql};
        }
        for (;;) {
            std::string col = c.read_identifier();
            if (col.empty()) {
                return util::Error{7200, 0,
                    "expected column name in GROUP BY", sql};
            }
            stmt.group_by.push_back(std::move(col));
            if (!c.match_char(',')) break;
        }
    }
    if (c.match_keyword("HAVING")) {
        // M10.30 — recursive HAVING parser. Mirror parse_where_expr's
        // shape: OR-binds, AND-binds, NOT, parens, leaves. Each leaf
        // is `<agg-call> <op> <number>`.
        std::function<util::Result<std::unique_ptr<HavingExpr>>()> h_parse_or;
        std::function<util::Result<std::unique_ptr<HavingExpr>>()> h_parse_and;
        std::function<util::Result<std::unique_ptr<HavingExpr>>()> h_parse_primary;
        auto h_parse_leaf =
            [&]() -> util::Result<std::unique_ptr<HavingExpr>> {
            std::string head = c.read_identifier();
            std::string upper;
            upper.reserve(head.size());
            for (char ch : head) {
                upper.push_back(static_cast<char>(std::toupper(
                    static_cast<unsigned char>(ch))));
            }
            if (upper != "COUNT" && upper != "SUM" && upper != "AVG" &&
                upper != "MIN"   && upper != "MAX") {
                return util::Error{7200, 0,
                    "HAVING expects an aggregate call "
                    "(COUNT/SUM/AVG/MIN/MAX)", sql};
            }
            if (!c.match_char('(')) {
                return util::Error{7200, 0,
                    "expected '(' after aggregate name in HAVING", sql};
            }
            auto node = std::make_unique<HavingExpr>();
            node->kind = HavingExpr::Kind::Cmp;
            if (upper == "COUNT") node->cmp.agg.kind = AggregateKind::Count;
            if (upper == "SUM")   node->cmp.agg.kind = AggregateKind::Sum;
            if (upper == "AVG")   node->cmp.agg.kind = AggregateKind::Avg;
            if (upper == "MIN")   node->cmp.agg.kind = AggregateKind::Min;
            if (upper == "MAX")   node->cmp.agg.kind = AggregateKind::Max;
            if (c.match_char('*')) {
                if (upper != "COUNT") {
                    return util::Error{7200, 0,
                        "* argument only valid for COUNT", sql};
                }
                node->cmp.agg.kind = AggregateKind::CountStar;
            } else {
                std::string col = c.read_identifier();
                if (col.empty()) {
                    return util::Error{7200, 0,
                        "expected column name inside HAVING aggregate", sql};
                }
                node->cmp.agg.column = std::move(col);
            }
            if (!c.match_char(')')) {
                return util::Error{7200, 0,
                    "expected ')' to close HAVING aggregate", sql};
            }
            if      (c.match_seq("<=")) node->cmp.op = WhereOp::Le;
            else if (c.match_seq(">=")) node->cmp.op = WhereOp::Ge;
            else if (c.match_seq("<>")) node->cmp.op = WhereOp::Ne;
            else if (c.match_seq("!=")) node->cmp.op = WhereOp::Ne;
            else if (c.match_char('=')) node->cmp.op = WhereOp::Eq;
            else if (c.match_char('<')) node->cmp.op = WhereOp::Lt;
            else if (c.match_char('>')) node->cmp.op = WhereOp::Gt;
            else return util::Error{7200, 0,
                "expected =, !=, <>, <, >, <= or >= after HAVING aggregate", sql};
            auto n = c.read_numeric_literal();
            if (!n) return n.error();
            node->cmp.num = n.value();
            return node;
        };
        h_parse_primary = [&]() -> util::Result<std::unique_ptr<HavingExpr>> {
            if (c.match_keyword("NOT")) {
                auto inner = h_parse_primary();
                if (!inner) return inner.error();
                std::unique_ptr<HavingExpr> ip;
                ip.reset(inner.value().release());
                auto node = std::make_unique<HavingExpr>();
                node->kind  = HavingExpr::Kind::Not;
                node->child = std::move(ip);
                return node;
            }
            if (c.match_char('(')) {
                auto inner = h_parse_or();
                if (!inner) return inner.error();
                if (!c.match_char(')')) {
                    return util::Error{7200, 0,
                        "expected ')' in HAVING expression", sql};
                }
                std::unique_ptr<HavingExpr> ip;
                ip.reset(inner.value().release());
                return ip;
            }
            return h_parse_leaf();
        };
        h_parse_and = [&]() -> util::Result<std::unique_ptr<HavingExpr>> {
            auto left = h_parse_primary();
            if (!left) return left.error();
            std::unique_ptr<HavingExpr> lhs;
            lhs.reset(left.value().release());
            while (c.match_keyword("AND")) {
                auto right = h_parse_primary();
                if (!right) return right.error();
                std::unique_ptr<HavingExpr> rhs;
                rhs.reset(right.value().release());
                auto node = std::make_unique<HavingExpr>();
                node->kind = HavingExpr::Kind::And;
                node->children.push_back(std::move(lhs));
                node->children.push_back(std::move(rhs));
                lhs = std::move(node);
            }
            return lhs;
        };
        h_parse_or = [&]() -> util::Result<std::unique_ptr<HavingExpr>> {
            auto left = h_parse_and();
            if (!left) return left.error();
            std::unique_ptr<HavingExpr> lhs;
            lhs.reset(left.value().release());
            while (c.match_keyword("OR")) {
                auto right = h_parse_and();
                if (!right) return right.error();
                std::unique_ptr<HavingExpr> rhs;
                rhs.reset(right.value().release());
                auto node = std::make_unique<HavingExpr>();
                node->kind = HavingExpr::Kind::Or;
                node->children.push_back(std::move(lhs));
                node->children.push_back(std::move(rhs));
                lhs = std::move(node);
            }
            return lhs;
        };
        auto root = h_parse_or();
        if (!root) return root.error();
        stmt.having.reset(root.value().release());
    }

    // M10.6 / M10.37 — ORDER BY <col> [ASC|DESC] [, <col> ...].
    if (c.match_keyword("ORDER")) {
        if (!c.match_keyword("BY")) {
            return util::Error{7200, 0,
                "expected BY after ORDER", sql};
        }
        bool first = true;
        for (;;) {
            OrderBy ob;
            ob.column = c.read_identifier();
            if (ob.column.empty()) {
                return util::Error{7200, 0,
                    "expected column name in ORDER BY", sql};
            }
            if      (c.match_keyword("DESC")) ob.descending = true;
            else if (c.match_keyword("ASC"))  ob.descending = false;
            if (first) { stmt.order_by = std::move(ob); first = false; }
            else        stmt.order_by_extra.push_back(std::move(ob));
            if (!c.match_char(',')) break;
        }
    }

    // M10.32 — `LIMIT N [OFFSET M]`. Sits at the tail of the SELECT.
    if (c.match_keyword("LIMIT")) {
        auto n = c.read_numeric_literal();
        if (!n) return n.error();
        stmt.limit = static_cast<std::int64_t>(n.value());
        if (c.match_keyword("OFFSET")) {
            auto o = c.read_numeric_literal();
            if (!o) return o.error();
            stmt.offset = static_cast<std::int64_t>(o.value());
        }
    }

    c.match_char(';');
    return stmt;
}

bool sql_is_insert(const std::string& sql) {
    Cursor c(sql);
    return c.match_keyword("INSERT");
}

bool sql_is_update(const std::string& sql) {
    Cursor c(sql);
    return c.match_keyword("UPDATE");
}

bool sql_is_delete(const std::string& sql) {
    Cursor c(sql);
    return c.match_keyword("DELETE");
}

bool sql_is_create_table(const std::string& sql) {
    Cursor c(sql);
    return c.match_keyword("CREATE") && c.match_keyword("TABLE");
}

bool sql_is_create_index(const std::string& sql) {
    Cursor c(sql);
    if (!c.match_keyword("CREATE")) return false;
    c.match_keyword("UNIQUE");  // optional UNIQUE
    return c.match_keyword("INDEX");
}

bool sql_is_create_procedure(const std::string& sql) {
    Cursor c(sql);
    return c.match_keyword("CREATE") && c.match_keyword("PROCEDURE");
}

bool sql_is_execute_procedure(const std::string& sql) {
    Cursor c(sql);
    return c.match_keyword("EXECUTE") && c.match_keyword("PROCEDURE");
}

util::Result<CreateProcedureStmt>
parse_create_procedure(const std::string& sql) {
    Cursor c(sql);
    if (!c.match_keyword("CREATE")) {
        return util::Error{7200, 0, "expected CREATE", sql};
    }
    if (!c.match_keyword("PROCEDURE")) {
        return util::Error{7200, 0, "expected PROCEDURE", sql};
    }
    CreateProcedureStmt stmt;
    stmt.name = c.read_identifier();
    if (stmt.name.empty()) {
        return util::Error{7200, 0, "expected procedure name", sql};
    }
    if (!c.match_keyword("AS")) {
        return util::Error{7200, 0, "expected AS", sql};
    }
    auto lit = c.read_string_literal();
    if (!lit) return lit.error();
    std::string spec = std::move(lit).value();
    auto sep = spec.find("::");
    if (sep == std::string::npos) {
        return util::Error{7200, 0,
            "procedure spec must be '<dll_path>::<symbol>'", sql};
    }
    stmt.dll_path = spec.substr(0, sep);
    stmt.symbol   = spec.substr(sep + 2);
    if (stmt.dll_path.empty() || stmt.symbol.empty()) {
        return util::Error{7200, 0,
            "procedure spec must be '<dll_path>::<symbol>'", sql};
    }
    c.match_char(';');
    return stmt;
}

util::Result<ExecuteProcedureStmt>
parse_execute_procedure(const std::string& sql) {
    Cursor c(sql);
    if (!c.match_keyword("EXECUTE")) {
        return util::Error{7200, 0, "expected EXECUTE", sql};
    }
    if (!c.match_keyword("PROCEDURE")) {
        return util::Error{7200, 0, "expected PROCEDURE", sql};
    }
    ExecuteProcedureStmt stmt;
    stmt.name = c.read_identifier();
    if (stmt.name.empty()) {
        return util::Error{7200, 0, "expected procedure name", sql};
    }
    if (!c.match_char('(')) {
        return util::Error{7200, 0,
            "expected '(' after procedure name", sql};
    }
    if (!c.match_char(')')) {
        for (;;) {
            ExecuteProcedureArg a;
            if (c.peek_char('\'')) {
                auto s = c.read_string_literal();
                if (!s) return s.error();
                a.text       = std::move(s).value();
                a.is_numeric = false;
            } else {
                auto n = c.read_numeric_literal();
                if (!n) return n.error();
                a.is_numeric = true;
                a.number     = n.value();
                char tmp[64];
                std::snprintf(tmp, sizeof(tmp), "%g", n.value());
                a.text       = tmp;
            }
            stmt.args.push_back(std::move(a));
            if (c.match_char(',')) continue;
            break;
        }
        if (!c.match_char(')')) {
            return util::Error{7200, 0,
                "expected ')' to close argument list", sql};
        }
    }
    c.match_char(';');
    return stmt;
}

util::Result<CreateTableStmt>
parse_create_table(const std::string& sql) {
    Cursor c(sql);
    if (!c.match_keyword("CREATE")) {
        return util::Error{7200, 0, "expected CREATE", sql};
    }
    if (!c.match_keyword("TABLE")) {
        return util::Error{7200, 0, "expected TABLE", sql};
    }
    CreateTableStmt stmt;
    stmt.table = c.read_identifier_or_filename();
    if (stmt.table.empty()) {
        return util::Error{7200, 0, "expected table name", sql};
    }
    // M10.42 — `CREATE TABLE t AS SELECT ...`. The schema is taken
    // from the inner SELECT's result cursor at execution time.
    if (c.match_keyword("AS")) {
        if (!c.peek_keyword("SELECT")) {
            return util::Error{7200, 0,
                "expected SELECT after CREATE TABLE ... AS", sql};
        }
        std::string upper_sql;
        upper_sql.reserve(sql.size());
        for (char ch : sql) upper_sql.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(ch))));
        std::size_t sel = upper_sql.find("SELECT");
        if (sel == std::string::npos) {
            return util::Error{7200, 0,
                "CREATE TABLE ... AS — could not locate inner SELECT",
                sql};
        }
        stmt.select_sql = sql.substr(sel);
        while (!stmt.select_sql.empty() &&
               (stmt.select_sql.back() == ';' ||
                std::isspace(static_cast<unsigned char>(
                    stmt.select_sql.back())))) {
            stmt.select_sql.pop_back();
        }
        return stmt;
    }
    if (!c.match_char('(')) {
        return util::Error{7200, 0,
            "expected '(' to open column list", sql};
    }
    for (;;) {
        CreateTableColumn col;
        col.name = c.read_identifier();
        if (col.name.empty()) {
            return util::Error{7200, 0,
                "expected column name in CREATE TABLE", sql};
        }
        col.type = c.read_identifier();
        if (col.type.empty()) {
            return util::Error{7200, 0,
                "expected type after column name", sql};
        }
        if (c.match_char('(')) {
            auto n = c.read_numeric_literal();
            if (!n) return n.error();
            col.length = static_cast<std::uint32_t>(n.value());
            if (c.match_char(',')) {
                auto d = c.read_numeric_literal();
                if (!d) return d.error();
                col.decimals = static_cast<std::uint32_t>(d.value());
            }
            if (!c.match_char(')')) {
                return util::Error{7200, 0,
                    "expected ')' to close column type", sql};
            }
        }
        stmt.columns.push_back(std::move(col));
        if (c.match_char(',')) continue;
        break;
    }
    if (!c.match_char(')')) {
        return util::Error{7200, 0,
            "expected ')' to close column list", sql};
    }
    c.match_char(';');
    return stmt;
}

util::Result<CreateIndexStmt>
parse_create_index(const std::string& sql) {
    Cursor c(sql);
    if (!c.match_keyword("CREATE")) {
        return util::Error{7200, 0, "expected CREATE", sql};
    }
    CreateIndexStmt stmt;
    if (c.match_keyword("UNIQUE")) {
        stmt.unique = true;
    }
    if (!c.match_keyword("INDEX")) {
        return util::Error{7200, 0, "expected INDEX", sql};
    }
    stmt.tag = c.read_identifier();
    if (stmt.tag.empty()) {
        return util::Error{7200, 0, "expected index tag name", sql};
    }
    if (!c.match_keyword("ON")) {
        return util::Error{7200, 0, "expected ON", sql};
    }
    stmt.table = c.read_identifier_or_filename();
    if (stmt.table.empty()) {
        return util::Error{7200, 0, "expected table name", sql};
    }
    if (!c.match_char('(')) {
        return util::Error{7200, 0,
            "expected '(' to open index expression", sql};
    }
    // Read everything up to the matching ')' as the expression. The
    // existing engine evaluator handles compound expressions like
    // UPPER(name) so identifiers + parens nest correctly. consume_char
    // skips no whitespace so the inner buffer keeps every byte.
    std::string expr;
    int depth = 1;
    while (depth > 0) {
        if (c.eof()) {
            return util::Error{7200, 0,
                "unterminated CREATE INDEX expression", sql};
        }
        char ch = c.consume_char();
        if (ch == '(') { ++depth; expr.push_back('('); continue; }
        if (ch == ')') {
            --depth;
            if (depth == 0) break;
            expr.push_back(')');
            continue;
        }
        expr.push_back(ch);
    }
    while (!expr.empty() &&
           std::isspace(static_cast<unsigned char>(expr.front()))) {
        expr.erase(expr.begin());
    }
    while (!expr.empty() &&
           std::isspace(static_cast<unsigned char>(expr.back()))) {
        expr.pop_back();
    }
    stmt.expression = std::move(expr);
    if (c.match_keyword("DESCENDING") || c.match_keyword("DESC")) {
        stmt.descending = true;
    }
    if (c.match_keyword("UNIQUE")) {
        stmt.unique = true;
    }
    c.match_char(';');
    return stmt;
}

util::Result<UpdateStmt> parse_update(const std::string& sql) {
    Cursor c(sql);
    if (!c.match_keyword("UPDATE")) {
        return util::Error{7200, 0, "expected UPDATE", sql};
    }
    UpdateStmt stmt;
    stmt.table = c.read_identifier_or_filename();
    if (stmt.table.empty()) {
        return util::Error{7200, 0, "expected table name", sql};
    }
    if (!c.match_keyword("SET")) {
        return util::Error{7200, 0, "expected SET", sql};
    }
    for (;;) {
        UpdateAssign a;
        a.column = c.read_identifier();
        if (a.column.empty()) {
            return util::Error{7200, 0,
                "expected column name in SET clause", sql};
        }
        if (!c.match_char('=')) {
            return util::Error{7200, 0,
                "expected '=' after column name", sql};
        }
        if (c.peek_char('\'')) {
            auto s = c.read_string_literal();
            if (!s) return s.error();
            a.value.is_numeric = false;
            a.value.text       = std::move(s).value();
        } else {
            auto n = c.read_numeric_literal();
            if (!n) return n.error();
            a.value.is_numeric = true;
            a.value.number     = n.value();
        }
        stmt.assignments.push_back(std::move(a));
        if (c.match_char(',')) continue;
        break;
    }
    if (c.match_keyword("WHERE")) {
        auto root = parse_or_expr(c, sql);
        if (!root) return root.error();
        stmt.where = std::move(root).value();
    }
    c.match_char(';');
    return stmt;
}

util::Result<DeleteStmt> parse_delete(const std::string& sql) {
    Cursor c(sql);
    if (!c.match_keyword("DELETE")) {
        return util::Error{7200, 0, "expected DELETE", sql};
    }
    if (!c.match_keyword("FROM")) {
        return util::Error{7200, 0, "expected FROM after DELETE", sql};
    }
    DeleteStmt stmt;
    stmt.table = c.read_identifier_or_filename();
    if (stmt.table.empty()) {
        return util::Error{7200, 0, "expected table name", sql};
    }
    if (c.match_keyword("WHERE")) {
        auto root = parse_or_expr(c, sql);
        if (!root) return root.error();
        stmt.where = std::move(root).value();
    }
    c.match_char(';');
    return stmt;
}

util::Result<InsertStmt> parse_insert(const std::string& sql) {
    Cursor c(sql);
    if (!c.match_keyword("INSERT")) {
        return util::Error{7200, 0, "expected INSERT", sql};
    }
    if (!c.match_keyword("INTO")) {
        return util::Error{7200, 0, "expected INTO after INSERT", sql};
    }
    InsertStmt stmt;
    stmt.table = c.read_identifier_or_filename();
    if (stmt.table.empty()) {
        return util::Error{7200, 0, "expected table name", sql};
    }

    // Optional column list. Older xBase apps omit it (column order
    // matches table definition); we require it for clarity.
    if (!c.match_char('(')) {
        return util::Error{7200, 0,
            "expected '(' to open INSERT column list", sql};
    }
    for (;;) {
        std::string col = c.read_identifier();
        if (col.empty()) {
            return util::Error{7200, 0,
                "expected column name in INSERT list", sql};
        }
        stmt.columns.push_back(std::move(col));
        if (c.match_char(',')) continue;
        break;
    }
    if (!c.match_char(')')) {
        return util::Error{7200, 0,
            "expected ')' to close INSERT column list", sql};
    }

    // M10.41 — `INSERT INTO t (cols) SELECT ...`. Capture the inner
    // SELECT text and let the executor recurse into the SQL pipeline
    // to materialise it.
    if (c.peek_keyword("SELECT")) {
        // Rebuild the SELECT substring from `sql`. We can locate it
        // via a case-insensitive scan starting at the cursor's
        // current position by searching for "SELECT" forward in the
        // input.
        std::string upper_sql;
        upper_sql.reserve(sql.size());
        for (char ch : sql) upper_sql.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(ch))));
        std::size_t after_close_paren = sql.find(')', 0);
        std::size_t sel = upper_sql.find("SELECT", after_close_paren);
        if (sel == std::string::npos) {
            return util::Error{7200, 0,
                "INSERT INTO ... SELECT — could not locate inner SELECT",
                sql};
        }
        stmt.select_sql = sql.substr(sel);
        // Strip trailing ';' if any.
        while (!stmt.select_sql.empty() &&
               (stmt.select_sql.back() == ';' ||
                std::isspace(static_cast<unsigned char>(
                    stmt.select_sql.back())))) {
            stmt.select_sql.pop_back();
        }
        return stmt;
    }

    if (!c.match_keyword("VALUES")) {
        return util::Error{7200, 0, "expected VALUES or SELECT", sql};
    }
    // M10.52 — `VALUES (...), (...), ...` — multi-row form. Each
    // tuple lands in `stmt.rows`; the legacy `stmt.values` carries
    // the first tuple for back-compat with the single-row path.
    for (;;) {
        if (!c.match_char('(')) {
            return util::Error{7200, 0,
                "expected '(' to open VALUES list", sql};
        }
        std::vector<InsertLiteral> row;
        for (;;) {
            InsertLiteral lit;
            if (c.peek_char('\'')) {
                auto s = c.read_string_literal();
                if (!s) return s.error();
                lit.is_numeric = false;
                lit.text       = std::move(s).value();
            } else {
                auto n = c.read_numeric_literal();
                if (!n) return n.error();
                lit.is_numeric = true;
                lit.number     = n.value();
            }
            row.push_back(std::move(lit));
            if (c.match_char(',')) continue;
            break;
        }
        if (!c.match_char(')')) {
            return util::Error{7200, 0,
                "expected ')' to close VALUES list", sql};
        }
        if (stmt.columns.size() != row.size()) {
            return util::Error{7200, 0,
                "INSERT column count must match VALUES count", sql};
        }
        stmt.rows.push_back(std::move(row));
        if (!c.match_char(',')) break;
    }
    // Single-row callers expect `stmt.values`; preserve back-compat.
    if (stmt.rows.size() == 1) {
        stmt.values = stmt.rows.front();
        stmt.rows.clear();
    }

    c.match_char(';');
    return stmt;
}

bool sql_is_create_database(const std::string& sql) {
    Cursor c(sql);
    return c.match_keyword("CREATE") && c.match_keyword("DATABASE");
}

bool sql_is_grant(const std::string& sql) {
    Cursor c(sql);
    return c.match_keyword("GRANT");
}

bool sql_is_revoke(const std::string& sql) {
    Cursor c(sql);
    return c.match_keyword("REVOKE");
}

util::Result<CreateDatabaseStmt> parse_create_database(const std::string& sql) {
    Cursor c(sql);
    if (!c.match_keyword("CREATE"))
        return util::Error{7200, 0, "expected CREATE", sql};
    if (!c.match_keyword("DATABASE"))
        return util::Error{7200, 0, "expected DATABASE", sql};
    CreateDatabaseStmt stmt;
    // Path may be double-quoted (ADS convention) or single-quoted or bare
    if (c.peek_char('"')) {
        c.consume_char();
        while (!c.eof()) {
            char ch = c.consume_char();
            if (ch == '"') break;
            stmt.path.push_back(ch);
        }
    } else if (c.peek_char('\'')) {
        auto s = c.read_string_literal();
        if (!s) return s.error();
        stmt.path = s.value();
    } else {
        stmt.path = c.read_identifier_or_filename();
    }
    if (stmt.path.empty())
        return util::Error{7200, 0, "expected database path", sql};
    for (;;) {
        if (c.match_keyword("PASSWORD")) {
            auto s = c.read_string_literal();
            if (s) stmt.password = s.value();
        } else if (c.match_keyword("DESCRIPTION")) {
            auto s = c.read_string_literal();
            if (s) stmt.description = s.value();
        } else if (c.match_keyword("ENCRYPT")) {
            std::string val = c.read_identifier();
            for (auto& ch : val) ch = static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch)));
            stmt.encrypt = (val == "true" || val == "1" || val == "yes");
        } else {
            break;
        }
    }
    return stmt;
}

static util::Result<GrantStmt>
parse_grant_impl(const std::string& sql, bool is_revoke) {
    Cursor c(sql);
    if (is_revoke) {
        if (!c.match_keyword("REVOKE"))
            return util::Error{7200, 0, "expected REVOKE", sql};
    } else {
        if (!c.match_keyword("GRANT"))
            return util::Error{7200, 0, "expected GRANT", sql};
    }
    GrantStmt stmt;
    stmt.is_revoke = is_revoke;
    stmt.right = c.read_identifier();
    for (auto& ch : stmt.right)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    if (stmt.right.empty())
        return util::Error{7200, 0, "expected right type after GRANT/REVOKE", sql};
    // Optional ("col") column-level specifier
    if (c.match_char('(')) {
        if (c.peek_char('"')) {
            c.consume_char();
            while (!c.eof()) {
                char ch = c.consume_char();
                if (ch == '"') break;
                stmt.column.push_back(ch);
            }
        } else if (c.peek_char('\'')) {
            auto s = c.read_string_literal();
            if (s) stmt.column = s.value();
        } else {
            stmt.column = c.read_identifier();
        }
        if (!c.match_char(')'))
            return util::Error{7200, 0, "expected ')' after column name", sql};
    }
    if (!c.match_keyword("ON"))
        return util::Error{7200, 0, "expected ON", sql};
    stmt.object = c.read_identifier();
    if (stmt.object.empty())
        return util::Error{7200, 0, "expected object name after ON", sql};
    if (!is_revoke) {
        if (!c.match_keyword("TO"))
            return util::Error{7200, 0, "expected TO after object name", sql};
    } else {
        if (!c.match_keyword("FROM"))
            return util::Error{7200, 0, "expected FROM after object name", sql};
    }
    // Principal: bare identifier or [NAME] for group/ALL
    if (c.match_char('[')) {
        stmt.principal = c.read_identifier();
        c.match_char(']');
    } else {
        stmt.principal = c.read_identifier();
    }
    if (stmt.principal.empty())
        return util::Error{7200, 0, "expected user or group name", sql};
    return stmt;
}

util::Result<GrantStmt> parse_grant(const std::string& sql) {
    return parse_grant_impl(sql, false);
}

util::Result<GrantStmt> parse_revoke(const std::string& sql) {
    return parse_grant_impl(sql, true);
}

} // namespace openads::sql
