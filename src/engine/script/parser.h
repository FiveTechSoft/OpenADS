// OpenADS script engine — parser (docs/script-engine.md §4.1).
#pragma once

#include "engine/script/ast.h"
#include "util/result.h"

#include <memory>
#include <string>

namespace openads::script {

// Compile script text to a Program. Parsed once, cached by callers (the
// DataDict entry holds the shared_ptr). Errors carry kScriptError with the
// source offset in context, matching SAP's 7200 family behavior.
util::Result<std::shared_ptr<const Program>> compile(const std::string& src);

// Parse a function/procedure parameter list as stored in the DD, e.g.
// "cPropertyID CHAR( 25 ), dstart DATE, dEnd DATE" (commas or semicolons
// separate entries; SAP proc Proc_Input uses "name,TYPE,len;name,..." — both
// shapes are accepted). Used by call sites to coerce caller arguments into
// the declared parameter types before running a script.
struct Param {
    std::string name;
    Type        type = Type::Null;
    std::size_t char_limit = 0;
};
util::Result<std::vector<Param>> parse_params(const std::string& text);

}  // namespace openads::script
