// M-AOF.1 — recursive-descent parser for the filter-expression
// subset documented in aof_expr.h. The grammar is small enough that
// hand-rolled descent is clearer than a generator; it also keeps
// error reporting line-precise without an external dep.
//
// Grammar (V2):
//   expr    := orExpr
//   orExpr  := andExpr ( ("OR"|".OR.")  andExpr )*
//   andExpr := notExpr ( ("AND"|".AND.") notExpr )*
//   notExpr := ("NOT"|".NOT."|"!") notExpr | primary
//   primary := "(" expr ")" | leaf
//   leaf    := IDENT op literal
//            | IDENT "BETWEEN" literal "AND" literal
//            | IDENT "IN" "(" literal ("," literal)* ")"
//            | IDENT "LIKE" STRING
//            | IDENT "IS" "NULL"
//            | IDENT "IS" "NOT" "NULL"
//   op      := "=" | "==" | "!=" | "<>" | "#" | "<" | "<=" | ">" | ">="
//   literal := INT | REAL | STRING | "T"/"F"/".T."/".F."
//
// Behavioural notes:
//   * Identifiers and keywords are case-insensitive; literal strings
//     keep their case.
//   * Both Clipper-style (`.AND.`, `.OR.`, `.NOT.`, `.T.`, `.F.`) and
//     SQL-style (`AND`, `OR`, `NOT`, `T`, `F`) keywords are accepted
//     because rddads forwards either depending on the call site.
//   * On any unrecognised construct (function call, arithmetic, LIKE)
//     the parser returns Result<Error>; the caller treats this as
//     "cannot optimise" and falls back to a full table scan, which
//     matches pre-AOF behaviour exactly.

#include "engine/aof_expr.h"
#include "openads/error.h"

#include <cctype>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace openads::engine::aof {

namespace {

struct Token {
    enum class K {
        Ident, Number, String, Bool,
        Eq, Ne, Lt, Le, Gt, Ge,
        And, Or, Not, Between, In, Like, Is,
        LParen, RParen, Comma,
        End,
    };
    K           k;
    std::string s;             // raw text (for Ident / Number / String / Bool)
    std::int64_t i = 0;        // populated for Number when integer
    double      d = 0.0;       // populated for Number when real
    bool        is_real = false;
    bool        boolean = false;
};

class Lexer {
public:
    explicit Lexer(const std::string& src) : s_(src) {}

    util::Result<Token> next() {
        skip_ws();
        if (pos_ >= s_.size()) return tok(Token::K::End);

        char c = s_[pos_];

        // Strings: single or double quoted.
        if (c == '\'' || c == '"') return read_string(c);

        // Numbers.
        if (std::isdigit(static_cast<unsigned char>(c))) return read_number();

        // Clipper .AND. / .OR. / .NOT. / .T. / .F. / # operator.
        if (c == '.') {
            if (auto t = try_dot_keyword(); t) return *std::move(t);
            // bare leading dot like `.5` — treat as number.
            if (pos_ + 1 < s_.size() &&
                std::isdigit(static_cast<unsigned char>(s_[pos_ + 1]))) {
                return read_number();
            }
            return err("unexpected '.'");
        }

        // Multi-char operators and single-char tokens.
        if (c == '=') { ++pos_; if (pos_ < s_.size() && s_[pos_] == '=') ++pos_; return tok(Token::K::Eq); }
        if (c == '!' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '=') { pos_ += 2; return tok(Token::K::Ne); }
        if (c == '<' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '>') { pos_ += 2; return tok(Token::K::Ne); }
        if (c == '<' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '=') { pos_ += 2; return tok(Token::K::Le); }
        if (c == '>' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '=') { pos_ += 2; return tok(Token::K::Ge); }
        if (c == '<') { ++pos_; return tok(Token::K::Lt); }
        if (c == '>') { ++pos_; return tok(Token::K::Gt); }
        if (c == '!') { ++pos_; return tok(Token::K::Not); }
        if (c == '#') { ++pos_; return tok(Token::K::Ne); }
        if (c == '(') { ++pos_; return tok(Token::K::LParen); }
        if (c == ')') { ++pos_; return tok(Token::K::RParen); }
        if (c == ',') { ++pos_; return tok(Token::K::Comma); }

        // Identifiers / keywords.
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            return read_ident();
        }

        return err(std::string("unexpected character '") + c + "'");
    }

private:
    const std::string& s_;
    std::size_t        pos_ = 0;

    void skip_ws() {
        while (pos_ < s_.size() &&
               std::isspace(static_cast<unsigned char>(s_[pos_]))) {
            ++pos_;
        }
    }

