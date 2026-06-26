#include "sql_backend/postgres_connection.h"

#include "sql_backend/postgres_backend.h"
#include "sql_backend/sql_common.h"

#include <algorithm>
#include <vector>

#if defined(OPENADS_WITH_POSTGRESQL)
#include <libpq-fe.h>
#endif

namespace openads::sql_backend {

namespace {

#if defined(OPENADS_WITH_POSTGRESQL)

std::string quote_ident(const std::string& name) {
    std::string out = "\"";
    for (char c : name) {
        if (c == '"') out += "\"\"";  // double any embedded quote
        else          out += c;
    }
    out += '"';
    return out;
}

std::string pk_select_list(const PostgresTable& tbl) {
    std::string out;
    for (std::size_t i = 0; i < tbl.pk_columns.size(); ++i) {
        if (i > 0) out += ", ";
        out += quote_ident(tbl.pk_columns[i]);
    }
    return out;
}

// primary-key columns with an explicit direction ("pk1" ASC, "pk2" ASC) — a
// deterministic tie-breaker so a seek never returns an arbitrary row on dup keys.
std::string pk_order_by(const PostgresTable& tbl, const char* dir) {
    std::string out;
    for (std::size_t i = 0; i < tbl.pk_columns.size(); ++i) {
        if (i > 0) out += ", ";
        out += quote_ident(tbl.pk_columns[i]) + ' ' + dir;
    }
    return out;
}

// "col1" = $1 AND "col2" = $2 ...  (placeholders bound positionally)
std::string pk_where_clause(const PostgresTable& tbl) {
    std::string sql;
    for (std::size_t i = 0; i < tbl.pk_columns.size(); ++i) {
        if (i > 0) sql += " AND ";
        sql += quote_ident(tbl.pk_columns[i]) + " = $" +
               std::to_string(i + 1);
    }
    return sql;
}

util::Result<void> load_current_row(PGconn* conn, PostgresTable* tbl);

util::Result<std::vector<std::string>>
discover_pk_columns(PGconn* conn, const std::string& table_name) {
    const char* params[1] = {table_name.c_str()};
    PGresult* res = PQexecParams(
        conn,
        "SELECT kcu.column_name "
        "FROM information_schema.table_constraints tc "
        "JOIN information_schema.key_column_usage kcu "
        "  ON kcu.constraint_schema = tc.constraint_schema "
        " AND kcu.constraint_name = tc.constraint_name "
        "WHERE tc.constraint_type = 'PRIMARY KEY' "
        "  AND tc.table_schema = ANY (current_schemas(true)) "
        "  AND tc.table_name = $1 "
        "ORDER BY kcu.ordinal_position",
        1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        const char* msg = PQerrorMessage(conn);
        PQclear(res);
        return postgres_error("pk discovery", msg);
    }
    const int rows = PQntuples(res);
    std::vector<std::string> cols;
    cols.reserve(static_cast<std::size_t>(rows));
    for (int r = 0; r < rows; ++r) {
        cols.emplace_back(PQgetvalue(res, r, 0));
    }
    PQclear(res);
    if (cols.empty()) {
        return util::Error{5001, 0, "table has no primary key", table_name};
    }
    return cols;
}

util::Result<std::vector<PostgresTable::FieldDesc>>
describe_table_impl(PGconn* conn, PostgresTable* tbl) {
    if (!is_safe_identifier(tbl->name)) {
        return util::Error{5001, 0, "invalid table name", tbl->name};
    }
    const char* params[1] = {tbl->name.c_str()};
    PGresult* res = PQexecParams(
        conn,
        "SELECT column_name, data_type, is_nullable, "
        "character_maximum_length, numeric_precision, numeric_scale, "
        "column_default "
        "FROM information_schema.columns "
        "WHERE table_schema = ANY (current_schemas(true)) "
        "AND table_name = $1 "
        "ORDER BY ordinal_position",
        1, nullptr, params, nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        const char* msg = PQerrorMessage(conn);
        PQclear(res);
        return postgres_error("describe_table", msg);
    }
    const int rows = PQntuples(res);
    if (rows <= 0) {
        PQclear(res);
        return util::Error{5001, 0, "table not found or has no columns",
                           tbl->name};
    }
    std::vector<PostgresTable::FieldDesc> out;
    out.reserve(static_cast<std::size_t>(rows));
    for (int r = 0; r < rows; ++r) {
        const char* nullable = PQgetvalue(res, r, 2);
        PostgresTable::FieldDesc fd = map_pg_column(
            PQgetvalue(res, r, 0),
            PQgetvalue(res, r, 1),
            nullable && nullable[0] == 'Y',
            std::atoi(PQgetvalue(res, r, 3)),
            std::atoi(PQgetvalue(res, r, 4)),
            std::atoi(PQgetvalue(res, r, 5)));
        fd.default_value = PQgetisnull(res, r, 6)
            ? std::string{} : std::string(PQgetvalue(res, r, 6));
        out.push_back(std::move(fd));
    }
    PQclear(res);
    tbl->fields        = out;
    tbl->fields_cached = true;
    return out;
}

util::Result<void> position_at_pk(PGconn* conn, PostgresTable* tbl,
                                  const PostgresTable::PkRow& pk) {
    if (tbl == nullptr || conn == nullptr) {
        return util::Error{5001, 0, "invalid postgres table state", ""};
    }
    auto it = std::find_if(
        tbl->pk_snapshot.begin(), tbl->pk_snapshot.end(),
        [&](const PostgresTable::PkRow& row) {
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

util::Result<void> load_current_row(PGconn* conn, PostgresTable* tbl) {
    if (tbl == nullptr || conn == nullptr) {
        return util::Error{5001, 0, "invalid postgres table state", ""};
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

    // Explicit column list in tbl->fields order (NOT "SELECT *"): keeps
    // current_row[i] aligned with fields[i] regardless of physical column order.
    std::string collist;
    for (std::size_t i = 0; i < tbl->fields.size(); ++i) {
        if (i > 0) collist += ", ";
        collist += quote_ident(tbl->fields[i].name);
    }
    const std::string sql =
        "SELECT " + collist + " FROM " + quote_ident(tbl->name) + " WHERE " +
        pk_where_clause(*tbl);
    const PostgresTable::PkRow& pk = tbl->pk_snapshot[tbl->pos];
    std::vector<const char*> params;
    params.reserve(pk.values.size());
    for (const std::string& v : pk.values) params.push_back(v.c_str());

    PGresult* res = PQexecParams(
        conn, sql.c_str(), static_cast<int>(params.size()), nullptr,
        params.data(), nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        const char* msg = PQerrorMessage(conn);
        PQclear(res);
        return postgres_error("load row", msg);
    }

    tbl->current_row.clear();
    tbl->current_nulls.clear();
    tbl->row_valid = false;

    if (PQntuples(res) == 1) {
        const int cols = PQnfields(res);
        tbl->current_row.resize(static_cast<std::size_t>(cols));
        tbl->current_nulls.resize(static_cast<std::size_t>(cols));
        for (int c = 0; c < cols; ++c) {
            bool is_null = false;
            tbl->current_row[c] = format_pg_value(res, 0, c, is_null);
            tbl->current_nulls[c] = is_null;
        }
        tbl->row_valid = true;
    } else {
        tbl->positioned = false;
    }
    PQclear(res);
    return util::Result<void>{};
}

#endif

} // namespace

struct PostgresConnection::Impl {
#if defined(OPENADS_WITH_POSTGRESQL)
    PGconn* conn = nullptr;
#endif
};

PostgresConnection::PostgresConnection() = default;
PostgresConnection::~PostgresConnection() { disconnect(); }

PostgresConnection::PostgresConnection(PostgresConnection&& other) noexcept
    : impl_(std::move(other.impl_)), conninfo_(std::move(other.conninfo_)) {}

PostgresConnection& PostgresConnection::operator=(
    PostgresConnection&& other) noexcept {
    if (this != &other) {
        disconnect();
        impl_      = std::move(other.impl_);
        conninfo_  = std::move(other.conninfo_);
    }
    return *this;
}

util::Result<PostgresConnection> PostgresConnection::open(
    const PostgresUri& uri) {
#if defined(OPENADS_WITH_POSTGRESQL)
    PostgresConnection conn;
    conn.conninfo_ = uri.conninfo;
    conn.impl_     = std::make_unique<Impl>();

    PGconn* raw = PQconnectdb(uri.conninfo.c_str());
    if (raw == nullptr) {
        return util::Error{5001, 0, "PQconnectdb failed", ""};
    }
    if (PQstatus(raw) != CONNECTION_OK) {
        util::Error e = postgres_error("connect", PQerrorMessage(raw));
        PQfinish(raw);
        return e;
    }
    conn.impl_->conn = raw;
    return std::move(conn);
#else
    (void)uri;
    return util::Error{5004, 0,
                       "postgresql backend requires "
                       "OPENADS_WITH_POSTGRESQL=ON",
                       ""};
#endif
}

void PostgresConnection::disconnect() noexcept {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (impl_ && impl_->conn) {
        PQfinish(impl_->conn);
        impl_->conn = nullptr;
    }
#endif
    impl_.reset();
}

bool PostgresConnection::valid() const noexcept {
#if defined(OPENADS_WITH_POSTGRESQL)
    return impl_ && impl_->conn != nullptr &&
           PQstatus(impl_->conn) == CONNECTION_OK;
#else
    return false;
#endif
}

#if defined(OPENADS_WITH_POSTGRESQL)
namespace {

// Load (or reload) the PK snapshot for `tbl`, honouring any `tbl->where_filter`
// push-down predicate. Requires tbl->pk_columns already discovered. The WHERE
// fragment is trusted (produced by engine::try_emit_sql_where, which constrains
// operators, escapes string literals and only emits known functions), so it is
// spliced into the SELECT verbatim — mirroring how the table name is here.
util::Result<void> load_pk_snapshot(PGconn* conn, PostgresTable* tbl) {
    const std::string sel = pk_select_list(*tbl);
    std::string sql = "SELECT " + sel + " FROM " + quote_ident(tbl->name);
    if (!tbl->where_filter.empty()) {
        sql += " WHERE (" + tbl->where_filter + ")";
    }
    sql += " ORDER BY " + sel;

    PGresult* res = PQexec(conn, sql.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        const char* msg = PQerrorMessage(conn);
        PQclear(res);
        return postgres_error("pk snapshot", msg);
    }
    const int rows    = PQntuples(res);
    const int pk_cols = PQnfields(res);
    tbl->pk_snapshot.clear();
    tbl->pk_snapshot.reserve(static_cast<std::size_t>(rows));
    for (int r = 0; r < rows; ++r) {
        PostgresTable::PkRow pk_row;
        pk_row.values.resize(static_cast<std::size_t>(pk_cols));
        for (int c = 0; c < pk_cols; ++c) {
            pk_row.values[static_cast<std::size_t>(c)] =
                PQgetisnull(res, r, c) ? std::string{}
                                       : PQgetvalue(res, r, c);
        }
        tbl->pk_snapshot.push_back(std::move(pk_row));
    }
    PQclear(res);

    tbl->cached_rec_count = static_cast<std::uint32_t>(tbl->pk_snapshot.size());
    tbl->rec_count_cached = true;
    tbl->positioned       = false;
    tbl->row_valid        = false;
    tbl->current_recno    = 0;
    tbl->current_deleted  = false;
    tbl->pos              = 0;
    return util::Result<void>{};
}

}  // namespace
#endif

util::Result<std::unique_ptr<PostgresTable>>
PostgresConnection::open_table(const std::string& table_name) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid()) {
        return util::Error{5001, 0, "postgres connection not open", ""};
    }
    if (!is_safe_identifier(table_name)) {
        return util::Error{5001, 0, "invalid table name", table_name};
    }

    auto tbl = std::make_unique<PostgresTable>();
    tbl->conn = this;
    tbl->name = table_name;

    auto pk = discover_pk_columns(impl_->conn, table_name);
    if (!pk) return pk.error();
    tbl->pk_columns = std::move(pk).value();

    if (auto r = load_pk_snapshot(impl_->conn, tbl.get()); !r) {
        return r.error();
    }

    if (auto d = describe_table_impl(impl_->conn, tbl.get()); !d) {
        return d.error();
    }
    return tbl;
#else
    (void)table_name;
    return util::Error{5004, 0,
                       "postgresql backend requires "
                       "OPENADS_WITH_POSTGRESQL=ON",
                       ""};
#endif
}

util::Result<void>
PostgresConnection::set_filter(PostgresTable* tbl, const std::string& where) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres set_filter", ""};
    }
    tbl->where_filter = where;
    return load_pk_snapshot(impl_->conn, tbl);   // reload the (filtered) snapshot
#else
    (void)tbl; (void)where;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<std::vector<engine::AggValue>>
PostgresConnection::aggregate(PostgresTable* tbl,
                              const std::string& where_sql,
                              const std::vector<engine::AggSpec>& specs) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr)
        return util::Error{5001, 0, "invalid postgres aggregate", ""};
    if (!tbl->fields_cached) {
        auto d = describe_table(tbl);
        if (!d) return d.error();
        tbl->fields        = std::move(d).value();
        tbl->fields_cached = true;
    }
    auto field_is_numeric = [&](const std::string& name) {
        for (const auto& f : tbl->fields) {
            if (f.name.size() != name.size()) continue;
            bool same = true;
            for (std::size_t i = 0; i < name.size(); ++i)
                if (std::toupper(static_cast<unsigned char>(f.name[i])) !=
                    std::toupper(static_cast<unsigned char>(name[i]))) {
                    same = false; break;
                }
            if (!same) continue;
            switch (f.type) {                       // ADS numeric type codes
                case 2: case 10: case 11: case 12:
                case 15: case 17: case 18:
                    return true;
                default:
                    return false;
            }
        }
        return false;
    };

