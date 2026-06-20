#include "sql_backend/maria_connection.h"

#include "sql_backend/maria_backend.h"
#include "sql_backend/sql_common.h"

#include <algorithm>
#include <cstdlib>

#if defined(OPENADS_WITH_MARIADB)
#include <mysql.h>
#endif

namespace openads::sql_backend {

namespace {

#if defined(OPENADS_WITH_MARIADB)

std::string quote_ident(const std::string& name) {
    return '`' + name + '`';
}

std::string escape_literal(MYSQL* conn, const std::string& value) {
    std::string out;
    out.resize(value.size() * 2 + 1);
    const unsigned long len = mysql_real_escape_string(
        conn, out.data(), value.c_str(),
        static_cast<unsigned long>(value.size()));
    out.resize(len);
    return '\'' + out + '\'';
}

std::string pk_where_clause(MYSQL* conn, const MariaTable& tbl,
                            const MariaTable::PkRow& pk) {
    std::string sql;
    for (std::size_t i = 0; i < tbl.pk_columns.size(); ++i) {
        if (i > 0) sql += " AND ";
        sql += quote_ident(tbl.pk_columns[i]) + " = " +
               escape_literal(conn, pk.values[i]);
    }
    return sql;
}

util::Result<void> load_current_row(MYSQL* conn, MariaTable* tbl);

util::Result<std::vector<std::string>>
discover_pk_columns(MYSQL* conn, const std::string& table_name) {
    const std::string sql =
        "SELECT COLUMN_NAME FROM information_schema.KEY_COLUMN_USAGE "
        "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = '" +
        table_name +
        "' AND CONSTRAINT_NAME = 'PRIMARY' "
        "ORDER BY ORDINAL_POSITION";
    if (mysql_query(conn, sql.c_str()) != 0) {
        return maria_error("pk discovery", mysql_error(conn));
    }
    MYSQL_RES* res = mysql_store_result(conn);
    if (res == nullptr) {
        return maria_error("pk discovery", mysql_error(conn));
    }
    std::vector<std::string> cols;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        if (row[0] != nullptr) cols.emplace_back(row[0]);
    }
    mysql_free_result(res);
    if (cols.empty()) {
        return util::Error{5001, 0, "table has no primary key", table_name};
    }
    return cols;
}

util::Result<std::vector<MariaTable::FieldDesc>>
describe_table_impl(MYSQL* conn, MariaTable* tbl) {
    if (!is_safe_identifier(tbl->name)) {
        return util::Error{5001, 0, "invalid table name", tbl->name};
    }
    const std::string sql =
        "SELECT COLUMN_NAME, DATA_TYPE, IS_NULLABLE, "
        "CHARACTER_MAXIMUM_LENGTH, NUMERIC_PRECISION, NUMERIC_SCALE "
        "FROM information_schema.COLUMNS "
        "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = '" +
        tbl->name + "' ORDER BY ORDINAL_POSITION";
    if (mysql_query(conn, sql.c_str()) != 0) {
        return maria_error("describe_table", mysql_error(conn));
    }
    MYSQL_RES* res = mysql_store_result(conn);
    if (res == nullptr) {
        return maria_error("describe_table", mysql_error(conn));
    }
    const unsigned int rows = mysql_num_rows(res);
    if (rows == 0) {
        mysql_free_result(res);
        return util::Error{5001, 0, "table not found or has no columns",
                           tbl->name};
    }
    std::vector<MariaTable::FieldDesc> out;
    out.reserve(rows);
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        const bool nullable = row[2] != nullptr && row[2][0] == 'Y';
        out.push_back(map_maria_column(
            row[0], row[1], nullable,
            row[3] ? std::atoi(row[3]) : 0,
            row[4] ? std::atoi(row[4]) : 0,
            row[5] ? std::atoi(row[5]) : 0));
    }
    mysql_free_result(res);
    tbl->fields        = out;
    tbl->fields_cached = true;
    return out;
}

util::Result<void> position_at_pk(MYSQL* conn, MariaTable* tbl,
                                  const MariaTable::PkRow& pk) {
    if (tbl == nullptr || conn == nullptr) {
        return util::Error{5001, 0, "invalid mariadb table state", ""};
    }
    auto it = std::find_if(tbl->pk_snapshot.begin(), tbl->pk_snapshot.end(),
        [&](const MariaTable::PkRow& row) {
            return row.values == pk.values;
        });
    if (it == tbl->pk_snapshot.end()) {
        tbl->positioned = false;
        tbl->row_valid  = false;
        return util::Result<void>{};
    }
    tbl->pos           = static_cast<std::size_t>(
        std::distance(tbl->pk_snapshot.begin(), it));
    tbl->positioned    = true;
    tbl->current_recno = static_cast<std::uint32_t>(tbl->pos + 1);
    return load_current_row(conn, tbl);
}

