// OpenADS script engine — recursive-descent parser.
//
// RCB 07/17/2026: grammar per docs/script-engine.md §3 + §10 (all forms
// oracle-verified): IF/ELSEIF/ELSE/ENDIF|END IF, WHILE…DO…END WHILE with
// LEAVE/CONTINUE, TRY…CATCH ALL…END TRY, RAISE name(code,msg), RETURN,
// assignments, and raw-captured embedded SQL for everything else.
#include "engine/script/parser.h"
#include "engine/script/lexer.h"

#include <cstdlib>

namespace openads::script {

using util::Error;
using util::Result;

namespace {

Error perr(const std::string& what, std::size_t pos) {
    return Error{kScriptError, 0, "script parse error: " + what,
                 "offset " + std::to_string(pos)};
}

class Parser {
public:
    Parser(const std::string& src, std::vector<Token> toks)
        : src_(src), t_(std::move(toks)) {}

    Result<std::shared_ptr<const Program>> run() {
        auto prog = std::make_shared<Program>();
        auto blk = parse_block(/*stops=*/{});
        if (!blk) return blk.error();
        prog->stmts = std::move(blk).value();
        if (!at(Tok::End))
            return perr("unexpected '" + cur().text + "'", cur().pos);
        return std::shared_ptr<const Program>(std::move(prog));
    }

private:
    const std::string& src_;
    std::vector<Token> t_;
    std::size_t i_ = 0;
    bool seen_exec_ = false;   // a non-DECLARE statement has been parsed

    const Token& cur() const { return t_[i_]; }
    const Token& peek(std::size_t k = 1) const {
        std::size_t j = i_ + k;
        return j < t_.size() ? t_[j] : t_.back();
    }
    bool at(Tok k) const { return cur().kind == k; }
    bool at_kw(const char* kw) const {
        return cur().kind == Tok::Ident && cur().upper == kw;
    }
    void advance() { if (i_ + 1 < t_.size()) ++i_; }
    bool eat(Tok k) { if (at(k)) { advance(); return true; } return false; }
    bool eat_kw(const char* kw) {
        if (at_kw(kw)) { advance(); return true; } return false;
    }
    // Statement terminator: ';' — optional on the script's last statement
    // (SAP accepts a final statement without one).
    bool eat_stmt_semi() { return eat(Tok::Semi) || at(Tok::End); }

    // Is the current token one of the block-stop keywords?
    static bool in_stops(const std::vector<const char*>& stops,
                         const Token& t) {
        if (t.kind != Tok::Ident) return false;
        for (const char* s : stops)
            if (t.upper == s) return true;
        return false;
    }

    // ---- Statements ------------------------------------------------------

    Result<Block> parse_block(const std::vector<const char*>& stops) {
        Block out;
        while (!at(Tok::End) && !in_stops(stops, cur())) {
            auto s = parse_stmt();
            if (!s) return s.error();
            out.push_back(std::move(s).value());
        }
        return out;
    }

