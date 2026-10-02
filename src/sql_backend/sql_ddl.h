#pragma once

#include "sql_backend/sql_common.h"
#include "util/result.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openads::sql_backend {

enum class SqlDdlDialect {
    Sqlite,
    Postgres,
    Maria,
    Mssql,
    Firebird,
    Oracle,
};

struct SqlDdlColumn {
    std::string  name;
    char         xbase_type = 'C';
    std::uint16_t length    = 10;
    std::uint8_t dec        = 0;
};

util::Result<std::string> build_create_table_ddl(
    SqlDdlDialect dialect,
    const std::string& table_name,
    const std::vector<SqlDdlColumn>& columns);

// One ALTER TABLE … ADD COLUMN statement per column (portable across dialects).
util::Result<std::vector<std::string>> build_alter_table_add_ddl(
    SqlDdlDialect dialect,
    const std::string& table_name,
    const std::vector<SqlDdlColumn>& columns);

util::Result<std::vector<std::string>> build_alter_table_drop_ddl(
    SqlDdlDialect dialect,
    const std::string& table_name,
    const std::vector<std::string>& column_names);

// CHANGE: adjust length/decimals (same xBase type). SQLite TEXT/INTEGER
// columns treat length changes as no-ops at the SQL layer.
util::Result<std::vector<std::string>> build_alter_table_change_ddl(
    SqlDdlDialect dialect,
    const std::string& table_name,
    const std::vector<SqlDdlColumn>& columns);

util::Result<std::string> build_drop_table_ddl(
    SqlDdlDialect dialect,
    const std::string& table_name,
    bool if_exists = false);

// CREATE INDEX for SQL URI navigational SEEK (column or UPPER(column) only).
util::Result<std::string> build_create_index_ddl(
    SqlDdlDialect dialect,
    const std::string& table_name,
    const std::string& index_name,
    IndexExprKind expr_kind,
    const std::string& column);

}  // namespace openads::sql_backend