    static Token tok(Token::K k) {
        Token t; t.k = k; return t;
    }

    util::Error err(std::string msg) const {
        std::ostringstream os;
        os << "AOF parse error at offset " << pos_ << ": " << msg;
        return util::Error{static_cast<std::int32_t>(openads::AE_PARSE_ERROR),
                           0, os.str(), {}};
    }

    util::Result<Token> read_string(char quote) {
        ++pos_;
        std::string out;
        while (pos_ < s_.size() && s_[pos_] != quote) {
            out.push_back(s_[pos_++]);
        }
        if (pos_ >= s_.size()) return err("unterminated string");
        ++pos_;
        Token t; t.k = Token::K::String; t.s = std::move(out);
        return t;
    }

    util::Result<Token> read_number() {
        std::size_t start = pos_;
        bool is_real = false;
        if (s_[pos_] == '-' || s_[pos_] == '+') ++pos_;
        while (pos_ < s_.size() &&
               std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        if (pos_ < s_.size() && s_[pos_] == '.') {
            is_real = true; ++pos_;
            while (pos_ < s_.size() &&
                   std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        Token t;
        t.k = Token::K::Number;
        t.s = s_.substr(start, pos_ - start);
        if (is_real) {
            t.is_real = true;
            t.d = std::strtod(t.s.c_str(), nullptr);
        } else {
            t.i = static_cast<std::int64_t>(std::strtoll(t.s.c_str(), nullptr, 10));
            t.d = static_cast<double>(t.i);
        }
        return t;
    }

    // Try to consume a Clipper `.KEY.` or `.T.` / `.F.` token. Returns
    // an empty optional when the dotted run does not match a known
    // keyword, leaving pos_ untouched so the caller can fall back to
    // the generic error path.
    std::optional<Token> try_dot_keyword() {
        if (pos_ >= s_.size() || s_[pos_] != '.') return std::nullopt;
        std::size_t save = pos_;
        ++pos_;
        std::string body;
        while (pos_ < s_.size() && s_[pos_] != '.') {
            body.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(s_[pos_]))));
            ++pos_;
        }
        if (pos_ >= s_.size() || s_[pos_] != '.') {
            pos_ = save;
            return std::nullopt;
        }
        ++pos_;
        Token t;
        if      (body == "AND") t.k = Token::K::And;
        else if (body == "OR")  t.k = Token::K::Or;
        else if (body == "NOT") t.k = Token::K::Not;
        else if (body == "T" || body == "Y") {
            t.k = Token::K::Bool; t.boolean = true;
        }
        else if (body == "F" || body == "N") {
            t.k = Token::K::Bool; t.boolean = false;
        }
        else { pos_ = save; return std::nullopt; }
        return t;
    }

    util::Result<Token> read_ident() {
        std::size_t start = pos_;
        while (pos_ < s_.size() &&
               (std::isalnum(static_cast<unsigned char>(s_[pos_])) ||
                s_[pos_] == '_')) ++pos_;
        std::string raw = s_.substr(start, pos_ - start);
        std::string up;
        up.reserve(raw.size());
        for (char ch : raw) {
            up.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(ch))));
        }
        Token t;
        t.s = raw;
        if      (up == "AND")     { t.k = Token::K::And; }
        else if (up == "OR")      { t.k = Token::K::Or;  }
        else if (up == "NOT")     { t.k = Token::K::Not; }
        else if (up == "BETWEEN") { t.k = Token::K::Between; }
        else if (up == "IN")      { t.k = Token::K::In;  }
        else if (up == "LIKE")    { t.k = Token::K::Like; }
        else if (up == "IS")      { t.k = Token::K::Is;   }
        else if (up == "TRUE" || up == "T") {
            t.k = Token::K::Bool; t.boolean = true;
        }
        else if (up == "FALSE" || up == "F") {
            t.k = Token::K::Bool; t.boolean = false;
        }
        else                      { t.k = Token::K::Ident; }
        return t;
    }
};

class Parser {
public:
    explicit Parser(const std::string& src) : lex_(src) {
        if (auto t = lex_.next(); t) cur_ = std::move(t).value();
        else err_ = t.error();
    }

    util::Result<NodePtr> parse_top() {
        if (err_.code != 0) return err_;
        auto n = parse_or();
        if (!n) return n;
        if (cur_.k != Token::K::End) {
            return util::Error{
                static_cast<std::int32_t>(openads::AE_PARSE_ERROR),
                0, "AOF parse error: unexpected trailing token", {}};
        }
        return n;
    }

private:
    Lexer       lex_;
    Token       cur_;
    util::Error err_{};