    Result<StmtPtr> parse_stmt() {
        // Stray semicolons are harmless.
        while (eat(Tok::Semi)) {}
        if (at(Tok::End)) return perr("statement expected", cur().pos);

        if (cur().kind == Tok::Ident) {
            const std::string& kw = cur().upper;
            if (kw == "DECLARE")  return parse_declare();
            // DECLAREs must precede every executable statement (C28: SAP
            // 2217 "Variable declaration is not allowed in the script
            // body"). Everything below marks the script body as started.
            seen_exec_ = true;
            if (kw == "IF")       return parse_if();
            if (kw == "WHILE")    return parse_while();
            if (kw == "TRY")      return parse_try();
            if (kw == "RAISE")    return parse_raise();
            if (kw == "RETURN")   return parse_return();
            // EXECUTE IMMEDIATE <string-expr>;  (EXECUTE PROCEDURE stays a
            // raw embedded statement — the SQL dispatcher owns it.)
            if (kw == "EXECUTE" && peek().kind == Tok::Ident &&
                peek().upper == "IMMEDIATE") {
                auto s = std::make_unique<Stmt>();
                s->kind = StmtKind::ExecImmediate;
                s->pos = cur().pos;
                advance(); advance();
                auto e = parse_expr();
                if (!e) return e.error();
                s->expr = std::move(e).value();
                if (!eat_stmt_semi())
                    return perr("';' expected after EXECUTE IMMEDIATE",
                                cur().pos);
                return StmtPtr(std::move(s));
            }
            if (kw == "LEAVE" || kw == "CONTINUE") {
                auto s = std::make_unique<Stmt>();
                s->kind = (kw == "LEAVE") ? StmtKind::Leave
                                          : StmtKind::Continue;
                s->pos = cur().pos;
                advance();
                if (!eat_stmt_semi())
                    return perr("';' expected after " + kw, cur().pos);
                return StmtPtr(std::move(s));
            }
            // SET <ident> = <expr> ;  — SAP accepts SET-prefixed
            // assignments (pmsys sp_SaveIntoAuditLog uses them).
            if (kw == "SET" && peek().kind == Tok::Ident &&
                peek(2).kind == Tok::Eq) {
                advance();
                return parse_assign();
            }
            // Cursor statements (§11 grammar): OPEN <c> [AS <stmt>];
            // FETCH <c>; CLOSE <c>;  A non-matching shape falls through to
            // raw SQL (e.g. OPEN inside some future dialect statement).
            if (kw == "OPEN" && peek().kind == Tok::Ident) {
                auto s = std::make_unique<Stmt>();
                s->kind = StmtKind::OpenCursor;
                s->pos = cur().pos;
                advance();
                s->name  = cur().text;
                s->upper = cur().upper;
                advance();
                if (eat_kw("AS")) {
                    std::size_t start = cur().pos;
                    while (!at(Tok::End) && !at(Tok::Semi)) advance();
                    s->raw = src_.substr(start, cur().pos - start);
                    if (s->raw.empty())
                        return perr("statement expected after OPEN " +
                                    s->name + " AS", cur().pos);
                }
                if (!eat_stmt_semi())
                    return perr("';' expected after OPEN " + s->name,
                                cur().pos);
                return StmtPtr(std::move(s));
            }
            if ((kw == "FETCH" || kw == "CLOSE") &&
                peek().kind == Tok::Ident &&
                (peek(2).kind == Tok::Semi || peek(2).kind == Tok::End)) {
                auto s = std::make_unique<Stmt>();
                s->kind = (kw == "FETCH") ? StmtKind::FetchCursor
                                          : StmtKind::CloseCursor;
                s->pos = cur().pos;
                advance();
                s->name  = cur().text;
                s->upper = cur().upper;
                advance();
                eat(Tok::Semi);   // optional on the script's last statement
                return StmtPtr(std::move(s));
            }
            // Assignment: <ident> = <expr> ;
            if (peek().kind == Tok::Eq) return parse_assign();
        }
        return parse_raw_sql();
    }

    Result<StmtPtr> parse_declare() {
        // C28/C21: SAP rejects a DECLARE after the first executable
        // statement ("Variable declaration is not allowed in the script
        // body", NativeError 2217).
        if (seen_exec_)
            return perr("variable declaration is not allowed in the "
                        "script body", cur().pos);
        auto s = std::make_unique<Stmt>();
        s->kind = StmtKind::Declare;
        s->pos = cur().pos;
        advance();  // DECLARE
        if (cur().kind != Tok::Ident)
            return perr("variable name expected after DECLARE", cur().pos);
        s->name  = cur().text;
        s->upper = cur().upper;
        advance();

        if (at_kw("CURSOR")) {
            // DECLARE <name> CURSOR [AS <raw sql>] ;  — the AS-less form
            // binds its statement later at OPEN <name> AS <sql> (C12).
            advance();
            s->is_cursor = true;
            if (eat_kw("AS")) {
                std::size_t start = cur().pos;
                while (!at(Tok::End) && !at(Tok::Semi)) advance();
                std::size_t end = cur().pos;
                s->raw = src_.substr(start, end - start);
            }
            eat(Tok::Semi);
            s->decl_type = Type::Null;
            return StmtPtr(std::move(s));
        }

        if (cur().kind != Tok::Ident)
            return perr("type expected in DECLARE", cur().pos);
        const std::string& ty = cur().upper;
        if (ty == "CHAR" || ty == "CHARACTER" || ty == "VARCHAR" ||
            ty == "CICHAR" || ty == "VARCHARFOX") {
            s->decl_type = Type::Char;
            advance();
            if (eat(Tok::LParen)) {
                if (cur().kind != Tok::Number)
                    return perr("length expected", cur().pos);
                s->char_limit = static_cast<std::size_t>(
                    std::strtoul(cur().text.c_str(), nullptr, 10));
                advance();
                if (!eat(Tok::RParen))
                    return perr("')' expected", cur().pos);
            }
        } else if (ty == "STRING" || ty == "MEMO") {
            s->decl_type = Type::Char;   // unlimited
            advance();
        } else if (ty == "INTEGER" || ty == "INT" || ty == "SHORT" ||
                   ty == "SHORTINT" || ty == "AUTOINC") {
            s->decl_type = Type::Integer;
            advance();
        } else if (ty == "NUMERIC" || ty == "DOUBLE" || ty == "FLOAT" ||
                   ty == "REAL" || ty == "MONEY" || ty == "CURDOUBLE") {
            s->decl_type = Type::Double;
            advance();
            if (eat(Tok::LParen)) {      // NUMERIC(p[,s]) — precision ignored
                while (!at(Tok::RParen) && !at(Tok::End)) advance();
                if (!eat(Tok::RParen))
                    return perr("')' expected", cur().pos);
            }
        } else if (ty == "LOGICAL" || ty == "BOOLEAN") {
            s->decl_type = Type::Logical;
            advance();
        } else if (ty == "DATE") {
            s->decl_type = Type::Date;
            advance();
        } else if (ty == "TIMESTAMP" || ty == "DATETIME" || ty == "TIME") {
            s->decl_type = Type::Timestamp;
            advance();
        } else {
            return perr("unknown type '" + cur().text + "' in DECLARE",
                        cur().pos);
        }
        if (!eat_stmt_semi())
            return perr("';' expected after DECLARE", cur().pos);
        return StmtPtr(std::move(s));
    }