    std::string sql = "SELECT ";
    for (std::size_t i = 0; i < specs.size(); ++i) {
        if (i) sql += ", ";
        const auto& s = specs[i];
        // Leave the column unquoted so PostgreSQL case-folds it (the ABI hands
        // field names in upper case, e.g. "QTY", but the column is "qty");
        // this matches how the translated WHERE refers to the same column.
        const std::string col = s.field;
        switch (s.fn) {
            case engine::AggFn::Count:
                sql += s.field.empty() ? "COUNT(*)" : ("COUNT(" + col + ")");
                break;
            // COALESCE(SUM(),0) gives 0 over zero rows (xBase SUM semantics).
            case engine::AggFn::Sum: sql += "COALESCE(SUM(" + col + "),0)"; break;
            case engine::AggFn::Avg: sql += "AVG(" + col + ")"; break;
            case engine::AggFn::Min: sql += "MIN(" + col + ")"; break;
            case engine::AggFn::Max: sql += "MAX(" + col + ")"; break;
        }
        sql += " AS a" + std::to_string(i);
    }
    sql += " FROM " + quote_ident(tbl->name);
    if (!where_sql.empty()) sql += " WHERE (" + where_sql + ")";

    PGresult* res = PQexec(impl_->conn, sql.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        util::Error e = postgres_error("aggregate", PQerrorMessage(impl_->conn));
        PQclear(res);
        return e;
    }
    const bool have_row = PQntuples(res) >= 1;
    std::vector<engine::AggValue> out;
    out.reserve(specs.size());
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const auto& s   = specs[i];
        const int    ci = static_cast<int>(i);
        const bool   is_null = !have_row || PQgetisnull(res, 0, ci);
        std::string  val =
            is_null ? std::string{} : std::string(PQgetvalue(res, 0, ci));
        const bool numeric_result =
            s.fn == engine::AggFn::Count || s.fn == engine::AggFn::Sum ||
            s.fn == engine::AggFn::Avg   || field_is_numeric(s.field);
        engine::AggValue av;
        if (is_null) {
            av.type = engine::AggType::Empty;
        } else if (numeric_result) {
            av.type  = engine::AggType::Numeric;
            av.bytes = engine::format_agg_double(std::strtod(val.c_str(), nullptr));
        } else {
            av.type  = engine::AggType::String;
            av.bytes = std::move(val);
        }
        out.push_back(std::move(av));
    }
    PQclear(res);
    return out;