    void advance() {
        if (auto t = lex_.next(); t) cur_ = std::move(t).value();
        else { err_ = t.error(); cur_ = Token{}; cur_.k = Token::K::End; }
    }

    bool match(Token::K k) {
        if (cur_.k != k) return false;
        advance();
        return true;
    }

    static util::Error parse_err(const std::string& m) {
        return util::Error{
            static_cast<std::int32_t>(openads::AE_PARSE_ERROR), 0, m, {}};
    }

    util::Result<NodePtr> parse_or() {
        auto lhs = parse_and();
        if (!lhs) return lhs;
        std::vector<NodePtr> kids;
        kids.emplace_back(std::move(lhs).value());
        while (cur_.k == Token::K::Or) {
            advance();
            auto rhs = parse_and();
            if (!rhs) return rhs;
            kids.emplace_back(std::move(rhs).value());
        }
        if (kids.size() == 1) return std::move(kids.front());
        return make_or(std::move(kids));
    }

    util::Result<NodePtr> parse_and() {
        auto lhs = parse_not();
        if (!lhs) return lhs;
        std::vector<NodePtr> kids;
        kids.emplace_back(std::move(lhs).value());
        while (cur_.k == Token::K::And) {
            advance();
            auto rhs = parse_not();
            if (!rhs) return rhs;
            kids.emplace_back(std::move(rhs).value());
        }
        if (kids.size() == 1) return std::move(kids.front());
        return make_and(std::move(kids));
    }

    util::Result<NodePtr> parse_not() {
        if (cur_.k == Token::K::Not) {
            advance();
            auto child = parse_not();
            if (!child) return child;
            return make_not(std::move(child).value());
        }
        return parse_primary();
    }

    util::Result<NodePtr> parse_primary() {
        if (cur_.k == Token::K::LParen) {
            advance();
            auto e = parse_or();
            if (!e) return e;
            if (!match(Token::K::RParen)) return parse_err("expected ')'");
            return e;
        }
        if (cur_.k == Token::K::Ident) return parse_leaf();
        return parse_err("expected identifier or '('");
    }

    util::Result<Value> parse_literal() {
        if (cur_.k == Token::K::String) {
            Value v = cur_.s;
            advance();
            return v;
        }
        if (cur_.k == Token::K::Number) {
            Value v = cur_.is_real ? Value{cur_.d}
                                   : Value{cur_.i};
            advance();
            return v;
        }
        if (cur_.k == Token::K::Bool) {
            Value v = static_cast<std::int64_t>(cur_.boolean ? 1 : 0);
            advance();
            return v;
        }
        return parse_err("expected literal");
    }

    util::Result<NodePtr> parse_leaf() {
        Leaf leaf;
        leaf.field = cur_.s;
        advance();

        if (cur_.k == Token::K::Between) {
            advance();
            auto a = parse_literal();
            if (!a) return a.error();
            if (cur_.k != Token::K::And) return parse_err("expected AND in BETWEEN");
            advance();
            auto b = parse_literal();
            if (!b) return b.error();
            leaf.op = Op::Between;
            leaf.values.emplace_back(std::move(a).value());
            leaf.values.emplace_back(std::move(b).value());
            return make_leaf(std::move(leaf));
        }

        if (cur_.k == Token::K::In) {
            advance();
            if (!match(Token::K::LParen)) return parse_err("expected '(' after IN");
            std::vector<Value> vs;
            for (;;) {
                auto v = parse_literal();
                if (!v) return v.error();
                vs.emplace_back(std::move(v).value());
                if (cur_.k == Token::K::Comma) { advance(); continue; }
                break;
            }
            if (!match(Token::K::RParen)) return parse_err("expected ')' after IN list");
            leaf.op = Op::In;
            leaf.values = std::move(vs);
            return make_leaf(std::move(leaf));
        }

        // V2: LIKE 'pattern'
        if (cur_.k == Token::K::Like) {
            advance();
            auto pat = parse_literal();
            if (!pat) return pat.error();
            leaf.op = Op::Like;
            leaf.values.emplace_back(std::move(pat).value());
            return make_leaf(std::move(leaf));
        }

        // V2: IS NULL / IS NOT NULL
        if (cur_.k == Token::K::Is) {
            advance();
            if (cur_.k == Token::K::Not) {
                advance();
                // Expect NULL — but NULL is just an identifier we recognize
                if (cur_.k != Token::K::Ident)
                    return parse_err("expected NULL after IS NOT");
                std::string null_kw = cur_.s;
                for (auto& ch : null_kw) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                if (null_kw != "NULL")
                    return parse_err("expected NULL after IS NOT");
                advance();
                leaf.op = Op::IsNotNull;
                return make_leaf(std::move(leaf));
            } else {
                // Expect NULL
                if (cur_.k != Token::K::Ident)
                    return parse_err("expected NULL after IS");
                std::string null_kw = cur_.s;
                for (auto& ch : null_kw) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                if (null_kw != "NULL")
                    return parse_err("expected NULL after IS");
                advance();
                leaf.op = Op::IsNull;
                return make_leaf(std::move(leaf));
            }
        }

        Op op;
        switch (cur_.k) {
            case Token::K::Eq: op = Op::Eq; break;
            case Token::K::Ne: op = Op::Ne; break;
            case Token::K::Lt: op = Op::Lt; break;
            case Token::K::Le: op = Op::Le; break;
            case Token::K::Gt: op = Op::Gt; break;
            case Token::K::Ge: op = Op::Ge; break;
            default:
                return parse_err("expected comparison operator after field");
        }
        advance();
        auto rhs = parse_literal();
        if (!rhs) return rhs.error();
        leaf.op = op;
        leaf.values.emplace_back(std::move(rhs).value());
        return make_leaf(std::move(leaf));
    }
};

