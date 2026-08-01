#include "sql_backend/sql_acl_store.h"

#include "engine/data_dict.h"
#include "sql/parser.h"
#include "sql_backend/sql_common.h"

#include <cctype>
#include <cstdint>
#include <functional>

namespace openads::sql_backend {

namespace {

using DD = openads::engine::DataDict;

constexpr const char* kAclTable    = "OPENADS$ACL";
constexpr const char* kMemberTable = "OPENADS$MEMBER";
constexpr const char* kUserTable   = "OPENADS$USER";

std::string quote_ident(SqlDdlDialect d, const std::string& name) {
    switch (d) {
        case SqlDdlDialect::Mssql:
            return "[" + name + "]";
        case SqlDdlDialect::Maria:
            return "`" + name + "`";
        default:
            return "\"" + name + "\"";
    }
}

std::string sql_escape(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') out += "''";
        else           out += c;
    }
    out += '\'';
    return out;
}

std::string upper_copy(std::string s) {
    for (auto& ch : s) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return s;
}

std::string lower_copy(std::string s) {
    for (auto& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

// Advantage DD user names are case-insensitive; group grantees keep parsed case.
std::string normalize_user_name(const std::string& user) {
    return lower_copy(user);
}

std::string ci_compare_sql(const std::string& col, const std::string& value) {
    return "LOWER(" + col + ") = LOWER(" + sql_escape(value) + ")";
}

std::string limit_one_suffix(SqlDdlDialect dialect) {
    if (dialect == SqlDdlDialect::Oracle) {
        return " FETCH FIRST 1 ROW ONLY";
    }
    return " LIMIT 1";
}

uint32_t bits_from_right(const std::string& right) {
    const std::string r = upper_copy(right);
    if      (r == "ALL")       return DD::DD_PERM_FULL;
    else if (r == "SELECT")    return DD::DD_PERM_SELECT;
    else if (r == "INSERT")    return DD::DD_PERM_INSERT;
    else if (r == "UPDATE")    return DD::DD_PERM_UPDATE;
    else if (r == "DELETE")    return DD::DD_PERM_DELETE;
    else if (r == "EXECUTE")   return DD::DD_PERM_EXECUTE;
    else if (r == "REFERENCE" || r == "REFERENCES")
                               return DD::DD_PERM_REFERENCE;
    return DD::DD_PERM_FULL;
}

uint32_t revoke_mask_from_right(const std::string& right) {
    const std::string r = upper_copy(right);
    if (r == "ALL") return DD::DD_PERM_FULL;
    return bits_from_right(right);
}

bool grantee_is_group(const std::string& grantee) {
    if (grantee.empty()) return false;
    if (upper_copy(grantee) == "PUBLIC") return true;
    for (char c : grantee) {
        if (c >= 'a' && c <= 'z') return false;
    }
    return true;
}

std::string perm_col_sql(const char* col_name, uint32_t bit) {
    return std::string(
        "CASE WHEN (bitmask & ") + std::to_string(bit) +
        ") != 0 THEN CASE WHEN is_group != 0 THEN '1' ELSE '2' END "
        "ELSE '0' END AS \"" + col_name + "\"";
}

util::Result<void> ensure_acl_table(SqlDdlDialect dialect, SqlExecFn exec_sql) {
    return exec_sql(acl_table_ddl(dialect));
}

util::Result<void> ensure_member_table(SqlDdlDialect dialect, SqlExecFn exec_sql) {
    return exec_sql(member_table_ddl(dialect));
}

util::Result<void> ensure_user_table(SqlDdlDialect dialect, SqlExecFn exec_sql) {
    return exec_sql(user_table_ddl(dialect));
}

// GRANT GROUP sales TO alice  /  REVOKE GROUP sales FROM alice
std::optional<std::pair<bool, std::pair<std::string, std::string>>>
try_parse_group_membership(const std::string& sql) {
    std::string s = sql;
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    auto upper_prefix = [&](std::size_t n) {
        std::string out;
        for (std::size_t i = 0; i < n && i < s.size(); ++i) {
            out.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(s[i]))));
        }
        return out;
    };
    bool is_revoke = false;
    std::size_t pos = 0;
    if (upper_prefix(12) == "REVOKE GROUP") {
        is_revoke = true;
        pos = 12;
    } else if (upper_prefix(11) == "GRANT GROUP") {
        pos = 11;
    } else {
        return std::nullopt;
    }
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) {
        ++pos;
    }
    auto read_ident = [&]() -> std::string {
        std::string id;
        if (pos < s.size() && s[pos] == '[') {
            ++pos;
            while (pos < s.size() && s[pos] != ']') {
                id.push_back(s[pos++]);
            }
            if (pos < s.size() && s[pos] == ']') ++pos;
            return id;
        }
        while (pos < s.size()) {
            char c = s[pos];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
                c == ':' || c == '$') {
                id.push_back(c);
                ++pos;
            } else {
                break;
            }
        }
        return id;
    };
    auto match_kw = [&](const char* kw) -> bool {
        const auto klen = std::char_traits<char>::length(kw);
        if (pos + klen > s.size()) return false;
        for (std::size_t i = 0; i < klen; ++i) {
            if (std::toupper(static_cast<unsigned char>(s[pos + i])) !=
                std::toupper(static_cast<unsigned char>(kw[i]))) {
                return false;
            }
        }
        pos += klen;
        return true;
    };

    const std::string group = read_ident();
    if (group.empty()) return std::nullopt;
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) {
        ++pos;
    }
    if (is_revoke) {
        if (!match_kw("FROM")) return std::nullopt;
    } else {
        if (!match_kw("TO")) return std::nullopt;
    }
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) {
        ++pos;
    }
    const std::string user = read_ident();
    if (user.empty()) return std::nullopt;
    return std::pair<bool, std::pair<std::string, std::string>>{
        is_revoke, {group, user}};
}