#else
    (void)tbl; (void)where_sql; (void)specs;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<void> PostgresConnection::goto_top(PostgresTable* tbl) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres goto_top", ""};
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
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<void> PostgresConnection::goto_bottom(PostgresTable* tbl) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres goto_bottom", ""};
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
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<void> PostgresConnection::skip(PostgresTable* tbl,
                                            std::int32_t step) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres skip", ""};
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
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<bool> PostgresConnection::at_eof(PostgresTable* tbl) const {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres at_eof", ""};
    }
    if (tbl->pk_snapshot.empty()) return true;
    if (!tbl->positioned && tbl->pos >= tbl->pk_snapshot.size()) return true;
    return false;
#else
    (void)tbl;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<bool> PostgresConnection::at_bof(PostgresTable* tbl) const {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres at_bof", ""};
    }
    if (tbl->pk_snapshot.empty()) return true;
    return !tbl->positioned && tbl->pos == 0;
#else
    (void)tbl;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<std::uint32_t> PostgresConnection::record_count(PostgresTable* tbl) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres record_count", ""};
    }
    if (tbl->rec_count_cached) return tbl->cached_rec_count;
    tbl->cached_rec_count = static_cast<std::uint32_t>(tbl->pk_snapshot.size());
    tbl->rec_count_cached = true;
    return tbl->cached_rec_count;
