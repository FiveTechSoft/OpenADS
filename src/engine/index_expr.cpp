#include "engine/index_expr.h"

#include "engine/table.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <variant>

namespace openads::engine {

namespace {

// ---- Tokenizer ------------------------------------------------------

enum class TokKind {
    Ident, Number, String, LParen, RParen, Comma, Plus, End
};

struct Token {
    TokKind     kind;
    std::string text;
};

std::vector<Token> tokenize(const std::string& s) {
    std::vector<Token> out;
    std::size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (c == '(') { out.push_back({TokKind::LParen, "("}); ++i; continue; }
        if (c == ')') { out.push_back({TokKind::RParen, ")"}); ++i; continue; }
        if (c == ',') { out.push_back({TokKind::Comma,  ","}); ++i; continue; }
        if (c == '+') { out.push_back({TokKind::Plus,   "+"}); ++i; continue; }
        if (c == '"' || c == '\'') {
            char q = c; ++i;
            std::string lit;
            while (i < s.size() && s[i] != q) { lit.push_back(s[i++]); }
            if (i < s.size()) ++i;
            out.push_back({TokKind::String, std::move(lit)});
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '-' && i + 1 < s.size() &&
             std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
            std::string num;
            if (c == '-') { num.push_back(c); ++i; }
            while (i < s.size() &&
                   (std::isdigit(static_cast<unsigned char>(s[i])) ||
                    s[i] == '.')) {
                num.push_back(s[i++]);
            }
            out.push_back({TokKind::Number, std::move(num)});
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::string id;
            while (i < s.size() &&
                   (std::isalnum(static_cast<unsigned char>(s[i])) ||
                    s[i] == '_')) {
                id.push_back(s[i++]);
            }
            out.push_back({TokKind::Ident, std::move(id)});
            continue;
        }
        ++i;   // skip the unknown char rather than die — best-effort.
    }
    out.push_back({TokKind::End, ""});
    return out;
}

} // namespace (anonymous)

// Harbour key / FOR expressions arrive with the workarea alias still
// attached — `INDEX ON CUST->NAME` reaches the RDD as the literal
// text "CUST->NAME". An index belongs to exactly one table, so an
// `ALIAS->FIELD` qualifier always denotes a field of that table;
// drop the `ALIAS->` so the rest of the evaluator sees a plain field
// reference. (The tokenizer otherwise discards `-` and `>` as stray
// punctuation, leaving the alias to parse as an unknown identifier —
// every key then evaluates blank and the index degenerates to
// record order.) Works inside calls too: UPPER(CUST->NAME).
std::string strip_alias_qualifiers(const std::string& expr) {
    std::string out;
    std::size_t i = 0;
    while (i < expr.size()) {
        unsigned char c = static_cast<unsigned char>(expr[i]);
        if (std::isalpha(c) || c == '_') {
            std::size_t j = i;
            while (j < expr.size() &&
                   (std::isalnum(static_cast<unsigned char>(expr[j])) ||
                    expr[j] == '_')) {
                ++j;
            }
            std::size_t k = j;
            while (k < expr.size() &&
                   std::isspace(static_cast<unsigned char>(expr[k]))) {
                ++k;
            }
            if (k + 1 < expr.size() && expr[k] == '-' && expr[k + 1] == '>') {
                i = k + 2;          // discard `<ident> ->`
                continue;
            }
            out.append(expr, i, j - i);
            i = j;
            continue;
        }
        out.push_back(expr[i]);
        ++i;
    }
    return out;
}

