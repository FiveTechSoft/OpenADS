#pragma once

#include "util/result.h"
#include <cctype>
#include <string>

namespace openads::engine {

// Validate resource shape only, not SQL syntax or authorization. Quoted
// literals/identifiers and comments do not contribute nesting or token load.
inline util::Result<void> validate_remote_sql_input(const std::string& sql) {
    if (sql.size() > 1024u * 1024u)
        return util::Error{7079, 0, "SQL source size limit exceeded", ""};
    std::size_t tokens = 0;
    unsigned depth = 0;
    for (std::size_t i = 0; i < sql.size();) {
        const char ch = sql[i];
        if (ch == '\0') return util::Error{7079, 0, "SQL embedded NUL rejected", ""};
        if (std::isspace(static_cast<unsigned char>(ch))) { ++i; continue; }
        if (ch == '-' && i + 1 < sql.size() && sql[i + 1] == '-') {
            i += 2;
            while (i < sql.size() && sql[i] != '\n' && sql[i] != '\0') ++i;
            continue;
        }
        if (ch == '/' && i + 1 < sql.size() && sql[i + 1] == '*') {
            i += 2;
            while (i < sql.size() && sql[i] != '\0') {
                if (sql[i] == '*' && i + 1 < sql.size() && sql[i + 1] == '/') { i += 2; break; }
                ++i;
            }
            continue;
        }
        if (++tokens > 4096) return util::Error{7079, 0, "SQL token limit exceeded", ""};
        if (ch == '\'' || ch == '"' || ch == '[') {
            const char closing = ch == '[' ? ']' : ch;
            ++i;
            while (i < sql.size()) {
                if (sql[i] == '\0') return util::Error{7079, 0, "SQL embedded NUL rejected", ""};
                if (sql[i++] == closing) {
                    if (i < sql.size() && sql[i] == closing) { ++i; continue; }
                    break;
                }
            }
            continue;
        }
        if (ch == '(' && ++depth > 128)
            return util::Error{7079, 0, "SQL nesting limit exceeded", ""};
        if (ch == ')' && depth != 0) --depth;
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '@' || ch == '#') {
            do { ++i; } while (i < sql.size() &&
                (std::isalnum(static_cast<unsigned char>(sql[i])) || sql[i] == '_' || sql[i] == '@' || sql[i] == '#'));
        } else ++i;
    }
    return {};
}

} // namespace openads::engine