#else
    (void)tbl;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<std::vector<PostgresTable::FieldDesc>>
PostgresConnection::describe_table(PostgresTable* tbl) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres describe_table", ""};
    }
    if (tbl->fields_cached) return tbl->fields;
    return describe_table_impl(impl_->conn, tbl);
#else
    (void)tbl;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<void> PostgresConnection::read_field(
    PostgresTable* tbl, const std::string& field_name,
    std::string& buf, bool& is_null) const {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres read_field", ""};
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
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<bool> PostgresConnection::seek_index(
    PostgresTable* tbl, const std::string& column, const std::string& key,
    bool soft, bool last_key) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres seek", ""};
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

    const std::string sel  = pk_select_list(*tbl);
    const std::string qcol = quote_ident(column);
    const std::string from = " FROM " + quote_ident(tbl->name) + " WHERE ";

    const std::string pk_asc  = pk_order_by(*tbl, "ASC");
    const std::string pk_desc = pk_order_by(*tbl, "DESC");

    std::string sql;
    if (last_key) {
        sql = soft
            ? "SELECT " + sel + from + qcol + " <= $1 ORDER BY " + qcol +
              " DESC, " + pk_desc + " LIMIT 1"
            : "SELECT " + sel + from + qcol + " = $1 ORDER BY " + pk_desc +
              " LIMIT 1";
    } else {
        sql = soft
            ? "SELECT " + sel + from + qcol + " >= $1 ORDER BY " + qcol +
              " ASC, " + pk_asc + " LIMIT 1"
            : "SELECT " + sel + from + qcol + " = $1 ORDER BY " + pk_asc +
              " LIMIT 1";
    }

    const char* params[1] = {key.c_str()};
    PGresult* res = PQexecParams(impl_->conn, sql.c_str(), 1, nullptr, params,
                                 nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        const char* msg = PQerrorMessage(impl_->conn);
        PQclear(res);
        return postgres_error("seek", msg);
    }

    bool found = false;
    if (PQntuples(res) == 1) {
        const int pk_cols = PQnfields(res);
        PostgresTable::PkRow pk;
        pk.values.resize(static_cast<std::size_t>(pk_cols));
        for (int c = 0; c < pk_cols; ++c) {
            pk.values[static_cast<std::size_t>(c)] =
                PQgetisnull(res, 0, c) ? std::string{}
                                       : PQgetvalue(res, 0, c);
        }
        PQclear(res);
        if (auto p = position_at_pk(impl_->conn, tbl, pk); !p) {
            return p.error();
        }
        found = tbl->positioned && tbl->row_valid;
        tbl->last_seek_found = found;
    } else {
        PQclear(res);
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
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

#if defined(OPENADS_WITH_POSTGRESQL)
namespace {

// Re-run the PK snapshot SELECT (same shape as open_table) so RECCOUNT and row
// ordering reflect a just-committed INSERT/DELETE. libpq autocommits each
// statement, so the committed change is visible to this fresh SELECT.
util::Result<void> reload_pk_snapshot(PGconn* conn, PostgresTable* tbl) {
    const std::string sel = pk_select_list(*tbl);
    const std::string sql = "SELECT " + sel + " FROM " +
                            quote_ident(tbl->name) + " ORDER BY " + sel;
    PGresult* res = PQexec(conn, sql.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        const char* msg = PQerrorMessage(conn);
        PQclear(res);
        return postgres_error("pk snapshot", msg);
    }
    const int rows    = PQntuples(res);
    const int pk_cols = PQnfields(res);
    tbl->pk_snapshot.clear();
    tbl->pk_snapshot.reserve(static_cast<std::size_t>(rows));
    for (int r = 0; r < rows; ++r) {
        PostgresTable::PkRow pk_row;
        pk_row.values.resize(static_cast<std::size_t>(pk_cols));
        for (int c = 0; c < pk_cols; ++c) {
            pk_row.values[static_cast<std::size_t>(c)] =
                PQgetisnull(res, r, c) ? std::string{} : PQgetvalue(res, r, c);
        }
        tbl->pk_snapshot.push_back(std::move(pk_row));
    }
    PQclear(res);
    tbl->cached_rec_count = static_cast<std::uint32_t>(tbl->pk_snapshot.size());
    tbl->rec_count_cached = true;
    return util::Result<void>{};
}

} // namespace
#endif

util::Result<void> PostgresConnection::append_blank(PostgresTable* tbl) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres append", ""};
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
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<void> PostgresConnection::set_field(
    PostgresTable* tbl, const std::string& field_name,
    const std::string& value) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres set_field", ""};
    }
    if (!tbl->row_valid && !tbl->pending_append) {
        return util::Error{5026, 0, "no current record", ""};
    }
    if (!tbl->fields_cached) {
        return util::Error{5001, 0, "schema not cached", ""};
    }
    const std::size_t idx = field_index_ci(*tbl, field_name);
    if (idx == static_cast<std::size_t>(-1)) {
        return util::Error{5063, 0, "column not found", field_name};
    }
    // For an UPDATE (not an append) seed the staging buffer from the current
    // row so columns the caller leaves untouched keep their existing values.
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
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<void> PostgresConnection::flush_record(PostgresTable* tbl) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres flush", ""};
    }
    if (!tbl->row_dirty && !tbl->pending_append) return util::Result<void>{};
    if (!tbl->fields_cached) {
        return util::Error{5001, 0, "schema not cached", ""};
    }
    PGconn* conn = impl_->conn;

    // Which fields are part of the primary key (skipped in an UPDATE SET).
    std::vector<bool> is_pk(tbl->fields.size(), false);
    for (const std::string& pkc : tbl->pk_columns) {
        const std::size_t fi = field_index_ci(*tbl, pkc);
        if (fi != static_cast<std::size_t>(-1)) is_pk[fi] = true;
    }

    if (tbl->pending_append) {
        std::string cols, ph;
        std::vector<std::string> store;     // stable backing for c_str()
        std::vector<bool>        store_null;
        bool any = false;
        for (std::size_t i = 0; i < tbl->fields.size(); ++i) {
            if (i < tbl->staging_nulls.size() && tbl->staging_nulls[i]) continue;
            if (any) { cols += ", "; ph += ", "; }
            cols += quote_ident(tbl->fields[i].name);
            store.push_back(tbl->staging_row[i]);
            store_null.push_back(false);
            ph += "$" + std::to_string(store.size());
            any = true;
        }
        if (!any) return util::Error{5001, 0, "insert has no columns", tbl->name};
        const std::string sql = "INSERT INTO " + quote_ident(tbl->name) +
                                " (" + cols + ") VALUES (" + ph + ")";
        std::vector<const char*> params(store.size());
        for (std::size_t i = 0; i < store.size(); ++i) {
            params[i] = store_null[i] ? nullptr : store[i].c_str();
        }
        PGresult* res = PQexecParams(conn, sql.c_str(),
                                     static_cast<int>(params.size()), nullptr,
                                     params.data(), nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            const char* msg = PQerrorMessage(conn);
            PQclear(res);
            return postgres_error("insert", msg);
        }
        PQclear(res);

        // Capture this row's PK from staging so we can reposition on it.
        PostgresTable::PkRow pk;
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

    // UPDATE the positioned row by its PK.
    if (tbl->pos >= tbl->pk_snapshot.size()) {
        return util::Error{5026, 0, "no current record", ""};
    }
    const PostgresTable::PkRow current_pk = tbl->pk_snapshot[tbl->pos];
    std::string set_clause;
    std::vector<std::string> store;
    std::vector<bool>        store_null;
    bool any = false;
    for (std::size_t i = 0; i < tbl->fields.size(); ++i) {
        if (is_pk[i]) continue;
        if (i >= tbl->staging_row.size()) continue;
        if (any) set_clause += ", ";
        const bool isn = (i < tbl->staging_nulls.size()) && tbl->staging_nulls[i];
        store.push_back(isn ? std::string{} : tbl->staging_row[i]);
        store_null.push_back(isn);
        set_clause += quote_ident(tbl->fields[i].name) + " = $" +
                      std::to_string(store.size());
        any = true;
    }
    if (!any) { tbl->row_dirty = false; return util::Result<void>{}; }
    std::string sql = "UPDATE " + quote_ident(tbl->name) + " SET " +
                      set_clause + " WHERE ";
    for (std::size_t i = 0; i < tbl->pk_columns.size(); ++i) {
        if (i > 0) sql += " AND ";
        store.push_back(current_pk.values[i]);
        store_null.push_back(false);
        sql += quote_ident(tbl->pk_columns[i]) + " = $" +
               std::to_string(store.size());
    }
    std::vector<const char*> params(store.size());
    for (std::size_t i = 0; i < store.size(); ++i) {
        params[i] = store_null[i] ? nullptr : store[i].c_str();
    }
    PGresult* res = PQexecParams(conn, sql.c_str(),
                                 static_cast<int>(params.size()), nullptr,
                                 params.data(), nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        const char* msg = PQerrorMessage(conn);
        PQclear(res);
        return postgres_error("update", msg);
    }
    PQclear(res);
    tbl->row_dirty = false;
    return load_current_row(conn, tbl);
#else
    (void)tbl;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<void> PostgresConnection::delete_record(PostgresTable* tbl) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres delete", ""};
    }
    if (tbl->pending_append || tbl->pos >= tbl->pk_snapshot.size() ||
        !tbl->positioned) {
        return util::Error{5026, 0, "no current record", ""};
    }
    PGconn* conn = impl_->conn;
    const std::string sql = "DELETE FROM " + quote_ident(tbl->name) +
                            " WHERE " + pk_where_clause(*tbl);
    const PostgresTable::PkRow& pk = tbl->pk_snapshot[tbl->pos];
    std::vector<const char*> params;
    params.reserve(pk.values.size());
    for (const std::string& v : pk.values) params.push_back(v.c_str());
    PGresult* res = PQexecParams(conn, sql.c_str(),
                                 static_cast<int>(params.size()), nullptr,
                                 params.data(), nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        const char* msg = PQerrorMessage(conn);
        PQclear(res);
        return postgres_error("delete", msg);
    }
    PQclear(res);
    tbl->positioned     = false;
    tbl->row_valid      = false;
    tbl->row_dirty      = false;
    tbl->pending_append = false;
    return reload_pk_snapshot(conn, tbl);
