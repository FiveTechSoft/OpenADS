// OpenADS script engine — lexer (docs/script-engine.md §4).
//
// RCB 07/17/2026: tokenizes the ADS scripting dialect. Notable rules, all
// oracle-verified (§10): identifiers may start with '@'; keywords and
// identifiers are case-insensitive; strings are single-quoted with ''
// escaping; {d '...'} / {ts '...'} are date/timestamp literals; comments are
// "--", "//" and "/* */"; the only relational forms are = and <> (no ==/!=).
#pragma once

#include "engine/script/value.h"
#include "util/result.h"

#include <cstddef>
#include <string>
#include <vector>

namespace openads::script {

enum class Tok : std::uint8_t {
    End,
    Ident,       // variable / function / keyword candidate (text preserved)
    Number,      // integer or decimal literal
    String,      // 'quoted' (text = decoded content)
    DateLit,     // {d 'YYYY-MM-DD'}      (text = inner string)
    TsLit,       // {ts 'YYYY-MM-DD HH:MM:SS[.fff]'}
    LParen, RParen, Comma, Semi, Dot,
    Plus, Minus, Star, Slash, Percent,
    Eq, Ne, Lt, Le, Gt, Ge,
};

struct Token {
    Tok         kind = Tok::End;
    std::string text;        // Ident/String/Number/DateLit/TsLit content
    std::string upper;       // Ident only: upper-cased for keyword tests
    std::size_t pos = 0;     // byte offset in source (for error messages)
};

// Tokenize the whole script. On a malformed token (unterminated string,
// bad {d} literal) returns kScriptError with the offset in context.
util::Result<std::vector<Token>> lex(const std::string& src);

}  // namespace openads::script