    Result<StmtPtr> parse_assign() {
        auto s = std::make_unique<Stmt>();
        s->kind = StmtKind::Assign;
        s->pos = cur().pos;
        s->name  = cur().text;
        s->upper = cur().upper;
        advance();          // name
        advance();          // '='
        auto e = parse_expr();
        if (!e) return e.error();
        s->expr = std::move(e).value();
        if (!eat_stmt_semi())
            return perr("';' expected after assignment", cur().pos);
        return StmtPtr(std::move(s));
    }

    Result<StmtPtr> parse_return() {
        auto s = std::make_unique<Stmt>();
        s->kind = StmtKind::Return;
        s->pos = cur().pos;
        advance();  // RETURN
        if (!at(Tok::Semi) && !at(Tok::End)) {
            auto e = parse_expr();
            if (!e) return e.error();
            s->expr = std::move(e).value();
        }
        eat(Tok::Semi);
        return StmtPtr(std::move(s));
    }

    // "ENDIF" | "END IF" | bare "END" — all accepted by SAP (the bare form
    // appears in real DDs: `IF … THEN … END ;`). A bare END followed by
    // WHILE/TRY belongs to the enclosing block and is NOT consumed.
    bool eat_endif() {
        if (eat_kw("ENDIF")) { eat(Tok::Semi); return true; }
        if (at_kw("END")) {
            if (peek().kind == Tok::Ident && peek().upper == "IF") {
                advance(); advance(); eat(Tok::Semi);
                return true;
            }
            if (peek().kind == Tok::Ident &&
                (peek().upper == "WHILE" || peek().upper == "TRY"))
                return false;
            advance(); eat(Tok::Semi);
            return true;
        }
        return false;
    }

    Result<StmtPtr> parse_if() {
        auto s = std::make_unique<Stmt>();
        s->kind = StmtKind::If;
        s->pos = cur().pos;
        advance();  // IF
        for (;;) {
            auto cond = parse_expr();
            if (!cond) return cond.error();
            if (!eat_kw("THEN"))
                return perr("THEN expected in IF", cur().pos);
            auto blk = parse_block({"ELSEIF", "ELSE", "ENDIF", "END"});
            if (!blk) return blk.error();
            s->branches.emplace_back(std::move(cond).value(),
                                     std::move(blk).value());
            if (eat_kw("ELSEIF")) continue;
            break;
        }
        if (eat_kw("ELSE")) {
            // "ELSE IF …" nests: parse the IF as the sole else statement.
            if (at_kw("IF")) {
                auto nested = parse_if();
                if (!nested) return nested.error();
                s->else_block.push_back(std::move(nested).value());
                if (!eat_endif())
                    return perr("ENDIF expected", cur().pos);
                return StmtPtr(std::move(s));
            }
            auto blk = parse_block({"ENDIF", "END"});
            if (!blk) return blk.error();
            s->else_block = std::move(blk).value();
        }
        if (!eat_endif())
            return perr("ENDIF expected", cur().pos);
        return StmtPtr(std::move(s));
    }