#else
    (void)tbl;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

#if defined(OPENADS_WITH_POSTGRESQL)
namespace {

// Advisory-lock key: "R" + table + PK values for a record, "T" + table for the
// whole table. Distinct prefixes keep a table lock from colliding with a record
// lock that would otherwise hash to the same key.
std::string advisory_record_key(const PostgresTable& tbl, std::size_t pos) {
    std::string k = "R\x1f" + tbl.name;
    if (pos < tbl.pk_snapshot.size()) {
        for (const std::string& v : tbl.pk_snapshot[pos].values) k += "\x1f" + v;
    }
    return k;
}

// Runs SELECT pg_(try_)advisory_(un)lock(hashtextextended($1,0)); returns the
// boolean the function yields ('t'). For unlock the result is informational.
util::Result<bool> advisory_call(PGconn* conn, const char* fn,
                                 const std::string& key) {
    const std::string sql =
        std::string("SELECT ") + fn + "(hashtextextended($1, 0))";
    const char* params[1] = {key.c_str()};
    PGresult* res = PQexecParams(conn, sql.c_str(), 1, nullptr, params,
                                 nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        const char* msg = PQerrorMessage(conn);
        PQclear(res);
        return postgres_error("advisory lock", msg);
    }
    const bool got = (PQntuples(res) == 1 && PQgetvalue(res, 0, 0)[0] == 't');
    PQclear(res);
    return got;
}

} // namespace
#endif