const char* op_str(Op o) {
    switch (o) {
        case Op::Eq: return "=";
        case Op::Ne: return "!=";
        case Op::Lt: return "<";
        case Op::Le: return "<=";
        case Op::Gt: return ">";
        case Op::Ge: return ">=";
        case Op::Between: return "BETWEEN";
        case Op::In: return "IN";
        case Op::Like: return "LIKE";
        case Op::IsNull: return "IS NULL";
        case Op::IsNotNull: return "IS NOT NULL";
    }
    return "?";
}

void value_str(const Value& v, std::ostringstream& os) {
    if (auto p = std::get_if<std::int64_t>(&v)) os << *p;
    else if (auto pd = std::get_if<double>(&v)) os << *pd;
    else if (auto ps = std::get_if<std::string>(&v)) os << '\'' << *ps << '\'';
}

void to_string_rec(const Node& n, std::ostringstream& os) {
    if (auto leaf = std::get_if<Leaf>(&n.v)) {
        os << leaf->field << ' ' << op_str(leaf->op);
        if (leaf->op == Op::Between) {
            os << ' '; value_str(leaf->values[0], os);
            os << " AND ";
            value_str(leaf->values[1], os);
        } else if (leaf->op == Op::In) {
            os << " (";
            for (std::size_t i = 0; i < leaf->values.size(); ++i) {
                if (i) os << ", ";
                value_str(leaf->values[i], os);
            }
            os << ')';
        } else if (leaf->op == Op::IsNull || leaf->op == Op::IsNotNull) {
            // No value to print
        } else if (leaf->op == Op::Like) {
            os << ' ';
            value_str(leaf->values[0], os);
        } else {
            os << ' ';
            value_str(leaf->values[0], os);
        }
    } else if (auto a = std::get_if<And>(&n.v)) {
        os << '(';
        for (std::size_t i = 0; i < a->kids.size(); ++i) {
            if (i) os << " AND ";
            to_string_rec(*a->kids[i], os);
        }
        os << ')';
    } else if (auto o = std::get_if<Or>(&n.v)) {
        os << '(';
        for (std::size_t i = 0; i < o->kids.size(); ++i) {
            if (i) os << " OR ";
            to_string_rec(*o->kids[i], os);
        }
        os << ')';
    } else if (auto nt = std::get_if<Not>(&n.v)) {
        os << "NOT ";
        to_string_rec(*nt->child, os);
    }
}

} // namespace

util::Result<NodePtr> parse(const std::string& src) {
    Parser p(src);
    return p.parse_top();
}

std::optional<bool> constant_literal(const std::string& src) {
    std::size_t b = 0, e = src.size();
    auto is_ws = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    for (;;) {
        while (b < e && is_ws(src[b])) ++b;
        while (e > b && is_ws(src[e - 1])) --e;
        if (e - b >= 2 && src[b] == '(' && src[e - 1] == ')') { ++b; --e; continue; }
        break;
    }
    if (e - b != 3 || src[b] != '.' || src[b + 2] != '.') return std::nullopt;
    switch (src[b + 1]) {
        case 'T': case 't': case 'Y': case 'y': return true;
        case 'F': case 'f': case 'N': case 'n': return false;
        default: return std::nullopt;
    }
}

std::string to_string(const Node& n) {
    std::ostringstream os;
    to_string_rec(n, os);
    return os.str();
}

} // namespace openads::engine::aof