    Result<StmtPtr> parse_while() {
        auto s = std::make_unique<Stmt>();
        s->kind = StmtKind::While;
        s->pos = cur().pos;
        advance();  // WHILE
        // WHILE FETCH <cursor> DO … — the idiomatic ADS cursor loop; the
        // condition is just a Fetch expression (FETCH is boolean-valued,
        // C24/C25), so the generic while machinery drives it.
        auto cond = parse_expr();
        if (!cond) return cond.error();
        s->expr = std::move(cond).value();
        if (!eat_kw("DO"))
            return perr("DO expected in WHILE", cur().pos);
        auto blk = parse_block({"END"});
        if (!blk) return blk.error();
        s->body = std::move(blk).value();
        if (!eat_kw("END"))
            return perr("END WHILE expected", cur().pos);
        eat_kw("WHILE");   // both "END WHILE;" and bare "END;" close (C3)
        eat(Tok::Semi);
        return StmtPtr(std::move(s));
    }

    Result<StmtPtr> parse_try() {
        // TRY s CATCH [ALL|<name>] s … [FINALLY s] END TRY — §11 F-probes:
        // at least one CATCH or FINALLY is required (F0); CATCH <name>
        // catches only a matching RAISE, case-insensitively (F4/F6).
        auto s = std::make_unique<Stmt>();
        s->kind = StmtKind::Try;
        s->pos = cur().pos;
        advance();  // TRY
        auto body = parse_block({"CATCH", "FINALLY", "END"});
        if (!body) return body.error();
        s->body = std::move(body).value();
        while (eat_kw("CATCH")) {
            CatchClause cc;
            if (eat_kw("ALL")) {
                // name_upper stays empty: catches everything
            } else if (cur().kind == Tok::Ident) {
                cc.name_upper = cur().upper;
                advance();
            } else {
                return perr("ALL or an error name expected after CATCH",
                            cur().pos);
            }
            auto cb = parse_block({"CATCH", "FINALLY", "END"});
            if (!cb) return cb.error();
            cc.block = std::move(cb).value();
            s->catches.push_back(std::move(cc));
        }
        if (eat_kw("FINALLY")) {
            s->has_finally = true;
            auto fb = parse_block({"END"});
            if (!fb) return fb.error();
            s->finally_block = std::move(fb).value();
        }
        if (s->catches.empty() && !s->has_finally)
            return perr("TRY statement must have at least one CATCH or "
                        "FINALLY block", cur().pos);
        if (!eat_kw("END") || !eat_kw("TRY"))
            return perr("END TRY expected", cur().pos);
        eat(Tok::Semi);
        return StmtPtr(std::move(s));
    }

    Result<StmtPtr> parse_raise() {
        auto s = std::make_unique<Stmt>();
        s->kind = StmtKind::Raise;
        s->pos = cur().pos;
        advance();  // RAISE
        if (cur().kind != Tok::Ident)
            return perr("error name expected after RAISE", cur().pos);
        s->name = cur().text;
        advance();
        if (!eat(Tok::LParen))
            return perr("'(' expected after RAISE name", cur().pos);
        while (!at(Tok::RParen)) {
            auto e = parse_expr();
            if (!e) return e.error();
            s->args.push_back(std::move(e).value());
            if (!eat(Tok::Comma)) break;
        }
        if (!eat(Tok::RParen))
            return perr("')' expected in RAISE", cur().pos);
        if (!eat_stmt_semi())
            return perr("';' expected after RAISE", cur().pos);
        return StmtPtr(std::move(s));
    }

    Result<StmtPtr> parse_raw_sql() {
        auto s = std::make_unique<Stmt>();
        s->kind = StmtKind::Sql;
        s->pos = cur().pos;
        std::size_t start = cur().pos;
        while (!at(Tok::End) && !at(Tok::Semi)) advance();
        std::size_t end = cur().pos;
        s->raw = src_.substr(start, end - start);
        eat(Tok::Semi);
        return StmtPtr(std::move(s));
    }

    // ---- Expressions -----------------------------------------------------

    Result<ExprPtr> parse_expr() { return parse_or(); }

    Result<ExprPtr> parse_or() {
        auto a = parse_and();
        if (!a) return a;
        while (at_kw("OR")) {
            advance();
            auto b = parse_and();
            if (!b) return b;
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Binary; e->bin = BinOp::Or;
            e->a = std::move(a).value(); e->b = std::move(b).value();
            a = ExprPtr(std::move(e));
        }
        return a;
    }