util::Result<void> load_current_row(MYSQL* conn, MariaTable* tbl) {
    if (tbl == nullptr || conn == nullptr) {
        return util::Error{5001, 0, "invalid mariadb table state", ""};
    }
    if (!tbl->positioned) {
        tbl->row_valid      = false;
        tbl->current_row.clear();
        tbl->current_nulls.clear();
        return util::Result<void>{};
    }
    if (!tbl->fields_cached) {
        auto d = describe_table_impl(conn, tbl);
        if (!d) return d.error();
    }

    const std::string sql =
        "SELECT * FROM " + quote_ident(tbl->name) + " WHERE " +
        pk_where_clause(conn, *tbl, tbl->pk_snapshot[tbl->pos]);
    if (mysql_query(conn, sql.c_str()) != 0) {
        return maria_error("load row", mysql_error(conn));
    }
    MYSQL_RES* res = mysql_store_result(conn);
    if (res == nullptr) {
        return maria_error("load row", mysql_error(conn));
    }

    tbl->current_row.clear();
    tbl->current_nulls.clear();
    tbl->row_valid = false;

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row != nullptr) {
        const unsigned int cols = mysql_num_fields(res);
        tbl->current_row.resize(cols);
        tbl->current_nulls.resize(cols);
        for (unsigned int c = 0; c < cols; ++c) {
            bool is_null = false;
            tbl->current_row[c] = format_maria_value(row, c, is_null);
            tbl->current_nulls[c] = is_null;
        }
        tbl->row_valid = true;
    } else {
        tbl->positioned = false;
    }
    mysql_free_result(res);
    return util::Result<void>{};
}

std::string pk_select_list(const MariaTable& tbl) {
    std::string out;
    for (std::size_t i = 0; i < tbl.pk_columns.size(); ++i) {
        if (i > 0) out += ", ";
        out += quote_ident(tbl.pk_columns[i]);
    }
    return out;
}

std::string pk_order_by(const MariaTable& tbl) {
    return pk_select_list(tbl);
}

#endif

} // namespace

struct MariaConnection::Impl {
#if defined(OPENADS_WITH_MARIADB)
    MYSQL* conn = nullptr;
#endif
};

MariaConnection::MariaConnection() = default;
MariaConnection::~MariaConnection() { disconnect(); }

MariaConnection::MariaConnection(MariaConnection&& other) noexcept
    : impl_(std::move(other.impl_)) {}

MariaConnection& MariaConnection::operator=(MariaConnection&& other) noexcept {
    if (this != &other) {
        disconnect();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

util::Result<MariaConnection> MariaConnection::open(const MariaUri& uri) {
#if defined(OPENADS_WITH_MARIADB)
    MariaConnection conn;
    conn.impl_ = std::make_unique<Impl>();

    MYSQL* raw = mysql_init(nullptr);
    if (raw == nullptr) {
        return util::Error{5001, 0, "mysql_init failed", ""};
    }

    const char* host = uri.host.empty() ? "127.0.0.1" : uri.host.c_str();
    const char* user = uri.user.empty() ? nullptr : uri.user.c_str();
    const char* pass = uri.password.empty() ? nullptr : uri.password.c_str();
    const char* db   = uri.database.empty() ? nullptr : uri.database.c_str();

    if (mysql_real_connect(raw, host, user, pass, db, uri.port,
                           nullptr, 0) == nullptr) {
        util::Error e = maria_error("connect", mysql_error(raw));
        mysql_close(raw);
        return e;
    }
    conn.impl_->conn = raw;
    return std::move(conn);
#else
    (void)uri;
    return util::Error{5004, 0,
                       "mariadb backend requires OPENADS_WITH_MARIADB=ON",
                       ""};
#endif
}

void MariaConnection::disconnect() noexcept {
#if defined(OPENADS_WITH_MARIADB)
    if (impl_ && impl_->conn) {
        mysql_close(impl_->conn);
        impl_->conn = nullptr;
    }
#endif
    impl_.reset();
}

bool MariaConnection::valid() const noexcept {
#if defined(OPENADS_WITH_MARIADB)
    return impl_ && impl_->conn != nullptr;
#else
    return false;
#endif
}

util::Result<std::unique_ptr<MariaTable>>
MariaConnection::open_table(const std::string& table_name) {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid()) {
        return util::Error{5001, 0, "mariadb connection not open", ""};
    }
    if (!is_safe_identifier(table_name)) {
        return util::Error{5001, 0, "invalid table name", table_name};
    }

    auto tbl = std::make_unique<MariaTable>();
    tbl->conn = this;
    tbl->name = table_name;

    auto pk = discover_pk_columns(impl_->conn, table_name);
    if (!pk) return pk.error();
    tbl->pk_columns = std::move(pk).value();

    const std::string sql =
        "SELECT " + pk_select_list(*tbl) + " FROM " +
        quote_ident(table_name) + " ORDER BY " + pk_order_by(*tbl);
    if (mysql_query(impl_->conn, sql.c_str()) != 0) {
        return maria_error("pk snapshot", mysql_error(impl_->conn));
    }
    MYSQL_RES* res = mysql_store_result(impl_->conn);
    if (res == nullptr) {
        return maria_error("pk snapshot", mysql_error(impl_->conn));
    }

    const unsigned int pk_cols = static_cast<unsigned int>(tbl->pk_columns.size());
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        MariaTable::PkRow pk_row;
        pk_row.values.resize(pk_cols);
        for (unsigned int c = 0; c < pk_cols; ++c) {
            bool is_null = false;
            pk_row.values[c] = format_maria_value(row, c, is_null);
            if (is_null) pk_row.values[c].clear();
        }
        tbl->pk_snapshot.push_back(std::move(pk_row));
    }
    mysql_free_result(res);

    tbl->cached_rec_count = static_cast<std::uint32_t>(tbl->pk_snapshot.size());
    tbl->rec_count_cached = true;
    tbl->positioned       = false;
    tbl->row_valid        = false;
    tbl->current_recno    = 0;
    tbl->current_deleted  = false;
    tbl->pos              = 0;

    if (auto d = describe_table_impl(impl_->conn, tbl.get()); !d) {
        return d.error();
    }
    return tbl;
#else
    (void)table_name;
    return util::Error{5004, 0,
                       "mariadb backend requires OPENADS_WITH_MARIADB=ON",
                       ""};
#endif
}