namespace {

// ---- AST + evaluator ------------------------------------------------

struct Value {
    bool        is_number = false;
    std::string s;
    double      n = 0.0;
};

// ---- UTF-8 codepoint walker (M9.22) --------------------------------------
//
// UPPER / LOWER / SUBSTR walk codepoints instead of bytes so an index
// expression like UPPER(name) over a UTF-8 column normalises non-ASCII
// characters (ñ → Ñ, ç → Ç, é → É, …) instead of leaving the multi-byte
// sequence unchanged. The case-mapping table covers ASCII + Latin-1
// supplement + the U+00FF / U+0178 pair; codepoints outside that range
// pass through untouched (full Unicode case folding is ICU territory
// and out of scope until the engine carries an ICU dependency).

std::uint32_t utf8_decode_at(const std::string& s, std::size_t& i) {
    if (i >= s.size()) return 0xFFFD;
    unsigned char c = static_cast<unsigned char>(s[i]);
    std::uint32_t cp = 0xFFFD;
    std::size_t adv = 1;
    auto byte = [&](std::size_t k) {
        return static_cast<unsigned char>(s[k]);
    };
    if (c < 0x80) {
        cp = c;
    } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size() &&
               (byte(i + 1) & 0xC0) == 0x80) {
        cp = (static_cast<std::uint32_t>(c & 0x1F) << 6) |
              static_cast<std::uint32_t>(byte(i + 1) & 0x3F);
        adv = 2;
    } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size() &&
               (byte(i + 1) & 0xC0) == 0x80 &&
               (byte(i + 2) & 0xC0) == 0x80) {
        cp = (static_cast<std::uint32_t>(c & 0x0F) << 12) |
             (static_cast<std::uint32_t>(byte(i + 1) & 0x3F) << 6) |
              static_cast<std::uint32_t>(byte(i + 2) & 0x3F);
        adv = 3;
    } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size() &&
               (byte(i + 1) & 0xC0) == 0x80 &&
               (byte(i + 2) & 0xC0) == 0x80 &&
               (byte(i + 3) & 0xC0) == 0x80) {
        cp = (static_cast<std::uint32_t>(c & 0x07) << 18) |
             (static_cast<std::uint32_t>(byte(i + 1) & 0x3F) << 12) |
             (static_cast<std::uint32_t>(byte(i + 2) & 0x3F) << 6) |
              static_cast<std::uint32_t>(byte(i + 3) & 0x3F);
        adv = 4;
    }
    i += adv;
    return cp;
}

void utf8_encode(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

std::uint32_t to_upper_cp(std::uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') return cp - 0x20;
    // Latin-1 supplement: à..þ → À..Þ, skipping ÷ at 0xF7.
    if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 0x20;
    if (cp == 0xFF) return 0x178;   // ÿ → Ÿ
    return cp;
}

std::uint32_t to_lower_cp(std::uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 0x20;
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 0x20;
    if (cp == 0x178) return 0xFF;
    return cp;
}

std::string upper_utf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        std::uint32_t cp = utf8_decode_at(s, i);
        utf8_encode(out, to_upper_cp(cp));
    }
    return out;
}

std::string lower_utf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        std::uint32_t cp = utf8_decode_at(s, i);
        utf8_encode(out, to_lower_cp(cp));
    }
    return out;
}

std::string substr_utf8(const std::string& s, int start_1based, int len) {
    if (start_1based < 1) start_1based = 1;
    if (len < 0) len = 0;
    std::string out;
    std::size_t i = 0;
    int char_index = 0;       // 0-based codepoint counter
    int copied = 0;
    while (i < s.size() && copied < len) {
        std::size_t before = i;
        (void)utf8_decode_at(s, i);   // advance one codepoint
        if (char_index + 1 >= start_1based) {
            out.append(s, before, i - before);
            ++copied;
        }
        ++char_index;
        if (i == before) break;   // safety on malformed input
    }
    return out;
}