std::string member_grant_sql(SqlDdlDialect dialect,
                             const std::string& group,
                             const std::string& user) {
    const std::string qtab = quote_ident(dialect, kMemberTable);
    const std::string g = sql_escape(group);
    const std::string u = sql_escape(normalize_user_name(user));
    const std::string where_ci =
        ci_compare_sql("user_name", user) + " AND group_name = " + g;
    switch (dialect) {
        case SqlDdlDialect::Mssql:
            return "IF NOT EXISTS (SELECT 1 FROM " + qtab +
                   " WHERE " + where_ci + ") "
                   "INSERT INTO " + qtab + " (user_name, group_name) VALUES (" +
                   u + ", " + g + ")";
        case SqlDdlDialect::Firebird:
            return "UPDATE OR INSERT INTO " + qtab +
                   " (user_name, group_name) VALUES (" + u + ", " + g +
                   ") MATCHING (user_name, group_name)";
        case SqlDdlDialect::Postgres:
        case SqlDdlDialect::Sqlite:
            return "INSERT INTO " + qtab +
                   " (user_name, group_name) VALUES (" + u + ", " + g +
                   ") ON CONFLICT (user_name, group_name) DO NOTHING";
        case SqlDdlDialect::Maria:
            return "INSERT IGNORE INTO " + qtab +
                   " (user_name, group_name) VALUES (" + u + ", " + g + ")";
        case SqlDdlDialect::Oracle:
            return "MERGE INTO " + qtab + " t USING (SELECT " + u +
                   " AS user_name, " + g +
                   " AS group_name FROM dual) s "
                   "ON (LOWER(t.user_name) = LOWER(s.user_name) AND "
                   "t.group_name = s.group_name) "
                   "WHEN NOT MATCHED THEN INSERT (user_name, group_name) "
                   "VALUES (s.user_name, s.group_name)";
        default:
            return "INSERT IGNORE INTO " + qtab +
                   " (user_name, group_name) VALUES (" + u + ", " + g + ")";
    }
}

std::string member_revoke_sql(SqlDdlDialect dialect,
                              const std::string& group,
                              const std::string& user) {
    const std::string qtab = quote_ident(dialect, kMemberTable);
    return "DELETE FROM " + qtab + " WHERE " +
           ci_compare_sql("user_name", user) + " AND group_name = " +
           sql_escape(group);
}

std::string groups_for_user_sql(SqlDdlDialect dialect,
                                const std::string& username) {
    const std::string qtab = quote_ident(dialect, kMemberTable);
    const std::string where = ci_compare_sql("user_name", username);
    switch (dialect) {
        case SqlDdlDialect::Mssql:
            return "SELECT STRING_AGG(group_name, ',') FROM " + qtab +
                   " WHERE " + where;
        case SqlDdlDialect::Postgres:
            return "SELECT string_agg(group_name, ',') FROM " + qtab +
                   " WHERE " + where;
        case SqlDdlDialect::Firebird:
            return "SELECT LIST(group_name, ',') FROM " + qtab +
                   " WHERE " + where;
        case SqlDdlDialect::Maria:
        case SqlDdlDialect::Sqlite:
            return "SELECT GROUP_CONCAT(group_name) FROM " + qtab +
                   " WHERE " + where;
        case SqlDdlDialect::Oracle:
            return "SELECT LISTAGG(group_name, ',') WITHIN GROUP "
                   "(ORDER BY group_name) FROM " + qtab + " WHERE " + where;
        default:
            return "SELECT GROUP_CONCAT(group_name) FROM " + qtab +
                   " WHERE " + where;
    }
}

