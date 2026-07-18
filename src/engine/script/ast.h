// OpenADS script engine — AST (docs/script-engine.md §4).
//
// RCB 07/17/2026: the parser owns the scripting constructs ONLY. Every
// embedded SQL statement (SELECT/INSERT/UPDATE/DELETE/MERGE/EXECUTE/...)
// and every (SELECT ...) subquery is captured as RAW TEXT and delegated to
// the SQL executor through the bridge at run time — the script engine never
// re-implements SQL (design §4.2).
#pragma once

#include "engine/script/value.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace openads::script {

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

enum class BinOp : std::uint8_t {
    Add, Sub, Mul, Div, Mod,
    Eq, Ne, Lt, Le, Gt, Ge,
    And, Or, Like,
};
enum class UnOp : std::uint8_t { Neg, Not, IsNull, IsNotNull };

enum class ExprKind : std::uint8_t {
    Literal,     // lit
    Var,         // name/upper (parameter or DECLAREd variable)
    Unary,       // un, a
    Binary,      // bin, a, b
    FnCall,      // name(args…); type_name set for CONVERT/CAST
    Case,        // operand? + whens (cond-or-value, result) + else_expr?
    Subquery,    // raw "(SELECT …)" body text (without outer parens)
    InList,      // a IN (args…)  — negated for NOT IN
    Between,     // a BETWEEN b AND c
    CursorField, // name.field — S3 (parsed now so bodies compile later)
};

struct Expr {
    ExprKind kind = ExprKind::Literal;
    Value    lit;                      // Literal
    std::string name, upper;           // Var / FnCall / CursorField cursor
    std::string field;                 // CursorField field name
    std::string type_name;             // CONVERT/CAST target (upper-cased)
    std::string raw;                   // Subquery text
    BinOp    bin = BinOp::Add;
    UnOp     un  = UnOp::Neg;
    bool     negated = false;          // NOT IN / NOT BETWEEN / NOT LIKE
    ExprPtr  a, b, c;                  // operands
    std::vector<ExprPtr> args;         // FnCall / InList
    ExprPtr  case_operand;             // Case (simple form)
    std::vector<std::pair<ExprPtr, ExprPtr>> whens;
    ExprPtr  else_expr;
    std::size_t pos = 0;               // source offset (diagnostics)
};

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;
using Block   = std::vector<StmtPtr>;

enum class StmtKind : std::uint8_t {
    Declare,     // name, decl_type, char_limit; cursor_sql non-empty for
                 // DECLARE <name> CURSOR AS <raw sql>
    Assign,      // name = expr
    If,          // branches (cond, block)…, else_block
    While,       // cond, body
    Leave,
    Continue,
    Return,      // optional expr
    Try,         // body, catch_block
    Raise,       // raise_name, args (code, message)
    Sql,         // raw embedded statement text
    ExecImmediate, // EXECUTE IMMEDIATE <string-expr>
};

struct Stmt {
    StmtKind kind = StmtKind::Sql;
    std::string name, upper;           // Declare/Assign target, Raise name
    Type        decl_type = Type::Null;
    std::size_t char_limit = 0;
    std::string raw;                   // Sql / Declare-cursor text
    ExprPtr     expr;                  // Assign value / Return value / While cond
    std::vector<std::pair<ExprPtr, Block>> branches;   // If
    Block       else_block;            // If
    Block       body;                  // While / Try
    Block       catch_block;           // Try
    std::vector<ExprPtr> args;         // Raise(code, msg)
    std::size_t pos = 0;
};

struct Program {
    Block stmts;
};

}  // namespace openads::script