    Result<ExprPtr> parse_and() {
        auto a = parse_not();
        if (!a) return a;
        while (at_kw("AND")) {
            advance();
            auto b = parse_not();
            if (!b) return b;
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Binary; e->bin = BinOp::And;
            e->a = std::move(a).value(); e->b = std::move(b).value();
            a = ExprPtr(std::move(e));
        }
        return a;
    }

    Result<ExprPtr> parse_not() {
        if (at_kw("NOT")) {
            std::size_t p = cur().pos;
            advance();
            auto a = parse_not();
            if (!a) return a;
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Unary; e->un = UnOp::Not;
            e->a = std::move(a).value(); e->pos = p;
            return ExprPtr(std::move(e));
        }
        return parse_cmp();
    }

    Result<ExprPtr> parse_cmp() {
        auto a = parse_add();
        if (!a) return a;

        // IS [NOT] NULL
        if (at_kw("IS")) {
            advance();
            bool neg = eat_kw("NOT");
            if (!eat_kw("NULL"))
                return perr("NULL expected after IS", cur().pos);
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Unary;
            e->un = neg ? UnOp::IsNotNull : UnOp::IsNull;
            e->a = std::move(a).value();
            return ExprPtr(std::move(e));
        }

        bool neg = false;
        if (at_kw("NOT") &&
            (peek().kind == Tok::Ident &&
             (peek().upper == "LIKE" || peek().upper == "IN" ||
              peek().upper == "BETWEEN"))) {
            neg = true;
            advance();
        }

        if (at_kw("LIKE")) {
            advance();
            auto b = parse_add();
            if (!b) return b;
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Binary; e->bin = BinOp::Like;
            e->negated = neg;
            e->a = std::move(a).value(); e->b = std::move(b).value();
            return ExprPtr(std::move(e));
        }
        if (at_kw("IN")) {
            advance();
            if (!eat(Tok::LParen))
                return perr("'(' expected after IN", cur().pos);
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::InList;
            e->negated = neg;
            e->a = std::move(a).value();
            while (!at(Tok::RParen)) {
                auto v = parse_expr();
                if (!v) return v.error();
                e->args.push_back(std::move(v).value());
                if (!eat(Tok::Comma)) break;
            }
            if (!eat(Tok::RParen))
                return perr("')' expected after IN list", cur().pos);
            return ExprPtr(std::move(e));
        }
        if (at_kw("BETWEEN")) {
            advance();
            auto lo = parse_add();
            if (!lo) return lo;
            if (!eat_kw("AND"))
                return perr("AND expected in BETWEEN", cur().pos);
            auto hi = parse_add();
            if (!hi) return hi;
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Between;
            e->negated = neg;
            e->a = std::move(a).value();
            e->b = std::move(lo).value();
            e->c = std::move(hi).value();
            return ExprPtr(std::move(e));
        }
        if (neg) return perr("LIKE/IN/BETWEEN expected after NOT", cur().pos);

        BinOp op;
        switch (cur().kind) {
            case Tok::Eq: op = BinOp::Eq; break;
            case Tok::Ne: op = BinOp::Ne; break;
            case Tok::Lt: op = BinOp::Lt; break;
            case Tok::Le: op = BinOp::Le; break;
            case Tok::Gt: op = BinOp::Gt; break;
            case Tok::Ge: op = BinOp::Ge; break;
            default: return a;
        }
        advance();
        auto b = parse_add();
        if (!b) return b;
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::Binary; e->bin = op;
        e->a = std::move(a).value(); e->b = std::move(b).value();
        return ExprPtr(std::move(e));
    }

    Result<ExprPtr> parse_add() {
        auto a = parse_mul();
        if (!a) return a;
        while (at(Tok::Plus) || at(Tok::Minus)) {
            BinOp op = at(Tok::Plus) ? BinOp::Add : BinOp::Sub;
            advance();
            auto b = parse_mul();
            if (!b) return b;
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Binary; e->bin = op;
            e->a = std::move(a).value(); e->b = std::move(b).value();
            a = ExprPtr(std::move(e));
        }
        return a;
    }

    Result<ExprPtr> parse_mul() {
        auto a = parse_unary();
        if (!a) return a;
        while (at(Tok::Star) || at(Tok::Slash) || at(Tok::Percent)) {
            BinOp op = at(Tok::Star) ? BinOp::Mul
                     : at(Tok::Slash) ? BinOp::Div : BinOp::Mod;
            advance();
            auto b = parse_unary();
            if (!b) return b;
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Binary; e->bin = op;
            e->a = std::move(a).value(); e->b = std::move(b).value();
            a = ExprPtr(std::move(e));
        }
        return a;
    }