std::string ltrim_ascii(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}
std::string rtrim_ascii(const std::string& s) {
    std::size_t e = s.size();
    while (e > 0 && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(0, e);
}

// Shared scalar-function evaluator, used by BOTH the key-expression parser
// (Parser, below) and the FOR-clause truthy evaluator (eval_cmp, further
// down). Keeping a single implementation means a FOR condition like
// `UPPER(SUBS(ALLTRIM(col),1,5)) == 'ABC'` evaluates byte-identically to the
// key path — previously the FOR evaluator only knew RECNO()/DELETED() and
// returned an empty string for every other call, so such conditions matched
// no rows and the conditional index came out EMPTY.
std::string format_number_fn(double v, int width, int dec) {
    char buf[64];
    if (width <= 0) {
        std::snprintf(buf, sizeof(buf), "%.*f", dec > 0 ? dec : 0, v);
    } else {
        std::snprintf(buf, sizeof(buf), "%*.*f", width, dec > 0 ? dec : 0, v);
    }
    std::string out = buf;
    if (width > 0 && static_cast<int>(out.size()) > width) {
        out.resize(static_cast<std::size_t>(width));
    }
    return out;
}

Value apply_scalar_fn(const std::string& fn, const std::vector<Value>& args) {
    Value v;
    v.is_number = false;
    if (fn == "UPPER" && args.size() >= 1) {
        v.s = upper_utf8(args[0].s);
    } else if (fn == "LOWER" && args.size() >= 1) {
        v.s = lower_utf8(args[0].s);
    } else if (fn == "LTRIM" && args.size() >= 1) {
        v.s = ltrim_ascii(args[0].s);
    } else if ((fn == "RTRIM" || fn == "TRIM") && args.size() >= 1) {
        v.s = rtrim_ascii(args[0].s);
    } else if (fn == "ALLTRIM" && args.size() >= 1) {
        v.s = rtrim_ascii(ltrim_ascii(args[0].s));
    } else if (fn == "DTOS" && args.size() >= 1) {
        // Date fields are already YYYYMMDD on disk; return the raw bytes.
        v.s = args[0].s;
    } else if (fn == "STR") {
        int width = args.size() >= 2 && args[1].is_number
                  ? static_cast<int>(args[1].n) : 10;
        int dec   = args.size() >= 3 && args[2].is_number
                  ? static_cast<int>(args[2].n) : 0;
        double n = args[0].is_number ? args[0].n
                 : std::strtod(args[0].s.c_str(), nullptr);
        v.s = format_number_fn(n, width, dec);
    } else if ((fn == "SUBSTR" || fn == "SUBS") && args.size() >= 2) {
        const std::string& s = args[0].s;
        int start = args[1].is_number ? static_cast<int>(args[1].n) : 1;
        int len   = args.size() >= 3 && args[2].is_number
                  ? static_cast<int>(args[2].n)
                  : static_cast<int>(s.size());
        v.s = substr_utf8(s, start, len);
    } else if (fn == "VAL" && args.size() >= 1) {
        v.is_number = true;
        v.n = std::strtod(args[0].s.c_str(), nullptr);
    } else if ((fn == "AT" || fn == "ATNUM") && args.size() >= 2) {
        // AT(needle, haystack) / ATNUM(needle, haystack [, occurrence]) ->
        // 1-based position of the (occurrence-th) match, else 0. Used by the
        // browse "contains" search as `ATNUM(...) > 0`.
        const std::string& needle = args[0].s;
        const std::string& hay    = args[1].s;
        int occ = (fn == "ATNUM" && args.size() >= 3 && args[2].is_number)
                ? (static_cast<int>(args[2].n) < 1 ? 1
                   : static_cast<int>(args[2].n))
                : 1;
        v.is_number = true;
        v.n = 0.0;
        if (!needle.empty()) {
            std::size_t pos = 0;
            int found = 0;
            while (true) {
                std::size_t p = hay.find(needle, pos);
                if (p == std::string::npos) break;
                if (++found == occ) { v.n = static_cast<double>(p + 1); break; }
                pos = p + 1;
            }
        }
    } else {
        // Unknown function — empty string (FoxPro/ADS would error; we degrade).
    }
    return v;
}

class Parser {
public:
    Parser(const std::vector<Token>& toks, Table& t)
        : toks_(toks), t_(t) {}

    Value parse_expr() {
        Value lhs = parse_term();
        while (peek().kind == TokKind::Plus) {
            ++pos_;
            Value rhs = parse_term();
            // String concatenation; numeric values are stringified
            // first (Clipper semantics for + on mixed operands are
            // unsupported in CDX expressions).
            std::string a = lhs.is_number ? format_number_fn(lhs.n, 0, 0) : lhs.s;
            std::string b = rhs.is_number ? format_number_fn(rhs.n, 0, 0) : rhs.s;
            lhs.is_number = false;
            lhs.s = a + b;
        }
        return lhs;
    }

private:
    const Token& peek() const { return toks_[pos_]; }

    Value parse_term() {
        const Token& tk = peek();
        if (tk.kind == TokKind::String) {
            ++pos_;
            Value v;
            v.is_number = false;
            v.s = tk.text;
            return v;
        }
        if (tk.kind == TokKind::Number) {
            ++pos_;
            Value v;
            v.is_number = true;
            v.n = std::strtod(tk.text.c_str(), nullptr);
            return v;
        }
        if (tk.kind == TokKind::LParen) {
            ++pos_;
            Value v = parse_expr();
            if (peek().kind == TokKind::RParen) ++pos_;
            return v;
        }
        if (tk.kind == TokKind::Ident) {
            std::string name = tk.text;
            ++pos_;
            if (peek().kind == TokKind::LParen) {
                ++pos_;
                std::vector<Value> args;
                if (peek().kind != TokKind::RParen) {
                    args.push_back(parse_expr());
                    while (peek().kind == TokKind::Comma) {
                        ++pos_;
                        args.push_back(parse_expr());
                    }
                }
                if (peek().kind == TokKind::RParen) ++pos_;
                return apply_scalar_fn(upper_utf8(name), args);
            }
            return field_or_empty(name);
        }
        Value v;
        v.is_number = false;
        return v;
    }

    Value field_or_empty(const std::string& name) {
        Value v;
        v.is_number = false;
        std::int32_t fidx = t_.field_index(name);
        if (fidx < 0) return v;
        auto r = t_.read_field(static_cast<std::uint16_t>(fidx));
        if (!r) return v;
        const auto& f = t_.field_descriptor(static_cast<std::uint16_t>(fidx));
        if (f.type == drivers::DbfFieldType::Numeric ||
            f.type == drivers::DbfFieldType::Float ||
            f.type == drivers::DbfFieldType::Integer ||
            f.type == drivers::DbfFieldType::Double ||
            f.type == drivers::DbfFieldType::Currency ||
            f.type == drivers::DbfFieldType::AdtMoney) {
            v.is_number = true;
            v.n = r.value().as_double;
            // For arithmetic-style indexes the Clipper convention is
            // STR(numeric, len). The bare-field path returns the
            // *string* representation matching the on-disk bytes.
            v.s = r.value().as_string;
        } else {
            v.is_number = false;
            v.s = r.value().as_string;
        }
        return v;
    }

    const std::vector<Token>& toks_;
    std::size_t               pos_ = 0;
    Table&                    t_;
};

} // namespace

