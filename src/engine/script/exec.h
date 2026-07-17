// OpenADS script engine — executor + SQL bridge (docs/script-engine.md §4).
//
// RCB 07/17/2026: the executor runs compiled Programs. It owns ONLY the
// scripting semantics (scopes, control flow, expressions, builtins). All
// embedded SQL and every subquery goes through SqlBridge, implemented by
// the ABI layer on the caller's connection — one SQL executor for the whole
// product (design §4.2). A null bridge is legal (pure unit tests): embedded
// SQL then fails with a clear error.
#pragma once

#include "engine/script/ast.h"
#include "util/result.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace openads::script {

// Minimal typed row access the executor needs from an embedded statement.
struct SqlCursor {
    virtual ~SqlCursor() = default;
    virtual bool next() = 0;                 // first call → first row
    virtual std::size_t field_count() const = 0;
    virtual Value field(std::size_t idx) = 0;
};

struct SqlBridge {
    virtual ~SqlBridge() = default;
    // Run one embedded SQL statement (script variables already substituted
    // as literals by the executor). May return a null cursor for DML.
    virtual util::Result<std::unique_ptr<SqlCursor>>
        exec(const std::string& sql) = 0;
    // Script UDF invocation (function stored in the DD). The bridge owns
    // recursion-depth limits and DD lookup.
    virtual util::Result<Value>
        call_udf(const std::string& name, const std::vector<Value>& args) = 0;
    virtual bool has_udf(const std::string& name) = 0;
};

struct ExecResult {
    bool  returned = false;
    Value return_value;
};

class Executor {
public:
    explicit Executor(SqlBridge* bridge) : bridge_(bridge) {}

    // Pre-populate a parameter (function/proc argument). `name` as written;
    // lookup is case-insensitive.
    void set_param(const std::string& name, Value v);

    util::Result<ExecResult> run(const Program& p);

private:
    struct Slot {
        Value       val;
        Type        declared = Type::Null;   // Null = untyped (parameter)
        std::size_t char_limit = 0;
    };
    struct Flow {
        enum K { Normal, Return, Leave, Continue } k = Normal;
        Value ret;
    };

    SqlBridge* bridge_;
    std::unordered_map<std::string, Slot> scope_;   // key: upper-cased name
    Value err_code_;   // __errcode / __errtext inside CATCH
    Value err_text_;

    util::Result<Flow>  exec_block(const Block& b);
    util::Result<Flow>  exec_stmt(const Stmt& s);
    util::Result<Value> eval(const Expr& e);
    util::Result<Value> eval_call(const Expr& e);
    util::Result<Value> eval_subquery(const std::string& raw);
    util::Result<std::string> substitute(const std::string& raw) const;
};

// Render a value as a SQL literal for the substitution fallback (design
// §4.2): 'quoted' strings, plain numbers, {d '...'}/{ts '...'}, TRUE/FALSE,
// NULL.
std::string to_sql_literal(const Value& v);

}  // namespace openads::script
