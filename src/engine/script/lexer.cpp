// OpenADS script engine — lexer implementation.
#include "engine/script/lexer.h"

#include <cctype>

namespace openads::script {

using util::Error;
using util::Result;

static Error lex_err(const std::string& what, std::size_t pos) {
    return Error{kScriptError, 0, "script lex error: " + what,
                 "offset " + std::to_string(pos)};
}

static bool ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' ||
           c == '@';
}
static bool ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

Result<std::vector<Token>> lex(const std::string& src) {
    std::vector<Token> out;
    std::size_t i = 0, n = src.size();

    auto push = [&](Tok k, std::size_t pos) {
        Token t; t.kind = k; t.pos = pos; out.push_back(std::move(t));
    };

    while (i < n) {
        char c = src[i];

        // Whitespace
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++i; continue; }

        // Comments: "--", "//" to end of line; "/* */" block.
        if (c == '-' && i + 1 < n && src[i + 1] == '-') {
            while (i < n && src[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && src[i + 1] == '/') {
            while (i < n && src[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && src[i + 1] == '*') {
            std::size_t start = i; i += 2;
            while (i + 1 < n && !(src[i] == '*' && src[i + 1] == '/')) ++i;
            if (i + 1 >= n) return lex_err("unterminated /* comment", start);
            i += 2;
            continue;
        }

        // String literal
        if (c == '\'') {
            std::size_t start = i++;
            std::string v;
            bool closed = false;
            while (i < n) {
                if (src[i] == '\'') {
                    if (i + 1 < n && src[i + 1] == '\'') { v.push_back('\''); i += 2; }
                    else { ++i; closed = true; break; }
                } else v.push_back(src[i++]);
            }
            if (!closed) return lex_err("unterminated string", start);
            Token t; t.kind = Tok::String; t.text = std::move(v); t.pos = start;
            out.push_back(std::move(t));
            continue;
        }

        // {d '...'} / {ts '...'} literals
        if (c == '{') {
            std::size_t start = i++;
            std::string kw;
            while (i < n && std::isalpha(static_cast<unsigned char>(src[i])))
                kw.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(src[i++]))));
            while (i < n && src[i] == ' ') ++i;
            if ((kw != "d" && kw != "ts" && kw != "t") || i >= n || src[i] != '\'')
                return lex_err("bad {} literal (expected {d '...'} or {ts '...'})",
                               start);
            ++i;
            std::string v;
            while (i < n && src[i] != '\'') v.push_back(src[i++]);
            if (i >= n) return lex_err("unterminated {} literal", start);
            ++i;
            while (i < n && src[i] == ' ') ++i;
            if (i >= n || src[i] != '}')
                return lex_err("missing } in literal", start);
            ++i;
            Token t; t.kind = (kw == "d") ? Tok::DateLit : Tok::TsLit;
            t.text = std::move(v); t.pos = start;
            out.push_back(std::move(t));
            continue;
        }

        // Number
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && i + 1 < n &&
             std::isdigit(static_cast<unsigned char>(src[i + 1])))) {
            std::size_t start = i;
            std::string v;
            while (i < n && (std::isdigit(static_cast<unsigned char>(src[i])) ||
                             src[i] == '.'))
                v.push_back(src[i++]);
            Token t; t.kind = Tok::Number; t.text = std::move(v); t.pos = start;
            out.push_back(std::move(t));
            continue;
        }

        // Identifier / keyword (may start with '@')
        if (ident_start(c)) {
            std::size_t start = i;
            std::string v;
            v.push_back(src[i++]);
            while (i < n && ident_char(src[i])) v.push_back(src[i++]);
            Token t; t.kind = Tok::Ident; t.pos = start;
            t.upper.reserve(v.size());
            for (char ch : v)
                t.upper.push_back(static_cast<char>(
                    std::toupper(static_cast<unsigned char>(ch))));
            t.text = std::move(v);
            out.push_back(std::move(t));
            continue;
        }

        // Operators / punctuation
        std::size_t start = i;
        switch (c) {
            case '(': push(Tok::LParen, start); ++i; break;
            case ')': push(Tok::RParen, start); ++i; break;
            case ',': push(Tok::Comma, start); ++i; break;
            case ';': push(Tok::Semi, start); ++i; break;
            case '.': push(Tok::Dot, start); ++i; break;
            case '+': push(Tok::Plus, start); ++i; break;
            case '-': push(Tok::Minus, start); ++i; break;
            case '*': push(Tok::Star, start); ++i; break;
            case '/': push(Tok::Slash, start); ++i; break;
            case '%': push(Tok::Percent, start); ++i; break;
            case '=': push(Tok::Eq, start); ++i; break;
            case '<':
                if (i + 1 < n && src[i + 1] == '>') { push(Tok::Ne, start); i += 2; }
                else if (i + 1 < n && src[i + 1] == '=') { push(Tok::Le, start); i += 2; }
                else { push(Tok::Lt, start); ++i; }
                break;
            case '>':
                if (i + 1 < n && src[i + 1] == '=') { push(Tok::Ge, start); i += 2; }
                else { push(Tok::Gt, start); ++i; }
                break;
            default:
                return lex_err(std::string("unexpected character '") + c + "'",
                               start);
        }
    }

    push(Tok::End, n);
    return out;
}

}  // namespace openads::script