util::Result<std::string>
evaluate_index_expr(Table& t, const std::string& expr, std::uint16_t key_len) {
    // Memoise the alias-strip + tokenize step keyed by the raw expression.
    // These depend only on the expression text, not the row, so for a
    // CREATE INDEX / REINDEX that evaluates the same expression over every
    // record (and for every seek), tokenising ONCE instead of per-call
    // removes the dominant per-record allocation cost (a fresh vector<Token>
    // with per-token std::strings was built on each call). thread_local
    // avoids locking; the cache holds one tiny entry per distinct index
    // expression. The bare-field fast-path stays byte-identical.
    struct CompiledExpr {
        std::string        stripped;
        bool               bare = false;
        std::vector<Token> toks;
    };
    thread_local std::unordered_map<std::string, CompiledExpr> s_expr_cache;
    auto cit = s_expr_cache.find(expr);
    if (cit == s_expr_cache.end()) {
        CompiledExpr ce;
        ce.stripped = strip_alias_qualifiers(expr);
        ce.bare = !ce.stripped.empty() &&
            std::all_of(ce.stripped.begin(), ce.stripped.end(),
                [](char c) {
                    return std::isalnum(static_cast<unsigned char>(c)) ||
                           c == '_';
                });
        ce.toks = tokenize(ce.stripped);
        cit = s_expr_cache.emplace(expr, std::move(ce)).first;
    }
    const CompiledExpr& ce = cit->second;
    const std::string& e = ce.stripped;
    if (e.empty()) return std::string(key_len, ' ');

    // Fast-path: bare field-name expression (matches the M8.8 behaviour
    // exactly so existing CDX files round-trip identical bytes). Only when
    // the identifier resolves to a real column; otherwise fall through to
    // the general parser over the cached tokens.
    if (ce.bare && t.field_index(e) >= 0) {
        std::int32_t fidx = t.field_index(e);
        const auto& f = t.field_descriptor(static_cast<std::uint16_t>(fidx));
        auto r = t.read_field(static_cast<std::uint16_t>(fidx));
        if (!r) return r.error();
        std::string key = r.value().as_string;
        if (key.size() < key_len) key.append(key_len - key.size(), ' ');
        if (key.size() > key_len) key.resize(key_len);
        (void)f;
        return key;
    }

    Parser p(ce.toks, t);
    Value v = p.parse_expr();
    std::string s = v.s;
    if (s.size() < key_len) s.append(key_len - s.size(), ' ');
    if (s.size() > key_len) s.resize(key_len);
    return s;
}