    Result<ExprPtr> parse_unary() {
        if (at(Tok::Minus)) {
            std::size_t p = cur().pos;
            advance();
            auto a = parse_unary();
            if (!a) return a;
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Unary; e->un = UnOp::Neg;
            e->a = std::move(a).value(); e->pos = p;
            return ExprPtr(std::move(e));
        }
        if (at(Tok::Plus)) { advance(); return parse_unary(); }
        return parse_primary();
    }

    static Result<Value> date_from_iso(const std::string& s,
                                       std::size_t pos) {
        if (s.size() < 10 || s[4] != '-' || s[7] != '-')
            return perr("bad date literal '" + s + "'", pos);
        int y = std::atoi(s.substr(0, 4).c_str());
        int m = std::atoi(s.substr(5, 2).c_str());
        int d = std::atoi(s.substr(8, 2).c_str());
        return Value::date(jdn_from_ymd(y, m, d));
    }

    Result<ExprPtr> parse_primary() {
        auto lit = [&](Value v) {
            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Literal; e->lit = std::move(v);
            e->pos = cur().pos;
            return ExprPtr(std::move(e));
        };

        if (at(Tok::Number)) {
            const std::string& tx = cur().text;
            ExprPtr e;
            if (tx.find('.') == std::string::npos)
                e = lit(Value::integer(std::strtoll(tx.c_str(), nullptr, 10)));
            else
                e = lit(Value::real(std::strtod(tx.c_str(), nullptr)));
            advance();
            return e;
        }
        if (at(Tok::String)) {
            auto e = lit(Value::character(cur().text));
            advance();
            return e;
        }
        if (at(Tok::DateLit)) {
            auto v = date_from_iso(cur().text, cur().pos);
            if (!v) return v.error();
            auto e = lit(std::move(v).value());
            advance();
            return e;
        }
        if (at(Tok::TsLit)) {
            const std::string& s = cur().text;
            auto dv = date_from_iso(s, cur().pos);
            if (!dv) return dv.error();
            std::int64_t ms = 0;
            if (s.size() >= 19 && (s[10] == ' ' || s[10] == 'T')) {
                int hh = std::atoi(s.substr(11, 2).c_str());
                int mi = std::atoi(s.substr(14, 2).c_str());
                int ss = std::atoi(s.substr(17, 2).c_str());
                ms = (static_cast<std::int64_t>(hh) * 3600 + mi * 60 + ss) *
                     1000;
            }
            auto e = lit(Value::timestamp(dv.value().i * 86400000 + ms));
            advance();
            return e;
        }

        if (at(Tok::LParen)) {
            // (SELECT …) subquery — raw capture to the matching ')'.
            if (peek().kind == Tok::Ident && peek().upper == "SELECT") {
                std::size_t p0 = cur().pos;
                advance();                       // '('
                std::size_t start = cur().pos;
                int depth = 1;
                std::size_t end = start;
                while (!at(Tok::End)) {
                    if (at(Tok::LParen)) ++depth;
                    else if (at(Tok::RParen)) {
                        --depth;
                        if (depth == 0) { end = cur().pos; advance(); break; }
                    }
                    advance();
                }
                if (depth != 0)
                    return perr("unterminated subquery", p0);
                auto e = std::make_unique<Expr>();
                e->kind = ExprKind::Subquery;
                e->raw = src_.substr(start, end - start);
                e->pos = p0;
                return ExprPtr(std::move(e));
            }
            advance();
            auto inner = parse_expr();
            if (!inner) return inner;
            if (!eat(Tok::RParen))
                return perr("')' expected", cur().pos);
            return inner;
        }

        if (cur().kind == Tok::Ident) {
            const std::string up = cur().upper;
            std::size_t p0 = cur().pos;
            if (up == "NULL")  { advance(); return lit(Value::null()); }
            if (up == "TRUE")  { advance(); return lit(Value::logical(true)); }
            if (up == "FALSE") { advance(); return lit(Value::logical(false)); }
            if (up == "CASE")  return parse_case();
            // FETCH <cursor> — boolean condition form (WHILE FETCH c DO /
            // IF FETCH c THEN, probes C24/C25). Advances the cursor.
            if (up == "FETCH" && peek().kind == Tok::Ident) {
                auto e = std::make_unique<Expr>();
                e->kind = ExprKind::Fetch;
                e->pos = p0;
                advance();
                e->name  = cur().text;
                e->upper = cur().upper;
                advance();
                return ExprPtr(std::move(e));
            }

            std::string name = cur().text;
            advance();

            if (at(Tok::LParen)) return parse_call(name, up, p0);

            if (at(Tok::Dot) && peek().kind == Tok::Ident) {
                // cursor.field — current-row access on an open cursor.
                // The lexer hands [bracketed names] through as one Ident
                // with the brackets kept (C16) — strip them here.
                auto e = std::make_unique<Expr>();
                e->kind = ExprKind::CursorField;
                e->name = name; e->upper = up;
                advance();                    // '.'
                e->field = cur().text;
                if (e->field.size() >= 2 && e->field.front() == '[' &&
                    e->field.back() == ']')
                    e->field = e->field.substr(1, e->field.size() - 2);
                advance();
                e->pos = p0;
                return ExprPtr(std::move(e));
            }

            auto e = std::make_unique<Expr>();
            e->kind = ExprKind::Var;
            e->name = std::move(name); e->upper = up; e->pos = p0;
            return ExprPtr(std::move(e));
        }

        return perr("expression expected at '" + cur().text + "'",
                    cur().pos);
    }