util::Result<void> MariaConnection::goto_top(MariaTable* tbl) {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mariadb goto_top", ""};
    }
    if (tbl->pk_snapshot.empty()) {
        tbl->positioned    = false;
        tbl->row_valid     = false;
        tbl->current_recno = 0;
        tbl->pos           = 0;
        return util::Result<void>{};
    }
    tbl->pos           = 0;
    tbl->positioned    = true;
    tbl->current_recno = 1;
    return load_current_row(impl_->conn, tbl);
#else
    (void)tbl;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

util::Result<void> MariaConnection::goto_bottom(MariaTable* tbl) {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mariadb goto_bottom", ""};
    }
    if (tbl->pk_snapshot.empty()) {
        tbl->positioned    = false;
        tbl->row_valid     = false;
        tbl->current_recno = 0;
        tbl->pos           = 0;
        return util::Result<void>{};
    }
    tbl->pos           = tbl->pk_snapshot.size() - 1;
    tbl->positioned    = true;
    tbl->current_recno = static_cast<std::uint32_t>(tbl->pos + 1);
    return load_current_row(impl_->conn, tbl);
#else
    (void)tbl;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

util::Result<void> MariaConnection::skip(MariaTable* tbl, std::int32_t step) {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mariadb skip", ""};
    }
    if (step == 0) return util::Result<void>{};
    if (tbl->pk_snapshot.empty()) {
        tbl->positioned = false;
        tbl->row_valid  = false;
        tbl->pos        = 0;
        return util::Result<void>{};
    }

    std::int64_t next = 0;
    if (!tbl->positioned) {
        if (tbl->pos == 0) {
            if (step > 0) next = step - 1;
            else return util::Error{5026, 0, "bof", ""};
        } else {
            if (step < 0) {
                next = static_cast<std::int64_t>(tbl->pos) + step;
            } else {
                return util::Result<void>{};
            }
        }
    } else {
        next = static_cast<std::int64_t>(tbl->pos) + step;
    }

    if (next < 0) {
        tbl->positioned = false;
        tbl->row_valid  = false;
        tbl->pos        = 0;
        return util::Error{5026, 0, "bof", ""};
    }
    if (static_cast<std::size_t>(next) >= tbl->pk_snapshot.size()) {
        tbl->positioned = false;
        tbl->row_valid  = false;
        tbl->pos        = tbl->pk_snapshot.size();
        return util::Result<void>{};
    }

    tbl->pos           = static_cast<std::size_t>(next);
    tbl->positioned    = true;
    tbl->current_recno = static_cast<std::uint32_t>(tbl->pos + 1);
    return load_current_row(impl_->conn, tbl);
#else
    (void)tbl;
    (void)step;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

util::Result<bool> MariaConnection::at_eof(MariaTable* tbl) const {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mariadb at_eof", ""};
    }
    if (tbl->pk_snapshot.empty()) return true;
    if (!tbl->positioned && tbl->pos >= tbl->pk_snapshot.size()) return true;
    return false;
#else
    (void)tbl;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

util::Result<bool> MariaConnection::at_bof(MariaTable* tbl) const {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mariadb at_bof", ""};
    }
    if (tbl->pk_snapshot.empty()) return true;
    return !tbl->positioned && tbl->pos == 0;