void split_csv_groups(const std::string& csv,
                      const std::function<void(const std::string&)>& fn) {
    std::string token;
    for (char c : csv) {
        if (c == ',') {
            if (!token.empty()) fn(token);
            token.clear();
        } else {
            token.push_back(c);
        }
    }
    if (!token.empty()) fn(token);
}

std::string grant_upsert_sql(SqlDdlDialect dialect,
                             const std::string& obj_type,
                             const std::string& obj_name,
                             const std::string& parent,
                             const std::string& grantee,
                             bool is_group,
                             uint32_t add_bits) {
    const std::string qtab = quote_ident(dialect, kAclTable);
    const std::string ig   = is_group ? "1" : "0";
    const std::string bits = std::to_string(add_bits);
    switch (dialect) {
        case SqlDdlDialect::Sqlite:
            return "INSERT INTO " + qtab +
                   " (obj_type, obj_name, parent, grantee, is_group, bitmask) "
                   "VALUES (" + sql_escape(obj_type) + ", " +
                   sql_escape(obj_name) + ", " + sql_escape(parent) + ", " +
                   sql_escape(grantee) + ", " + ig + ", " + bits + ") "
                   "ON CONFLICT(obj_type, obj_name, grantee) DO UPDATE SET "
                   "bitmask = " + qtab + ".bitmask | " + bits + ", "
                   "parent = excluded.parent, is_group = excluded.is_group";
        case SqlDdlDialect::Postgres:
            return "INSERT INTO " + qtab +
                   " (obj_type, obj_name, parent, grantee, is_group, bitmask) "
                   "VALUES (" + sql_escape(obj_type) + ", " +
                   sql_escape(obj_name) + ", " + sql_escape(parent) + ", " +
                   sql_escape(grantee) + ", " + ig + ", " + bits + ") "
                   "ON CONFLICT (obj_type, obj_name, grantee) DO UPDATE SET "
                   "bitmask = " + qtab + ".bitmask | EXCLUDED.bitmask, "
                   "parent = EXCLUDED.parent, is_group = EXCLUDED.is_group";
        case SqlDdlDialect::Maria:
            return "INSERT INTO " + qtab +
                   " (obj_type, obj_name, parent, grantee, is_group, bitmask) "
                   "VALUES (" + sql_escape(obj_type) + ", " +
                   sql_escape(obj_name) + ", " + sql_escape(parent) + ", " +
                   sql_escape(grantee) + ", " + ig + ", " + bits + ") "
                   "ON DUPLICATE KEY UPDATE "
                   "bitmask = bitmask | " + bits + ", "
                   "parent = VALUES(parent), is_group = VALUES(is_group)";
        case SqlDdlDialect::Mssql:
            return "MERGE " + qtab + " AS t "
                   "USING (SELECT " + sql_escape(obj_type) + " AS obj_type, " +
                   sql_escape(obj_name) + " AS obj_name, " +
                   sql_escape(grantee) + " AS grantee, " + bits +
                   " AS add_bits, " + sql_escape(parent) + " AS parent, " +
                   ig + " AS is_group) AS s "
                   "ON t.obj_type = s.obj_type AND t.obj_name = s.obj_name "
                   "AND t.grantee = s.grantee "
                   "WHEN MATCHED THEN UPDATE SET "
                   "bitmask = t.bitmask | s.add_bits, parent = s.parent, "
                   "is_group = s.is_group "
                   "WHEN NOT MATCHED THEN INSERT "
                   "(obj_type, obj_name, parent, grantee, is_group, bitmask) "
                   "VALUES (s.obj_type, s.obj_name, s.parent, s.grantee, "
                   "s.is_group, s.add_bits);";
        case SqlDdlDialect::Firebird:
            return "MERGE INTO " + qtab + " t "
                   "USING (SELECT " + sql_escape(obj_type) +
                   " obj_type, " + sql_escape(obj_name) + " obj_name, " +
                   sql_escape(grantee) + " grantee, " + bits +
                   " add_bits, " + sql_escape(parent) + " parent, " + ig +
                   " is_group FROM RDB$DATABASE) s "
                   "ON t.obj_type = s.obj_type AND t.obj_name = s.obj_name "
                   "AND t.grantee = s.grantee "
                   "WHEN MATCHED THEN UPDATE SET "
                   "bitmask = BIN_OR(t.bitmask, s.add_bits), "
                   "parent = s.parent, is_group = s.is_group "
                   "WHEN NOT MATCHED THEN INSERT "
                   "(obj_type, obj_name, parent, grantee, is_group, bitmask) "
                   "VALUES (s.obj_type, s.obj_name, s.parent, s.grantee, "
                   "s.is_group, s.add_bits)";
        case SqlDdlDialect::Oracle:
            return "MERGE INTO " + qtab + " t "
                   "USING (SELECT " + sql_escape(obj_type) +
                   " AS obj_type, " + sql_escape(obj_name) + " AS obj_name, " +
                   sql_escape(grantee) + " AS grantee, " + bits +
                   " AS add_bits, " + sql_escape(parent) + " AS parent, " + ig +
                   " AS is_group FROM dual) s "
                   "ON (t.obj_type = s.obj_type AND t.obj_name = s.obj_name "
                   "AND t.grantee = s.grantee) "
                   "WHEN MATCHED THEN UPDATE SET "
                   "bitmask = t.bitmask + s.add_bits - BITAND(t.bitmask, s.add_bits), "
                   "parent = s.parent, is_group = s.is_group "
                   "WHEN NOT MATCHED THEN INSERT "
                   "(obj_type, obj_name, parent, grantee, is_group, bitmask) "
                   "VALUES (s.obj_type, s.obj_name, s.parent, s.grantee, "
                   "s.is_group, s.add_bits)";
    }
    return {};
}