// ---- M(rddads-compat) FOR-clause / scope truthy evaluator ----------------
//
// A tiny recursive-descent parser sufficient for the predicate forms
// rddtst.prg + most Clipper apps emit:
//
//   FNUM > 2 .AND. FNUM <= 4
//   FSTR == "3"
//   FNUM != 2 .AND. FNUM < 4
//   RECNO() <> 5
//
// Anything outside the supported grammar evaluates to TRUE (so the
// FOR clause degrades to "include every row" rather than corrupting
// the index — callers can still use it, just without the filter).
namespace {

struct Lex {
    const std::string& s;
    std::size_t i = 0;
    explicit Lex(const std::string& src) : s(src) {}

    void skip_ws() {
        while (i < s.size() && std::isspace(
                static_cast<unsigned char>(s[i]))) ++i;
    }
    bool eof() { skip_ws(); return i >= s.size(); }
    bool peek_kw(const char* kw) {
        skip_ws();
        std::size_t L = std::strlen(kw);
        if (i + L > s.size()) return false;
        for (std::size_t k = 0; k < L; ++k) {
            char a = static_cast<char>(std::toupper(
                static_cast<unsigned char>(s[i + k])));
            if (a != kw[k]) return false;
        }
        return true;
    }
    bool match_kw(const char* kw) {
        if (!peek_kw(kw)) return false;
        i += std::strlen(kw);
        return true;
    }
    bool match_char(char c) {
        skip_ws();
        if (i < s.size() && s[i] == c) { ++i; return true; }
        return false;
    }
};

bool eval_or(Lex& lx, Table& t);
bool eval_and(Lex& lx, Table& t);
bool eval_not(Lex& lx, Table& t);
bool eval_cmp(Lex& lx, Table& t);

Value parse_atom(Lex& lx, Table& t) {
    lx.skip_ws();
    Value v;
    if (lx.i < lx.s.size() &&
        (lx.s[lx.i] == '"' || lx.s[lx.i] == '\'')) {
        char q = lx.s[lx.i++];
        std::string lit;
        while (lx.i < lx.s.size() && lx.s[lx.i] != q) {
            lit.push_back(lx.s[lx.i++]);
        }
        if (lx.i < lx.s.size() && lx.s[lx.i] == q) ++lx.i;
        v.is_number = false; v.s = lit;
        return v;
    }
    // Number literal (including a leading minus).
    if (lx.i < lx.s.size() &&
        (std::isdigit(static_cast<unsigned char>(lx.s[lx.i])) ||
         (lx.s[lx.i] == '-' && lx.i + 1 < lx.s.size() &&
          std::isdigit(static_cast<unsigned char>(lx.s[lx.i + 1]))))) {
        std::string num;
        if (lx.s[lx.i] == '-') num.push_back(lx.s[lx.i++]);
        while (lx.i < lx.s.size() &&
               (std::isdigit(static_cast<unsigned char>(lx.s[lx.i])) ||
                lx.s[lx.i] == '.')) {
            num.push_back(lx.s[lx.i++]);
        }
        v.is_number = true;
        v.n = std::strtod(num.c_str(), nullptr);
        return v;
    }
    // Identifier / function call.
    if (lx.i < lx.s.size() &&
        (std::isalpha(static_cast<unsigned char>(lx.s[lx.i])) ||
         lx.s[lx.i] == '_')) {
        std::string name;
        while (lx.i < lx.s.size() &&
               (std::isalnum(static_cast<unsigned char>(lx.s[lx.i])) ||
                lx.s[lx.i] == '_')) {
            name.push_back(lx.s[lx.i++]);
        }
        std::string upper = upper_utf8(name);
        if (lx.match_char('(')) {
            // Only RECNO() / DELETED() are common in FOR clauses.
            std::vector<Value> args;
            lx.skip_ws();
            if (!lx.match_char(')')) {
                args.push_back(parse_atom(lx, t));
                while (lx.match_char(',')) args.push_back(parse_atom(lx, t));
                lx.match_char(')');
            }
            if (upper == "RECNO") {
                v.is_number = true;
                v.n = static_cast<double>(t.recno());
                return v;
            }
            if (upper == "DELETED") {
                v.is_number = true;
                v.n = t.is_deleted() ? 1.0 : 0.0;
                return v;
            }
            // Scalar functions (UPPER / SUBS / ALLTRIM / STR / DTOS / AT /
            // ATNUM / ...) — evaluate via the shared implementation so a FOR
            // condition like UPPER(SUBS(ALLTRIM(col),1,N)) == 'X' matches the
            // same rows ADS would. Previously every non-RECNO/DELETED call
            // returned "" here, so conditional-index FOR clauses matched no
            // rows and the temporary index (and its browse) came out EMPTY.
            return apply_scalar_fn(upper, args);
        }
        // Field lookup.
        std::int32_t fidx = t.field_index(name);
        if (fidx < 0) {
            v.is_number = false;
            return v;
        }
        auto r = t.read_field(static_cast<std::uint16_t>(fidx));
        if (!r) { v.is_number = false; return v; }
        const auto& f = t.field_descriptor(static_cast<std::uint16_t>(fidx));
        if (f.type == drivers::DbfFieldType::Numeric ||
            f.type == drivers::DbfFieldType::Float    ||
            f.type == drivers::DbfFieldType::Integer  ||
            f.type == drivers::DbfFieldType::Double   ||
            f.type == drivers::DbfFieldType::Currency ||
            f.type == drivers::DbfFieldType::AdtMoney) {
            v.is_number = true;
            v.n = r.value().as_double;
        } else if (f.type == drivers::DbfFieldType::Logical) {
            // A bare logical field as a truthy term (e.g. FOR ACTIVE) must
            // evaluate to its boolean value. Its as_string is "T"/"F" — both
            // non-empty — so the truthy fallback (!s.empty()) would match
            // every row and a conditional FOR index would index ALL records.
            // Carry it as a number (1/0) instead.
            v.is_number = true;
            v.n = r.value().as_bool ? 1.0 : 0.0;
        } else {
            v.is_number = false;
            std::string sv = r.value().as_string;
            while (!sv.empty() && sv.back() == ' ') sv.pop_back();
            v.s = sv;
        }
        return v;
    }
    return v;
}

bool eval_cmp(Lex& lx, Table& t) {
    lx.skip_ws();
    if (lx.match_char('(')) {
        bool inner = eval_or(lx, t);
        lx.match_char(')');
        return inner;
    }
    Value lhs = parse_atom(lx, t);
    lx.skip_ws();
    // Pick a comparison operator (longest match first).
    std::string op;
    auto try_op = [&](const char* s) {
        std::size_t L = std::strlen(s);
        if (lx.i + L > lx.s.size()) return false;
        if (std::memcmp(lx.s.data() + lx.i, s, L) != 0) return false;
        op.assign(s);
        lx.i += L;
        return true;
    };
    if (!try_op("==") && !try_op("!=") && !try_op("<>") &&
        !try_op(">=") && !try_op("<=") &&
        !try_op("=")  && !try_op(">")  && !try_op("<")  &&
        !try_op("#")) {
        // No comparison operator → treat lhs as truthy.
        return lhs.is_number ? (lhs.n != 0.0) : !lhs.s.empty();
    }
    Value rhs = parse_atom(lx, t);
    int cmp;
    if (lhs.is_number || rhs.is_number) {
        double a = lhs.is_number ? lhs.n : std::strtod(lhs.s.c_str(), nullptr);
        double b = rhs.is_number ? rhs.n : std::strtod(rhs.s.c_str(), nullptr);
        cmp = (a < b) ? -1 : (a > b ? 1 : 0);
    } else {
        cmp = lhs.s.compare(rhs.s);
        if (cmp < 0) cmp = -1; else if (cmp > 0) cmp = 1;
    }
    if (op == "==" || op == "=") return cmp == 0;
    if (op == "!=" || op == "<>" || op == "#") return cmp != 0;
    if (op == ">")  return cmp >  0;
    if (op == ">=") return cmp >= 0;
    if (op == "<")  return cmp <  0;
    if (op == "<=") return cmp <= 0;
    return true;
}

bool eval_not(Lex& lx, Table& t) {
    lx.skip_ws();
    if (lx.match_kw(".NOT.")) {
        return !eval_not(lx, t);
    }
    if (lx.match_char('!')) {
        return !eval_not(lx, t);
    }
    return eval_cmp(lx, t);
}

bool eval_and(Lex& lx, Table& t) {
    bool a = eval_not(lx, t);
    while (lx.match_kw(".AND.")) {
        bool b = eval_not(lx, t);
        a = a && b;
    }
    return a;
}

bool eval_or(Lex& lx, Table& t) {
    bool a = eval_and(lx, t);
    while (lx.match_kw(".OR.")) {
        bool b = eval_and(lx, t);
        a = a || b;
    }
    return a;
}

} // anonymous namespace