    Result<ExprPtr> parse_call(std::string name, std::string up,
                               std::size_t p0) {
        advance();  // '('
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::FnCall;
        e->name = std::move(name); e->upper = std::move(up); e->pos = p0;

        // CAST(expr AS TYPE)
        if (e->upper == "CAST") {
            auto a = parse_expr();
            if (!a) return a;
            e->args.push_back(std::move(a).value());
            if (!eat_kw("AS"))
                return perr("AS expected in CAST", cur().pos);
            if (cur().kind != Tok::Ident)
                return perr("type expected in CAST", cur().pos);
            e->type_name = cur().upper;
            advance();
            if (!eat(Tok::RParen))
                return perr("')' expected in CAST", cur().pos);
            return ExprPtr(std::move(e));
        }
        // CONVERT(expr, SQL_TYPE)
        if (e->upper == "CONVERT") {
            auto a = parse_expr();
            if (!a) return a;
            e->args.push_back(std::move(a).value());
            if (!eat(Tok::Comma))
                return perr("',' expected in CONVERT", cur().pos);
            if (cur().kind != Tok::Ident)
                return perr("type expected in CONVERT", cur().pos);
            e->type_name = cur().upper;
            advance();
            if (!eat(Tok::RParen))
                return perr("')' expected in CONVERT", cur().pos);
            return ExprPtr(std::move(e));
        }
        // TIMESTAMPADD / TIMESTAMPDIFF — the ODBC interval unit
        // (SQL_TSI_MONTH, …) is a bare keyword, not an expression; capture
        // it in type_name.
        if (e->upper == "TIMESTAMPADD" || e->upper == "TIMESTAMPDIFF") {
            if (cur().kind != Tok::Ident)
                return perr("interval unit expected in " + e->name,
                            cur().pos);
            e->type_name = cur().upper;
            advance();
            if (!eat(Tok::Comma))
                return perr("',' expected in " + e->name, cur().pos);
            while (!at(Tok::RParen)) {
                auto a = parse_expr();
                if (!a) return a;
                e->args.push_back(std::move(a).value());
                if (!eat(Tok::Comma)) break;
            }
            if (!eat(Tok::RParen))
                return perr("')' expected in " + e->name, cur().pos);
            return ExprPtr(std::move(e));
        }
        // POSITION(needle IN haystack) — the needle is parsed at additive
        // precedence so the grammar-level IN-list operator doesn't grab the
        // IN separator.
        if (e->upper == "POSITION") {
            auto a = parse_add();
            if (!a) return a;
            e->args.push_back(std::move(a).value());
            if (eat_kw("IN")) {
                auto b = parse_expr();
                if (!b) return b;
                e->args.push_back(std::move(b).value());
            }
            if (!eat(Tok::RParen))
                return perr("')' expected in POSITION", cur().pos);
            return ExprPtr(std::move(e));
        }

        while (!at(Tok::RParen)) {
            auto a = parse_expr();
            if (!a) return a;
            e->args.push_back(std::move(a).value());
            if (!eat(Tok::Comma)) break;
        }
        if (!eat(Tok::RParen))
            return perr("')' expected in call to " + e->name, cur().pos);
        return ExprPtr(std::move(e));
    }