std::string revoke_sql(SqlDdlDialect dialect,
                       const std::string& obj_type,
                       const std::string& obj_name,
                       const std::string& grantee,
                       uint32_t revoke_bits) {
    const std::string qtab = quote_ident(dialect, kAclTable);
    const std::string mask = std::to_string(revoke_bits);
    const std::string where =
        " WHERE obj_type = " + sql_escape(obj_type) +
        " AND obj_name = " + sql_escape(obj_name) +
        " AND " + ci_compare_sql("grantee", grantee);
    switch (dialect) {
        case SqlDdlDialect::Firebird:
            return "UPDATE " + qtab + " SET bitmask = BIN_AND(bitmask, -1 - " +
                   mask + ")" + where;
        case SqlDdlDialect::Oracle:
            return "UPDATE " + qtab +
                   " SET bitmask = bitmask - BITAND(bitmask, " + mask + ")" +
                   where;
        default:
            return "UPDATE " + qtab + " SET bitmask = bitmask & ~" + mask +
                   where;
    }
}

}  // namespace

std::string acl_table_ddl(SqlDdlDialect dialect) {
    const std::string qtab = quote_ident(dialect, kAclTable);
    switch (dialect) {
        case SqlDdlDialect::Mssql:
            return "IF NOT EXISTS (SELECT 1 FROM sys.tables WHERE name = '" +
                   std::string(kAclTable) + "') CREATE TABLE " + qtab +
                   " (obj_type NVARCHAR(8) NOT NULL, "
                   "obj_name NVARCHAR(200) NOT NULL, "
                   "parent NVARCHAR(200) NOT NULL DEFAULT '', "
                   "grantee NVARCHAR(200) NOT NULL, "
                   "is_group INT NOT NULL DEFAULT 0, "
                   "bitmask INT NOT NULL DEFAULT 0, "
                   "PRIMARY KEY (obj_type, obj_name, grantee))";
        case SqlDdlDialect::Maria:
            return "CREATE TABLE IF NOT EXISTS " + qtab +
                   " (obj_type VARCHAR(8) NOT NULL, "
                   "obj_name VARCHAR(200) NOT NULL, "
                   "parent VARCHAR(200) NOT NULL DEFAULT '', "
                   "grantee VARCHAR(200) NOT NULL, "
                   "is_group TINYINT NOT NULL DEFAULT 0, "
                   "bitmask INT UNSIGNED NOT NULL DEFAULT 0, "
                   "PRIMARY KEY (obj_type, obj_name, grantee))";
        case SqlDdlDialect::Firebird:
            return "EXECUTE BLOCK AS BEGIN IF (NOT EXISTS(SELECT 1 FROM "
                   "RDB$RELATIONS WHERE TRIM(RDB$RELATION_NAME) = '" +
                   std::string(kAclTable) + "')) THEN EXECUTE STATEMENT '" +
                   "CREATE TABLE " + qtab +
                   " (obj_type VARCHAR(8) NOT NULL, "
                   "obj_name VARCHAR(200) NOT NULL, "
                   "parent VARCHAR(200) DEFAULT '''', "
                   "grantee VARCHAR(200) NOT NULL, "
                   "is_group SMALLINT DEFAULT 0, "
                   "bitmask INTEGER DEFAULT 0, "
                   "PRIMARY KEY (obj_type, obj_name, grantee))'; END";
        case SqlDdlDialect::Oracle:
            return "DECLARE v_cnt NUMBER; BEGIN SELECT COUNT(*) INTO v_cnt "
                   "FROM user_tables WHERE table_name = '" +
                   std::string(kAclTable) + "'; IF v_cnt = 0 THEN "
                   "EXECUTE IMMEDIATE 'CREATE TABLE " + qtab +
                   " (obj_type VARCHAR2(8) NOT NULL, "
                   "obj_name VARCHAR2(200) NOT NULL, "
                   "parent VARCHAR2(200) DEFAULT '''' NOT NULL, "
                   "grantee VARCHAR2(200) NOT NULL, "
                   "is_group NUMBER(1) DEFAULT 0 NOT NULL, "
                   "bitmask NUMBER(10) DEFAULT 0 NOT NULL, "
                   "PRIMARY KEY (obj_type, obj_name, grantee))'; END IF; END";
        case SqlDdlDialect::Postgres:
        case SqlDdlDialect::Sqlite:
        default:
            return "CREATE TABLE IF NOT EXISTS " + qtab +
                   " (obj_type TEXT NOT NULL, "
                   "obj_name TEXT NOT NULL, "
                   "parent TEXT NOT NULL DEFAULT '', "
                   "grantee TEXT NOT NULL, "
                   "is_group INTEGER NOT NULL DEFAULT 0, "
                   "bitmask INTEGER NOT NULL DEFAULT 0, "
                   "PRIMARY KEY (obj_type, obj_name, grantee))";
    }
}