bool evaluate_index_expr_truthy(Table& t, const std::string& expr) {
    const std::string e = strip_alias_qualifiers(expr);
    if (e.empty()) return true;
    Lex lx(e);
    return eval_or(lx, t);
}

bool evaluate_index_expr_number(Table& t, const std::string& expr,
                                double& out) {
    const std::string e = strip_alias_qualifiers(expr);
    if (e.empty()) return false;
    auto toks = tokenize(e);
    Parser p(toks, t);
    Value v = p.parse_expr();
    if (!v.is_number) return false;
    out = v.n;
    return true;
}

std::string fox_numeric_key(double value) {
    // HB_DBL2ORD, little-endian host (hbdefs.h): big-endian byte order of
    // the IEEE-754 double, with the sign handled so unsigned byte compare
    // matches numeric order — non-negative flips the top bit, negative
    // inverts every byte.
    if (value == 0.0) value = 0.0;   // normalise -0.0 to +0.0
    const unsigned char* d = reinterpret_cast<const unsigned char*>(&value);
    unsigned char o[8];
    if (value >= 0.0) {
        o[0] = static_cast<unsigned char>(d[7] ^ 0x80);
        o[1] = d[6]; o[2] = d[5]; o[3] = d[4];
        o[4] = d[3]; o[5] = d[2]; o[6] = d[1]; o[7] = d[0];
    } else {
        o[0] = static_cast<unsigned char>(d[7] ^ 0xFF);
        o[1] = static_cast<unsigned char>(d[6] ^ 0xFF);
        o[2] = static_cast<unsigned char>(d[5] ^ 0xFF);
        o[3] = static_cast<unsigned char>(d[4] ^ 0xFF);
        o[4] = static_cast<unsigned char>(d[3] ^ 0xFF);
        o[5] = static_cast<unsigned char>(d[2] ^ 0xFF);
        o[6] = static_cast<unsigned char>(d[1] ^ 0xFF);
        o[7] = static_cast<unsigned char>(d[0] ^ 0xFF);
    }
    return std::string(reinterpret_cast<char*>(o), 8);
}