#else
    (void)tbl;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

util::Result<std::uint32_t> MariaConnection::record_count(MariaTable* tbl) {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mariadb record_count", ""};
    }
    if (tbl->rec_count_cached) return tbl->cached_rec_count;
    tbl->cached_rec_count = static_cast<std::uint32_t>(tbl->pk_snapshot.size());
    tbl->rec_count_cached = true;
    return tbl->cached_rec_count;
#else
    (void)tbl;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

util::Result<std::vector<MariaTable::FieldDesc>>
MariaConnection::describe_table(MariaTable* tbl) {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mariadb describe_table", ""};
    }
    if (tbl->fields_cached) return tbl->fields;
    return describe_table_impl(impl_->conn, tbl);
#else
    (void)tbl;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

util::Result<void> MariaConnection::read_field(
    MariaTable* tbl, const std::string& field_name,
    std::string& buf, bool& is_null) const {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mariadb read_field", ""};
    }
    if (!tbl->fields_cached) {
        return util::Error{5001, 0, "schema not cached", ""};
    }
    if (!tbl->row_valid) {
        return util::Error{5026, 0, "no current record", ""};
    }

    const std::size_t idx = field_index_ci(*tbl, field_name);
    if (idx == static_cast<std::size_t>(-1)) {
        return util::Error{5063, 0, "column not found", field_name};
    }
    if (idx >= tbl->current_row.size()) {
        return util::Error{5001, 0, "row cache mismatch", ""};
    }
    is_null = tbl->current_nulls[idx];
    buf     = tbl->current_row[idx];
    return util::Result<void>{};
#else
    (void)tbl;
    (void)field_name;
    (void)buf;
    (void)is_null;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

util::Result<bool> MariaConnection::seek_index(
    MariaTable* tbl, const std::string& column, const std::string& key,
    bool soft, bool last_key) {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mariadb seek", ""};
    }
    if (!is_safe_identifier(column)) {
        return util::Error{5001, 0, "invalid seek column", column};
    }
    if (!tbl->fields_cached) {
        if (auto d = describe_table_impl(impl_->conn, tbl); !d) return d.error();
    }
    if (field_index_ci(*tbl, column) == static_cast<std::size_t>(-1)) {
        return util::Error{5063, 0, "seek column not found", column};
    }

    const std::string pk_cols = pk_select_list(*tbl);
    const std::string esc_key = escape_literal(impl_->conn, key);
    const std::string qcol    = quote_ident(column);

    std::string sql;
    if (last_key) {
        sql = soft
            ? "SELECT " + pk_cols + " FROM " + quote_ident(tbl->name) +
              " WHERE " + qcol + " <= " + esc_key +
              " ORDER BY " + qcol + " DESC LIMIT 1"
            : "SELECT " + pk_cols + " FROM " + quote_ident(tbl->name) +
              " WHERE " + qcol + " = " + esc_key +
              " ORDER BY " + qcol + " DESC LIMIT 1";
    } else {
        sql = soft
            ? "SELECT " + pk_cols + " FROM " + quote_ident(tbl->name) +
              " WHERE " + qcol + " >= " + esc_key +
              " ORDER BY " + qcol + " ASC LIMIT 1"
            : "SELECT " + pk_cols + " FROM " + quote_ident(tbl->name) +
              " WHERE " + qcol + " = " + esc_key + " LIMIT 1";
    }

    if (mysql_query(impl_->conn, sql.c_str()) != 0) {
        return maria_error("seek", mysql_error(impl_->conn));
    }
    MYSQL_RES* res = mysql_store_result(impl_->conn);
    if (res == nullptr) {
        return maria_error("seek", mysql_error(impl_->conn));
    }

    bool found = false;
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row != nullptr) {
        MariaTable::PkRow pk;
        const unsigned int pk_cols_n =
            static_cast<unsigned int>(tbl->pk_columns.size());
        pk.values.resize(pk_cols_n);
        for (unsigned int c = 0; c < pk_cols_n; ++c) {
            bool is_null = false;
            pk.values[c] = format_maria_value(row, c, is_null);
            if (is_null) pk.values[c].clear();
        }
        mysql_free_result(res);
        if (auto p = position_at_pk(impl_->conn, tbl, pk); !p) {
            return p.error();
        }
        found = tbl->positioned && tbl->row_valid;
        tbl->last_seek_found = found;
    } else {
        mysql_free_result(res);
        tbl->positioned      = false;
        tbl->row_valid       = false;
        found                = false;
        tbl->last_seek_found = false;
    }
    return found;
#else
    (void)tbl;
    (void)column;
    (void)key;
    (void)soft;
    (void)last_key;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

} // namespace openads::sql_backend