std::string user_table_ddl(SqlDdlDialect dialect) {
    const std::string qtab = quote_ident(dialect, kUserTable);
    switch (dialect) {
        case SqlDdlDialect::Mssql:
            return "IF NOT EXISTS (SELECT 1 FROM sys.tables WHERE name = '" +
                   std::string(kUserTable) + "') CREATE TABLE " + qtab +
                   " (user_name NVARCHAR(200) NOT NULL PRIMARY KEY)";
        case SqlDdlDialect::Maria:
            return "CREATE TABLE IF NOT EXISTS " + qtab +
                   " (user_name VARCHAR(200) NOT NULL PRIMARY KEY)";
        case SqlDdlDialect::Firebird:
            return "EXECUTE BLOCK AS BEGIN IF (NOT EXISTS(SELECT 1 FROM "
                   "RDB$RELATIONS WHERE TRIM(RDB$RELATION_NAME) = '" +
                   std::string(kUserTable) + "')) THEN EXECUTE STATEMENT '" +
                   "CREATE TABLE " + qtab +
                   " (user_name VARCHAR(200) NOT NULL PRIMARY KEY)'; END";
        case SqlDdlDialect::Oracle:
            return "DECLARE v_cnt NUMBER; BEGIN SELECT COUNT(*) INTO v_cnt "
                   "FROM user_tables WHERE table_name = '" +
                   std::string(kUserTable) + "'; IF v_cnt = 0 THEN "
                   "EXECUTE IMMEDIATE 'CREATE TABLE " + qtab +
                   " (user_name VARCHAR2(200) NOT NULL PRIMARY KEY)'; "
                   "END IF; END";
        case SqlDdlDialect::Postgres:
        case SqlDdlDialect::Sqlite:
        default:
            return "CREATE TABLE IF NOT EXISTS " + qtab +
                   " (user_name TEXT NOT NULL PRIMARY KEY)";
    }
}