std::string ntx_numeric_key(double value, std::uint16_t width,
                            std::uint16_t dec) {
    if (value == 0.0) value = 0.0;          // normalise -0.0 to +0.0
    const bool neg = value < 0.0;
    // Clamp the format spec: an xBase numeric field is at most 255 wide, and
    // a runaway `dec` (or one wider than the field) would make snprintf spin
    // and overflow the buffer. The result is always normalised to exactly
    // `width` bytes below, so a clamped seek key still compares correctly.
    if (width > 255) width = 255;
    if (dec > width) dec = width;
    if (dec > 30)    dec = 30;
    // Zero-padded fixed-width magnitude, e.g. N(8,0) 42 -> "00000042",
    // N(12,2) 13.50 -> "000000013.50". The buffer covers width 255 plus a
    // very large integer part (huge seek values), sign, point and NUL.
    char buf[1024];
    int n = std::snprintf(buf, sizeof(buf), "%0*.*f",
                          static_cast<int>(width), static_cast<int>(dec),
                          neg ? -value : value);
    std::string out;
    if (n < 0) {
        out.assign(width, '0');
    } else {
        out.assign(buf, std::min<std::size_t>(static_cast<std::size_t>(n),
                                              sizeof(buf) - 1));
    }
    // Normalise to EXACTLY `width` bytes: pad short results (snprintf
    // truncation) with leading zeros, and keep the low `width` bytes of an
    // over-wide magnitude. The caller copies `width` bytes, so the key must
    // never be shorter than that.
    if (out.size() < width) {
        out.insert(0, static_cast<std::size_t>(width) - out.size(), '0');
    } else if (out.size() > width) {
        out = out.substr(out.size() - width);
    }
    if (neg) {
        for (char& c : out)
            c = static_cast<char>(0x5c -
                static_cast<unsigned char>(c));
    }
    return out;
}

} // namespace openads::engine
