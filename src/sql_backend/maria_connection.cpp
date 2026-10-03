#include "sql_backend/maria_connection.h"
#include "engine/sql_execution_budget.h"

#include "sql_backend/backend_aggregate.h"
#include "sql_backend/maria_backend.h"
#include "sql_backend/sql_acl_store.h"
#include "sql_backend/sql_common.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

#if defined(OPENADS_WITH_MARIADB)
#include <mysql.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <sys/select.h>
#endif
#endif

namespace openads::sql_backend {

namespace {

#if defined(OPENADS_WITH_MARIADB)

// The continuation API avoids query/fetch socket waits. Failure shuts down
// the socket before cleanup, so neither COM_QUIT nor unread-row drain waits.
util::Result<std::unique_ptr<MariaTable>> remote_query(
    MYSQL*& conn, MariaConnection* owner, const std::string& sql, bool materialize) {
    MYSQL_RES* res = nullptr;
    auto fail = [&](std::int32_t code, const std::string& message)
        -> util::Result<std::unique_ptr<MariaTable>> {
        const auto socket = mysql_get_socket(conn);
#ifdef _WIN32
        if (socket != INVALID_SOCKET) shutdown(socket, SD_BOTH);
#else
        if (socket >= 0) shutdown(socket, SHUT_RDWR);
#endif
        if (res) {
            res->handle = nullptr; // Never drain a failed unbuffered result.
            mysql_free_result(res);
            res = nullptr;
        }
        mysql_close(conn);
        conn = nullptr;
        return util::Error{code, 0, message, "remote maria connection closed"};
    };
    if (!engine::sql_execution_step())
        return fail(7079, "SQL execution budget exceeded");
    if (sql.size() > std::numeric_limits<unsigned long>::max())
        return fail(7079, "SQL query length exceeded");
    if (mysql_options(conn, MYSQL_OPT_NONBLOCK, nullptr) != 0)
        return fail(5001, mysql_error(conn));
    // Check readiness, never fabricate read/write events for continuations.
    auto wait = [&](int status) {
        const auto timeout_at = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(mysql_get_timeout_value_ms(conn));
        for (;;) {
            if (!engine::sql_execution_step()) return -1;
            const auto socket = mysql_get_socket(conn);
#ifdef _WIN32
            if (socket == INVALID_SOCKET) return -2;
#else
            if (socket < 0 || socket >= FD_SETSIZE) return -2;
#endif
            fd_set read, write, except;
            FD_ZERO(&read); FD_ZERO(&write); FD_ZERO(&except);
            if (status & MYSQL_WAIT_READ) FD_SET(socket, &read);
            if (status & MYSQL_WAIT_WRITE) FD_SET(socket, &write);
            if (status & MYSQL_WAIT_EXCEPT) FD_SET(socket, &except);
            timeval timeout{0, 1000};
#ifdef _WIN32
            const int ready = select(0, &read, &write, &except, &timeout);
#else
            const int ready = select(socket + 1, &read, &write, &except, &timeout);
#endif
            if (ready < 0) return -2;
            int events = 0;
            if (FD_ISSET(socket, &read)) events |= MYSQL_WAIT_READ;
            if (FD_ISSET(socket, &write)) events |= MYSQL_WAIT_WRITE;
            if (FD_ISSET(socket, &except)) events |= MYSQL_WAIT_EXCEPT;
            if ((status & MYSQL_WAIT_TIMEOUT) &&
                std::chrono::steady_clock::now() >= timeout_at)
                events |= MYSQL_WAIT_TIMEOUT;
            if (events) return events;
        }
    };
    int error = 0;
    int status = mysql_real_query_start(&error, conn, sql.data(),
                                        static_cast<unsigned long>(sql.size()));
    while (status) {
        const int ready = wait(status);
        if (ready < 0) return fail(ready == -1 ? 7079 : 5001,
                                  "SQL query wait failed or budget exceeded");
        status = mysql_real_query_cont(&error, conn, ready);
    }
    if (error) return fail(5001, mysql_error(conn));
    res = mysql_use_result(conn);
    if (!res) {
        if (mysql_field_count(conn) != 0) return fail(5001, mysql_error(conn));
        return std::unique_ptr<MariaTable>{};
    }
    constexpr std::uint64_t max_bytes = 64ULL * 1024ULL * 1024ULL;
    std::uint64_t bytes = 0, rows = 0;
    auto account = [&](std::uint64_t amount) {
        if (amount > max_bytes - bytes) return false;
        bytes += amount;
        return true;
    };
    const unsigned int cols = mysql_num_fields(res);
    auto* fields = mysql_fetch_fields(res);
    std::unique_ptr<MariaTable> tbl;
    if (materialize) {
        tbl = std::make_unique<MariaTable>();
        tbl->conn = owner; tbl->name = "(result)"; tbl->is_result = true;
        for (unsigned int c = 0; c < cols; ++c) {
            if (!account(sizeof(MariaTable::FieldDesc) + fields[c].name_length))
                return fail(7079, "SQL result byte budget exceeded");
            tbl->fields.push_back(map_maria_column(fields[c].name, "varchar", true, 0, 0, 0));
        }
        tbl->fields_cached = true;
    }
    for (;;) {
        if (!engine::sql_execution_step()) return fail(7079, "SQL execution budget exceeded");
        MYSQL_ROW row = nullptr;
        status = mysql_fetch_row_start(&row, res);
        while (status) {
            const int ready = wait(status);
            if (ready < 0) return fail(ready == -1 ? 7079 : 5001,
                                      "SQL fetch wait failed or budget exceeded");
            status = mysql_fetch_row_cont(&row, res, ready);
        }
        if (!row) break;
        if (++rows > 100000) return fail(7079, "SQL result row budget exceeded");
        const auto* lengths = mysql_fetch_lengths(res);
        if (!lengths || !account(2 * sizeof(std::vector<std::string>) +
                                 static_cast<std::uint64_t>(cols) * (sizeof(std::string) + 1)))
            return fail(7079, "SQL result byte budget exceeded");
        for (unsigned int c = 0; c < cols; ++c)
            if (!account(lengths[c])) return fail(7079, "SQL result byte budget exceeded");
        if (tbl) {
            std::vector<std::string> values(cols);
            std::vector<bool> nulls(cols);
            for (unsigned int c = 0; c < cols; ++c) {
                nulls[c] = row[c] == nullptr;
                if (row[c]) values[c].assign(row[c], lengths[c]);
            }
            tbl->result_rows.push_back(std::move(values));
            tbl->result_nulls.push_back(std::move(nulls));
        }
    }
    if (mysql_errno(conn)) return fail(5001, mysql_error(conn));
    mysql_free_result(res); res = nullptr; // EOF reached: no blocking drain.
    if (mysql_more_results(conn)) return fail(5001, "multiple Maria results unsupported");
    if (tbl) {
        tbl->cached_rec_count = static_cast<std::uint32_t>(tbl->result_rows.size());
        tbl->rec_count_cached = true; tbl->positioned = false;
        tbl->pos = 0; tbl->row_valid = false;
    }
    return tbl;
}

util::Result<void> reload_pk_snapshot(MYSQL* conn, MariaTable* tbl);

std::string quote_ident(const std::string& name) {
    std::string out = "`";
    for (char c : name) {
        if (c == '`') out += "``";
        else          out += c;
    }
    out += '`';
    return out;
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
    const auto rows = static_cast<std::size_t>(mysql_num_rows(res));
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
    if (tbl->is_result) {
        if (tbl->pos < tbl->result_rows.size()) {
            tbl->current_row   = tbl->result_rows[tbl->pos];
            tbl->current_nulls = tbl->result_nulls[tbl->pos];
            tbl->row_valid     = true;
        } else {
            tbl->current_row.clear();
            tbl->current_nulls.clear();
            tbl->row_valid = false;
        }
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
    (void)mysql_query(raw, acl_table_ddl(SqlDdlDialect::Maria).c_str());
    (void)mysql_query(raw, member_table_ddl(SqlDdlDialect::Maria).c_str());
    (void)mysql_query(raw, user_table_ddl(SqlDdlDialect::Maria).c_str());
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
    if (tbl->is_result) {
        if (tbl->result_rows.empty()) {
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
    if (tbl->is_result) {
        if (tbl->result_rows.empty()) {
            tbl->positioned    = false;
            tbl->row_valid     = false;
            tbl->current_recno = 0;
            tbl->pos           = 0;
            return util::Result<void>{};
        }
        tbl->pos           = tbl->result_rows.size() - 1;
        tbl->positioned    = true;
        tbl->current_recno = static_cast<std::uint32_t>(tbl->pos + 1);
        return load_current_row(impl_->conn, tbl);
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
    if (tbl->is_result) {
        const auto n = tbl->result_rows.size();
        if (n == 0) {
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
            } else if (step < 0) {
                next = static_cast<std::int64_t>(tbl->pos) + step;
            } else {
                return util::Result<void>{};
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
        if (static_cast<std::size_t>(next) >= n) {
            tbl->positioned = false;
            tbl->row_valid  = false;
            tbl->pos        = n;
            return util::Result<void>{};
        }
        tbl->pos           = static_cast<std::size_t>(next);
        tbl->positioned    = true;
        tbl->current_recno = static_cast<std::uint32_t>(tbl->pos + 1);
        return load_current_row(impl_->conn, tbl);
    }
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

util::Result<void>
MariaConnection::set_filter(MariaTable* tbl, const std::string& where) {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid maria set_filter", ""};
    }
    tbl->where_builder.aof_filter = where;
    tbl->where_filter = tbl->where_builder.build();
    return reload_pk_snapshot(impl_->conn, tbl);
#else
    (void)tbl; (void)where;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

util::Result<void>
MariaConnection::refresh_where_filter(MariaTable* tbl) {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid maria refresh_where_filter", ""};
    }
    tbl->where_filter = tbl->where_builder.build();
    return reload_pk_snapshot(impl_->conn, tbl);
#else
    (void)tbl;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

util::Result<std::vector<engine::AggValue>>
MariaConnection::aggregate(MariaTable* tbl,
                           const std::string& where_sql,
                           const std::vector<engine::AggSpec>& specs) {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid maria aggregate", ""};
    }
    if (!tbl->fields_cached) {
        if (auto d = describe_table_impl(impl_->conn, tbl); !d) return d.error();
    }
    std::vector<AggregateFieldDesc> fields;
    fields.reserve(tbl->fields.size());
    for (const auto& f : tbl->fields) {
        fields.push_back({f.name, f.type});
    }
    std::string bad;
    const std::string sql = build_aggregate_sql(
        quote_ident(tbl->name), where_sql, specs, fields,
        [](const std::string& n) { return '`' + n + '`'; }, &bad);
    if (sql.empty()) {
        return util::Error{5001, 0,
                           "invalid maria aggregate field: " + bad, ""};
    }
    if (mysql_query(impl_->conn, sql.c_str()) != 0) {
        return maria_error("aggregate", mysql_error(impl_->conn));
    }
    MYSQL_RES* res = mysql_store_result(impl_->conn);
    if (res == nullptr) return maria_error("aggregate", mysql_error(impl_->conn));
    std::vector<std::string> vals;
    vals.resize(specs.size());
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row) {
        for (std::size_t i = 0; i < specs.size(); ++i) {
            if (row[i] == nullptr) vals[i].clear();
            else vals[i] = row[i];
        }
    }
    mysql_free_result(res);
    return parse_aggregate_row(specs, fields, vals, row != nullptr);
#else
    (void)tbl; (void)where_sql; (void)specs;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

util::Result<bool> MariaConnection::at_eof(MariaTable* tbl) const {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mariadb at_eof", ""};
    }
    if (tbl->is_result) {
        if (tbl->result_rows.empty()) return true;
        if (!tbl->positioned && tbl->pos >= tbl->result_rows.size()) return true;
        return false;
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
    if (tbl->is_result) {
        if (tbl->result_rows.empty()) return true;
        return !tbl->positioned && tbl->pos == 0;
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
    if (tbl->is_result) {
        return static_cast<std::uint32_t>(tbl->result_rows.size());
    }
    if (tbl->rec_count_cached) return tbl->cached_rec_count;
    if (auto r = reload_pk_snapshot(impl_->conn, tbl); !r) return r.error();
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

#if defined(OPENADS_WITH_MARIADB)
namespace {

// Re-run the PK snapshot SELECT (same shape as open_table) so RECCOUNT and row
// ordering reflect a just-committed INSERT/DELETE (autocommit is on).
util::Result<void> reload_pk_snapshot(MYSQL* conn, MariaTable* tbl) {
    std::string sql = "SELECT " + pk_select_list(*tbl) + " FROM " +
                      quote_ident(tbl->name);
    if (!tbl->where_filter.empty()) {
        sql += " WHERE (" + tbl->where_filter + ")";
    }
    sql += " ORDER BY " + pk_order_by(*tbl);
    if (mysql_query(conn, sql.c_str()) != 0) {
        return maria_error("pk snapshot", mysql_error(conn));
    }
    MYSQL_RES* res = mysql_store_result(conn);
    if (res == nullptr) return maria_error("pk snapshot", mysql_error(conn));
    const unsigned int pk_cols =
        static_cast<unsigned int>(tbl->pk_columns.size());
    tbl->pk_snapshot.clear();
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
    return util::Result<void>{};
}

} // namespace
#endif

util::Result<void> MariaConnection::append_blank(MariaTable* tbl) {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid maria append", ""};
    }
    if (!tbl->fields_cached) {
        if (auto d = describe_table_impl(impl_->conn, tbl); !d) return d.error();
    }
    tbl->staging_row.assign(tbl->fields.size(), std::string{});
    tbl->staging_nulls.assign(tbl->fields.size(), true);
    tbl->pending_append = true;
    tbl->row_dirty      = true;
    tbl->row_valid      = true;
    tbl->positioned     = true;
    tbl->current_recno  = 0;
    return util::Result<void>{};
#else
    (void)tbl;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

util::Result<void> MariaConnection::set_field(
    MariaTable* tbl, const std::string& field_name, const std::string& value) {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid maria set_field", ""};
    }
    if (!tbl->row_valid && !tbl->pending_append) {
        return util::Error{5026, 0, "no current record", ""};
    }
    if (!tbl->fields_cached) return util::Error{5001, 0, "schema not cached", ""};
    const std::size_t idx = field_index_ci(*tbl, field_name);
    if (idx == static_cast<std::size_t>(-1)) {
        return util::Error{5063, 0, "column not found", field_name};
    }
    if (!tbl->row_dirty && !tbl->pending_append) {
        tbl->staging_row   = tbl->current_row;
        tbl->staging_nulls = tbl->current_nulls;
    }
    if (tbl->staging_row.size() < tbl->fields.size()) {
        tbl->staging_row.resize(tbl->fields.size());
        tbl->staging_nulls.resize(tbl->fields.size(), true);
    }
    tbl->staging_row[idx]   = value;
    tbl->staging_nulls[idx] = false;
    tbl->row_dirty          = true;
    return util::Result<void>{};
#else
    (void)tbl;
    (void)field_name;
    (void)value;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

util::Result<void> MariaConnection::flush_record(MariaTable* tbl) {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid maria flush", ""};
    }
    if (!tbl->row_dirty && !tbl->pending_append) return util::Result<void>{};
    if (!tbl->fields_cached) return util::Error{5001, 0, "schema not cached", ""};
    MYSQL* conn = impl_->conn;

    auto lit = [&](std::size_t i) -> std::string {
        if (i >= tbl->staging_nulls.size() || tbl->staging_nulls[i]) return "NULL";
        return escape_literal(conn, tbl->staging_row[i]);
    };

    std::vector<bool> is_pk(tbl->fields.size(), false);
    for (const std::string& pkc : tbl->pk_columns) {
        const std::size_t fi = field_index_ci(*tbl, pkc);
        if (fi != static_cast<std::size_t>(-1)) is_pk[fi] = true;
    }

    if (tbl->pending_append) {
        std::string cols, vals;
        bool any = false;
        for (std::size_t i = 0; i < tbl->fields.size(); ++i) {
            if (i < tbl->staging_nulls.size() && tbl->staging_nulls[i]) continue;
            if (any) { cols += ", "; vals += ", "; }
            cols += quote_ident(tbl->fields[i].name);
            vals += lit(i);
            any = true;
        }
        if (!any) return util::Error{5001, 0, "insert has no columns", tbl->name};
        const std::string sql = "INSERT INTO " + quote_ident(tbl->name) +
                                " (" + cols + ") VALUES (" + vals + ")";
        if (mysql_query(conn, sql.c_str()) != 0) {
            return maria_error("insert", mysql_error(conn));
        }
        MariaTable::PkRow pk;
        pk.values.resize(tbl->pk_columns.size());
        bool pk_ready = true;
        for (std::size_t i = 0; i < tbl->pk_columns.size(); ++i) {
            const std::size_t fi = field_index_ci(*tbl, tbl->pk_columns[i]);
            if (fi == static_cast<std::size_t>(-1) ||
                (fi < tbl->staging_nulls.size() && tbl->staging_nulls[fi])) {
                pk_ready = false;
                break;
            }
            pk.values[i] = tbl->staging_row[fi];
        }
        tbl->pending_append = false;
        tbl->row_dirty      = false;
        if (auto s = reload_pk_snapshot(conn, tbl); !s) return s.error();
        if (pk_ready) {
            for (std::size_t i = 0; i < tbl->pk_snapshot.size(); ++i) {
                if (tbl->pk_snapshot[i].values == pk.values) {
                    tbl->pos        = i;
                    tbl->positioned = true;
                    return load_current_row(conn, tbl);
                }
            }
        }
        return util::Result<void>{};
    }

    if (tbl->pos >= tbl->pk_snapshot.size()) {
        return util::Error{5026, 0, "no current record", ""};
    }
    const MariaTable::PkRow current_pk = tbl->pk_snapshot[tbl->pos];
    std::string set_clause;
    bool any = false;
    for (std::size_t i = 0; i < tbl->fields.size(); ++i) {
        if (is_pk[i]) continue;
        if (i >= tbl->staging_row.size()) continue;
        if (any) set_clause += ", ";
        set_clause += quote_ident(tbl->fields[i].name) + " = " + lit(i);
        any = true;
    }
    if (!any) { tbl->row_dirty = false; return util::Result<void>{}; }
    const std::string sql = "UPDATE " + quote_ident(tbl->name) + " SET " +
                            set_clause + " WHERE " +
                            pk_where_clause(conn, *tbl, current_pk);
    if (mysql_query(conn, sql.c_str()) != 0) {
        return maria_error("update", mysql_error(conn));
    }
    tbl->row_dirty = false;
    return load_current_row(conn, tbl);
#else
    (void)tbl;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

util::Result<void> MariaConnection::delete_record(MariaTable* tbl) {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid maria delete", ""};
    }
    if (tbl->pending_append || tbl->pos >= tbl->pk_snapshot.size() ||
        !tbl->positioned) {
        return util::Error{5026, 0, "no current record", ""};
    }
    MYSQL* conn = impl_->conn;
    const std::string sql = "DELETE FROM " + quote_ident(tbl->name) +
                            " WHERE " +
                            pk_where_clause(conn, *tbl, tbl->pk_snapshot[tbl->pos]);
    if (mysql_query(conn, sql.c_str()) != 0) {
        return maria_error("delete", mysql_error(conn));
    }
    tbl->positioned     = false;
    tbl->row_valid      = false;
    tbl->row_dirty      = false;
    tbl->pending_append = false;
    return reload_pk_snapshot(conn, tbl);
#else
    (void)tbl;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

#if defined(OPENADS_WITH_MARIADB)
namespace {

std::string lock_record_key(const MariaTable& tbl, std::size_t pos) {
    std::string k = "R\x1f" + tbl.name;
    if (pos < tbl.pk_snapshot.size()) {
        for (const std::string& v : tbl.pk_snapshot[pos].values) k += "\x1f" + v;
    }
    return k;
}

// Runs SELECT fn(MD5('key')[, 0]) and returns its integer result (1 = got the
// lock / released). MD5 keeps the name within MariaDB's 64-char lock-name limit.
util::Result<long long> lock_call(MYSQL* conn, const std::string& expr) {
    if (mysql_query(conn, expr.c_str()) != 0) {
        return maria_error("named lock", mysql_error(conn));
    }
    MYSQL_RES* res = mysql_store_result(conn);
    if (res == nullptr) return maria_error("named lock", mysql_error(conn));
    long long val = 0;
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row != nullptr && row[0] != nullptr) val = std::atoll(row[0]);
    mysql_free_result(res);
    return val;
}

} // namespace
#endif

util::Result<void> MariaConnection::lock_record(MariaTable* tbl,
                                                std::uint32_t recno) {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid maria lock", ""};
    }
    const std::size_t pos =
        (recno == 0) ? tbl->pos : static_cast<std::size_t>(recno - 1);
    if (pos >= tbl->pk_snapshot.size()) {
        return util::Error{5026, 0, "no current record", ""};
    }
    const std::string sql = "SELECT GET_LOCK(MD5(" +
        escape_literal(impl_->conn, lock_record_key(*tbl, pos)) + "), 0)";
    auto r = lock_call(impl_->conn, sql);
    if (!r) return r.error();
    if (r.value() != 1) return util::Error{5035, 0, "record locked", ""};
    return util::Result<void>{};
#else
    (void)tbl; (void)recno;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

util::Result<void> MariaConnection::unlock_record(MariaTable* tbl,
                                                  std::uint32_t recno) {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid maria unlock", ""};
    }
    const std::size_t pos =
        (recno == 0) ? tbl->pos : static_cast<std::size_t>(recno - 1);
    if (pos >= tbl->pk_snapshot.size()) return util::Result<void>{};
    const std::string sql = "SELECT RELEASE_LOCK(MD5(" +
        escape_literal(impl_->conn, lock_record_key(*tbl, pos)) + "))";
    auto r = lock_call(impl_->conn, sql);
    if (!r) return r.error();
    return util::Result<void>{};
#else
    (void)tbl; (void)recno;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

util::Result<void> MariaConnection::lock_table(MariaTable* tbl) {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid maria lock", ""};
    }
    const std::string sql = "SELECT GET_LOCK(MD5(" +
        escape_literal(impl_->conn, "T\x1f" + tbl->name) + "), 0)";
    auto r = lock_call(impl_->conn, sql);
    if (!r) return r.error();
    if (r.value() != 1) return util::Error{5035, 0, "table locked", ""};
    return util::Result<void>{};
#else
    (void)tbl;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

util::Result<void> MariaConnection::unlock_table(MariaTable* tbl) {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid maria unlock", ""};
    }
    const std::string sql = "SELECT RELEASE_LOCK(MD5(" +
        escape_literal(impl_->conn, "T\x1f" + tbl->name) + "))";
    auto r = lock_call(impl_->conn, sql);
    if (!r) return r.error();
    return util::Result<void>{};
#else
    (void)tbl;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

util::Result<void> MariaConnection::exec_sql(const std::string& sql) {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid()) {
        return util::Error{5001, 0, "mariadb connection not open", ""};
    }
    if (engine::sql_execution_budget.active) {
        auto result = remote_query(impl_->conn, this, sql, false);
        if (!result) return result.error();
        return util::Result<void>{};
    }
    if (mysql_query(impl_->conn, sql.c_str()) != 0) {
        return maria_error("exec_sql", mysql_error(impl_->conn));
    }
    return util::Result<void>{};
#else
    (void)sql;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

util::Result<std::unique_ptr<MariaTable>>
MariaConnection::run_sql(const std::string& sql) {
#if defined(OPENADS_WITH_MARIADB)
    if (!valid()) {
        return util::Error{5001, 0, "mariadb connection not open", ""};
    }
    if (engine::sql_execution_budget.active)
        return remote_query(impl_->conn, this, sql, true);
    if (mysql_query(impl_->conn, sql.c_str()) != 0) {
        return maria_error("run_sql", mysql_error(impl_->conn));
    }
    MYSQL_RES* res = mysql_store_result(impl_->conn);
    if (res == nullptr) {
        if (mysql_field_count(impl_->conn) == 0) {
            return std::unique_ptr<MariaTable>{};
        }
        return maria_error("run_sql", mysql_error(impl_->conn));
    }
    auto tbl = std::make_unique<MariaTable>();
    tbl->conn      = this;
    tbl->name      = "(result)";
    tbl->is_result = true;
    const unsigned int cols = mysql_num_fields(res);
    MYSQL_FIELD* fields = mysql_fetch_fields(res);
    tbl->fields.reserve(cols);
    for (unsigned int c = 0; c < cols; ++c) {
        tbl->fields.push_back(map_maria_column(
            fields[c].name, "varchar", true, 0, 0, 0));
    }
    tbl->fields_cached = true;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        std::vector<std::string> rrow(cols);
        std::vector<bool>        rnul(cols);
        for (unsigned int c = 0; c < cols; ++c) {
            bool is_null = false;
            rrow[c] = format_maria_value(row, c, is_null);
            rnul[c] = is_null;
        }
        tbl->result_rows.push_back(std::move(rrow));
        tbl->result_nulls.push_back(std::move(rnul));
    }
    mysql_free_result(res);
    tbl->cached_rec_count = static_cast<std::uint32_t>(tbl->result_rows.size());
    tbl->rec_count_cached = true;
    tbl->positioned       = false;
    tbl->pos              = 0;
    tbl->row_valid        = false;
    return tbl;
#else
    (void)sql;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
#endif
}

} // namespace openads::sql_backend