std::string connect_user_register_sql(SqlDdlDialect dialect,
                                       const std::string& user) {
    const std::string qtab = quote_ident(dialect, kUserTable);
    const std::string u = sql_escape(normalize_user_name(user));
    switch (dialect) {
        case SqlDdlDialect::Mssql:
            return "IF NOT EXISTS (SELECT 1 FROM " + qtab +
                   " WHERE " + ci_compare_sql("user_name", user) + ") "
                   "INSERT INTO " + qtab + " (user_name) VALUES (" + u + ")";
        case SqlDdlDialect::Firebird:
            return "UPDATE OR INSERT INTO " + qtab +
                   " (user_name) VALUES (" + u + ") MATCHING (user_name)";
        case SqlDdlDialect::Postgres:
        case SqlDdlDialect::Sqlite:
            return "INSERT INTO " + qtab + " (user_name) VALUES (" + u +
                   ") ON CONFLICT (user_name) DO NOTHING";
        case SqlDdlDialect::Maria:
            return "INSERT IGNORE INTO " + qtab + " (user_name) VALUES (" +
                   u + ")";
        case SqlDdlDialect::Oracle:
            return "MERGE INTO " + qtab + " t USING (SELECT " + u +
                   " AS user_name FROM dual) s "
                   "ON (LOWER(t.user_name) = LOWER(s.user_name)) "
                   "WHEN NOT MATCHED THEN INSERT (user_name) "
                   "VALUES (s.user_name)";
        default:
            return "INSERT IGNORE INTO " + qtab + " (user_name) VALUES (" +
                   u + ")";
    }
}

std::string member_table_ddl(SqlDdlDialect dialect) {
    const std::string qtab = quote_ident(dialect, kMemberTable);
    switch (dialect) {
        case SqlDdlDialect::Mssql:
            return "IF NOT EXISTS (SELECT 1 FROM sys.tables WHERE name = '" +
                   std::string(kMemberTable) + "') CREATE TABLE " + qtab +
                   " (user_name NVARCHAR(200) NOT NULL, "
                   "group_name NVARCHAR(200) NOT NULL, "
                   "PRIMARY KEY (user_name, group_name))";
        case SqlDdlDialect::Maria:
            return "CREATE TABLE IF NOT EXISTS " + qtab +
                   " (user_name VARCHAR(200) NOT NULL, "
                   "group_name VARCHAR(200) NOT NULL, "
                   "PRIMARY KEY (user_name, group_name))";
        case SqlDdlDialect::Firebird:
            return "EXECUTE BLOCK AS BEGIN IF (NOT EXISTS(SELECT 1 FROM "
                   "RDB$RELATIONS WHERE TRIM(RDB$RELATION_NAME) = '" +
                   std::string(kMemberTable) + "')) THEN EXECUTE STATEMENT '" +
                   "CREATE TABLE " + qtab +
                   " (user_name VARCHAR(200) NOT NULL, "
                   "group_name VARCHAR(200) NOT NULL, "
                   "PRIMARY KEY (user_name, group_name))'; END";
        case SqlDdlDialect::Oracle:
            return "DECLARE v_cnt NUMBER; BEGIN SELECT COUNT(*) INTO v_cnt "
                   "FROM user_tables WHERE table_name = '" +
                   std::string(kMemberTable) + "'; IF v_cnt = 0 THEN "
                   "EXECUTE IMMEDIATE 'CREATE TABLE " + qtab +
                   " (user_name VARCHAR2(200) NOT NULL, "
                   "group_name VARCHAR2(200) NOT NULL, "
                   "PRIMARY KEY (user_name, group_name))'; END IF; END";
        case SqlDdlDialect::Postgres:
        case SqlDdlDialect::Sqlite:
        default:
            return "CREATE TABLE IF NOT EXISTS " + qtab +
                   " (user_name TEXT NOT NULL, "
                   "group_name TEXT NOT NULL, "
                   "PRIMARY KEY (user_name, group_name))";
    }
}

std::string acl_users_catalog_sql(SqlDdlDialect dialect) {
    const std::string qacl = quote_ident(dialect, kAclTable);
    const std::string qmem = quote_ident(dialect, kMemberTable);
    const std::string qusr = quote_ident(dialect, kUserTable);
    // Column alias is SAP's "Name" (matching system.users on the native DD
    // backend), so the same client SQL works against either backend.
    return std::string("SELECT 'PUBLIC' AS \"Name\" UNION SELECT DISTINCT "
                       "user_name AS \"Name\" FROM ") +
           qmem + " UNION SELECT DISTINCT user_name AS \"Name\" FROM " +
           qusr + " UNION SELECT DISTINCT grantee AS \"Name\" FROM " +
           qacl + " WHERE is_group = 0";
}