util::Result<void> PostgresConnection::lock_record(PostgresTable* tbl,
                                                   std::uint32_t recno) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres lock", ""};
    }
    const std::size_t pos =
        (recno == 0) ? tbl->pos : static_cast<std::size_t>(recno - 1);
    if (pos >= tbl->pk_snapshot.size()) {
        return util::Error{5026, 0, "no current record", ""};
    }
    auto r = advisory_call(impl_->conn, "pg_try_advisory_lock",
                           advisory_record_key(*tbl, pos));
    if (!r) return r.error();
    if (!r.value()) return util::Error{5035, 0, "record locked", ""};
    return util::Result<void>{};
#else
    (void)tbl; (void)recno;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<void> PostgresConnection::unlock_record(PostgresTable* tbl,
                                                     std::uint32_t recno) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres unlock", ""};
    }
    const std::size_t pos =
        (recno == 0) ? tbl->pos : static_cast<std::size_t>(recno - 1);
    if (pos >= tbl->pk_snapshot.size()) return util::Result<void>{};
    auto r = advisory_call(impl_->conn, "pg_advisory_unlock",
                           advisory_record_key(*tbl, pos));
    if (!r) return r.error();
    return util::Result<void>{};
#else
    (void)tbl; (void)recno;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<void> PostgresConnection::lock_table(PostgresTable* tbl) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres lock", ""};
    }
    auto r = advisory_call(impl_->conn, "pg_try_advisory_lock",
                           "T\x1f" + tbl->name);
    if (!r) return r.error();
    if (!r.value()) return util::Error{5035, 0, "table locked", ""};
    return util::Result<void>{};
#else
    (void)tbl;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

util::Result<void> PostgresConnection::unlock_table(PostgresTable* tbl) {
#if defined(OPENADS_WITH_POSTGRESQL)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid postgres unlock", ""};
    }
    auto r = advisory_call(impl_->conn, "pg_advisory_unlock",
                           "T\x1f" + tbl->name);
    if (!r) return r.error();
    return util::Result<void>{};
#else
    (void)tbl;
    return util::Error{5004, 0, "postgresql backend disabled", ""};
#endif
}

} // namespace openads::sql_backend