    Result<ExprPtr> parse_case() {
        auto e = std::make_unique<Expr>();
        e->kind = ExprKind::Case;
        e->pos = cur().pos;
        advance();  // CASE
        if (!at_kw("WHEN")) {
            auto op = parse_expr();
            if (!op) return op;
            e->case_operand = std::move(op).value();
        }
        while (eat_kw("WHEN")) {
            auto w = parse_expr();
            if (!w) return w;
            if (!eat_kw("THEN"))
                return perr("THEN expected in CASE", cur().pos);
            auto r = parse_expr();
            if (!r) return r;
            e->whens.emplace_back(std::move(w).value(), std::move(r).value());
        }
        if (e->whens.empty())
            return perr("WHEN expected in CASE", cur().pos);
        if (eat_kw("ELSE")) {
            auto r = parse_expr();
            if (!r) return r;
            e->else_expr = std::move(r).value();
        }
        if (!eat_kw("END"))
            return perr("END expected in CASE", cur().pos);
        return ExprPtr(std::move(e));
    }
};

}  // namespace

Result<std::shared_ptr<const Program>> compile(const std::string& src) {
    auto toks = lex(src);
    if (!toks) return toks.error();
    Parser p(src, std::move(toks).value());
    return p.run();
}

// RCB 07/17/2026: shared type-name mapping for parameter lists (same names
// the DECLARE parser accepts).
static bool map_type_name(const std::string& up, Type& t) {
    if (up == "CHAR" || up == "CHARACTER" || up == "VARCHAR" ||
        up == "CICHAR" || up == "VARCHARFOX" || up == "STRING" ||
        up == "MEMO") { t = Type::Char; return true; }
    if (up == "INTEGER" || up == "INT" || up == "SHORT" ||
        up == "SHORTINT" || up == "AUTOINC") { t = Type::Integer; return true; }
    if (up == "NUMERIC" || up == "DOUBLE" || up == "FLOAT" || up == "REAL" ||
        up == "MONEY" || up == "CURDOUBLE") { t = Type::Double; return true; }
    if (up == "LOGICAL" || up == "BOOLEAN") { t = Type::Logical; return true; }
    if (up == "DATE") { t = Type::Date; return true; }
    if (up == "TIMESTAMP" || up == "DATETIME" || up == "TIME") {
        t = Type::Timestamp; return true;
    }
    return false;
}

Result<std::vector<Param>> parse_params(const std::string& text) {
    std::vector<Param> out;
    auto toks_r = lex(text);
    if (!toks_r) return toks_r.error();
    const auto& t = toks_r.value();
    std::size_t i = 0;
    auto skip_seps = [&]() {
        while (i < t.size() &&
               (t[i].kind == Tok::Comma || t[i].kind == Tok::Semi))
            ++i;
    };
    skip_seps();
    while (i < t.size() && t[i].kind != Tok::End) {
        if (t[i].kind != Tok::Ident)
            return perr("parameter name expected", t[i].pos);
        Param p;
        p.name = t[i].text;
        ++i;
        // Separator between name and type may be whitespace OR a comma
        // (SAP Proc_Input uses "name,TYPE,len;").
        bool comma_form = false;
        if (i < t.size() && t[i].kind == Tok::Comma) { ++i; comma_form = true; }
        if (i >= t.size() || t[i].kind != Tok::Ident)
            return perr("parameter type expected for " + p.name,
                        i < t.size() ? t[i].pos : text.size());
        if (!map_type_name(t[i].upper, p.type))
            return perr("unknown parameter type '" + t[i].text + "'",
                        t[i].pos);
        ++i;
        // Length: "(n)" in the space form, ",n" in the comma form.
        if (i < t.size() && t[i].kind == Tok::LParen) {
            ++i;
            if (i < t.size() && t[i].kind == Tok::Number) {
                p.char_limit = static_cast<std::size_t>(
                    std::strtoul(t[i].text.c_str(), nullptr, 10));
                ++i;
            }
            while (i < t.size() && t[i].kind != Tok::RParen) ++i;
            if (i < t.size()) ++i;   // ')'
        } else if (comma_form && i + 1 < t.size() &&
                   t[i].kind == Tok::Comma &&
                   t[i + 1].kind == Tok::Number) {
            p.char_limit = static_cast<std::size_t>(
                std::strtoul(t[i + 1].text.c_str(), nullptr, 10));
            i += 2;
        }
        out.push_back(std::move(p));
        skip_seps();
    }
    return out;
}

}  // namespace openads::script