std::string acl_groups_catalog_sql(SqlDdlDialect dialect) {
    const std::string qacl = quote_ident(dialect, kAclTable);
    const std::string qmem = quote_ident(dialect, kMemberTable);
    // SAP's system.usergroups column is "Name" — see acl_users_catalog_sql().
    return std::string("SELECT 'PUBLIC' AS \"Name\" UNION SELECT DISTINCT "
                       "group_name AS \"Name\" FROM ") +
           qmem + " UNION SELECT DISTINCT grantee AS \"Name\" FROM " +
           qacl + " WHERE is_group != 0";
}

std::string acl_members_catalog_sql(SqlDdlDialect dialect) {
    const std::string qmem = quote_ident(dialect, kMemberTable);
    // SAP's system.usergroupmembers is User_Name THEN Group_Name.
    return std::string(
               "SELECT user_name AS \"User_Name\", group_name AS \"Group_Name\" "
               "FROM ") +
           qmem +
           " UNION SELECT 'PUBLIC' AS \"User_Name\", 'PUBLIC' AS \"Group_Name\"";
}

std::string acl_permissions_select_sql(SqlDdlDialect dialect) {
    const std::string qtab = quote_ident(dialect, kAclTable);
    return std::string(
        "SELECT obj_name AS \"OBJ_NAME\", obj_type AS \"OBJ_TYPE\", "
        "parent AS \"PARENT\", grantee AS \"GRANTEE\", ") +
           perm_col_sql("SELECT", DD::DD_PERM_SELECT) + ", " +
           perm_col_sql("UPDATE", DD::DD_PERM_UPDATE) + ", " +
           perm_col_sql("INSERT", DD::DD_PERM_INSERT) + ", " +
           perm_col_sql("DELETE", DD::DD_PERM_DELETE) + ", " +
           perm_col_sql("EXECUTE", DD::DD_PERM_EXECUTE) + ", " +
           "'2' AS \"ACCESS\", "
           "CASE WHEN is_group != 0 THEN '' ELSE "
           "CASE WHEN (bitmask & " +
           std::to_string(DD::DD_PERM_GRANT) + ") != 0 THEN '2' ELSE '0' END "
           "END AS \"INHERIT\", "
           "CASE WHEN (bitmask & 128) != 0 THEN "
           "CASE WHEN is_group != 0 THEN '1' ELSE '2' END ELSE '0' END "
           "AS \"CREATE\", "
           "CASE WHEN (bitmask & 256) != 0 THEN "
           "CASE WHEN is_group != 0 THEN '1' ELSE '2' END ELSE '0' END "
           "AS \"ALTER\", "
           "CASE WHEN (bitmask & 512) != 0 THEN "
           "CASE WHEN is_group != 0 THEN '1' ELSE '2' END ELSE '0' END "
           "AS \"DROP\" "
           "FROM " + qtab;
}

std::optional<util::Result<void>> try_sql_acl_statement(
    const std::string& sql,
    SqlDdlDialect dialect,
    SqlExecFn exec_sql) {
    if (auto gm = try_parse_group_membership(sql)) {
        if (auto r = ensure_member_table(dialect, exec_sql); !r) return r.error();
        const bool is_revoke = gm->first;
        const std::string& group = gm->second.first;
        const std::string& user  = gm->second.second;
        if (is_revoke) {
            if (auto r = exec_sql(member_revoke_sql(dialect, group, user)); !r) {
                return r.error();
            }
        } else {
            if (auto r = exec_sql(member_grant_sql(dialect, group, user)); !r) {
                return r.error();
            }
        }
        return util::Result<void>{};
    }

    const bool is_grant = openads::sql::sql_is_grant(sql);
    const bool is_revoke = openads::sql::sql_is_revoke(sql);
    if (!is_grant && !is_revoke) return std::nullopt;

    openads::sql::GrantStmt g;
    if (is_grant) {
        auto gs = openads::sql::parse_grant(sql);
        if (!gs) return gs.error();
        g = gs.value();
    } else {
        auto gs = openads::sql::parse_revoke(sql);
        if (!gs) return gs.error();
        g = gs.value();
    }

    if (auto r = ensure_acl_table(dialect, exec_sql); !r) return r.error();

    const std::string obj_type = g.column.empty() ? "1" : "4";
    const std::string obj_name = g.column.empty() ? g.object : g.column;
    const std::string parent   = g.column.empty() ? "" : g.object;
    const std::string grantee_raw = g.principal;
    const bool is_grp             = grantee_is_group(grantee_raw);
    const std::string grantee     = is_grp ? grantee_raw
                                             : normalize_user_name(grantee_raw);

    if (is_grant) {
        const uint32_t add_bits = bits_from_right(g.right);
        if (auto r = exec_sql(grant_upsert_sql(
                dialect, obj_type, obj_name, parent, grantee, is_grp, add_bits));
            !r) {
            return r.error();
        }
        return util::Result<void>{};
    }

    const uint32_t revoke_bits = revoke_mask_from_right(g.right);
    if (auto r = exec_sql(revoke_sql(dialect, obj_type, obj_name, grantee,
                                     revoke_bits));
        !r) {
        return r.error();
    }
    return util::Result<void>{};
}

namespace {

uint32_t expand_bitmask(uint32_t mask) {
    if (mask & DD::DD_PERM_FULL) {
        return DD::DD_PERM_SELECT | DD::DD_PERM_UPDATE |
               DD::DD_PERM_INSERT | DD::DD_PERM_DELETE |
               DD::DD_PERM_EXECUTE | DD::DD_PERM_REFERENCE |
               DD::DD_PERM_GRANT;
    }
    return mask;
}

openads::engine::DataDict::EffectiveOps ops_from_bitmask(uint32_t mask) {
    const uint32_t m = expand_bitmask(mask);
    openads::engine::DataDict::EffectiveOps ops;
    ops.select_  = (m & DD::DD_PERM_SELECT)  != 0;
    ops.update_  = (m & DD::DD_PERM_UPDATE)  != 0;
    ops.execute_ = (m & DD::DD_PERM_EXECUTE) != 0;
    ops.insert_  = (m & DD::DD_PERM_INSERT)  != 0;
    ops.delete_  = (m & DD::DD_PERM_DELETE)  != 0;
    ops.open     = ops.select_ || ops.update_ || ops.insert_ || ops.delete_;
    return ops;
}

openads::engine::DataDict::EffectiveOps full_ops() {
    openads::engine::DataDict::EffectiveOps ops;
    ops.open = ops.select_ = ops.update_ =
        ops.insert_ = ops.delete_ = ops.execute_ = true;
    return ops;
}

std::optional<std::string> query_cell(const SqlQueryFn& query,
                                      const std::string& sql) {
    auto r = query(sql);
    if (!r) return std::nullopt;
    return r.value();
}

}  // namespace

bool sql_acl_has_any(SqlDdlDialect dialect, const SqlQueryFn& query) {
    const std::string qtab = quote_ident(dialect, kAclTable);
    return query_cell(query, "SELECT 1 FROM " + qtab + limit_one_suffix(dialect))
        .has_value();
}

openads::engine::DataDict::EffectiveOps sql_acl_effective_ops(
    SqlDdlDialect dialect,
    const SqlQueryFn& query,
    const std::string& username,
    const std::string& object_name) {
    if (!sql_acl_has_any(dialect, query)) {
        return full_ops();
    }
    const std::string qtab = quote_ident(dialect, kAclTable);
    const std::string obj_sql = sql_escape(object_name);
    const std::string probe =
        "SELECT 1 FROM " + qtab +
        " WHERE obj_type = '1' AND obj_name = " + obj_sql + limit_one_suffix(dialect);
    if (!query_cell(query, probe).has_value()) {
        return full_ops();
    }

    uint32_t bits = 0;
    auto accumulate = [&](const std::string& principal) {
        const std::string sql =
            "SELECT bitmask FROM " + qtab +
            " WHERE obj_type = '1' AND obj_name = " + obj_sql +
            " AND " + ci_compare_sql("grantee", principal) + limit_one_suffix(dialect);
        if (auto cell = query_cell(query, sql); cell && !cell->empty()) {
            try {
                bits |= static_cast<uint32_t>(std::stoul(*cell));
            } catch (...) {
            }
        }
    };
    if (!username.empty()) {
        accumulate(username);
        if (auto groups = query_cell(query, groups_for_user_sql(dialect, username));
            groups && !groups->empty()) {
            split_csv_groups(*groups, [&](const std::string& g) {
                accumulate(g);
            });
        }
    }
    accumulate("PUBLIC");
    return ops_from_bitmask(bits);
}

void sql_acl_register_connect_user(SqlDdlDialect dialect,
                                   SqlExecFn exec_sql,
                                   const std::string& username) {
    if (username.empty()) return;
    if (auto r = ensure_user_table(dialect, exec_sql); !r) return;
    (void)exec_sql(connect_user_register_sql(dialect, username));
}

}  // namespace openads::sql_backend