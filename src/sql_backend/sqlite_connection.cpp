#include "sql_backend/sqlite_connection.h"

#include "sql_backend/sqlite_backend.h"

#include <algorithm>
#include <cctype>

#if defined(OPENADS_WITH_SQLITE)
#include <sqlite3.h>
#endif

namespace openads::sql_backend {

namespace {

#if defined(OPENADS_WITH_SQLITE)

util::Result<void> load_current_row(sqlite3* db, SqliteTable* tbl);

util::Result<std::vector<SqliteTable::FieldDesc>>
describe_table_impl(sqlite3* db, SqliteTable* tbl) {
    if (!is_safe_identifier(tbl->name)) {
        return util::Error{5001, 0, "invalid table name", tbl->name};
    }
    const std::string sql =
        "PRAGMA table_info(\"" + tbl->name + "\")";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(),
                           static_cast<int>(sql.size()),
                           &stmt, nullptr) != SQLITE_OK) {
        return sqlite_error(db, "pragma table_info");
    }

    std::vector<SqliteTable::FieldDesc> out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 1));
        const char* type = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 2));
        const bool notnull = sqlite3_column_int(stmt, 3) != 0;
        out.push_back(map_sqlite_column(name, type, notnull));
    }
    sqlite3_finalize(stmt);

    if (out.empty()) {
        return util::Error{5001, 0, "table not found or has no columns",
                           tbl->name};
    }
    tbl->fields        = out;
    tbl->fields_cached = true;
    return out;
}

util::Result<void> position_at_rowid(sqlite3* db, SqliteTable* tbl,
                                     std::int64_t rowid) {
    if (tbl == nullptr || db == nullptr) {
        return util::Error{5001, 0, "invalid sqlite table state", ""};
    }
    // rowids is built ascending in open_table, so binary-search it
    // (O(log N)) rather than scan linearly.
    auto it = std::lower_bound(tbl->rowids.begin(), tbl->rowids.end(), rowid);
    if (it != tbl->rowids.end() && *it == rowid) {
        tbl->pos           = static_cast<std::size_t>(
            std::distance(tbl->rowids.begin(), it));
        tbl->positioned    = true;
        tbl->current_rowid = rowid;
        return load_current_row(db, tbl);
    }
    tbl->positioned = false;
    tbl->row_valid  = false;
    return util::Result<void>{};
}

util::Result<void> load_current_row(sqlite3* db, SqliteTable* tbl) {
    if (tbl == nullptr || db == nullptr) {
        return util::Error{5001, 0, "invalid sqlite table state", ""};
    }
    if (!tbl->positioned) {
        tbl->row_valid      = false;
        tbl->current_row.clear();
        tbl->current_nulls.clear();
        return util::Result<void>{};
    }
    if (tbl->is_result) {
        // Materialized result cursor: serve the row from memory, no query.
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
        auto d = describe_table_impl(db, tbl);
        if (!d) return d.error();
    }

    const std::string sql =
        "SELECT * FROM \"" + tbl->name + "\" WHERE rowid=?1";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(),
                           static_cast<int>(sql.size()),
                           &stmt, nullptr) != SQLITE_OK) {
        return sqlite_error(db, "prepare row fetch");
    }
    sqlite3_bind_int64(stmt, 1, tbl->current_rowid);

    tbl->current_row.clear();
    tbl->current_nulls.clear();
    tbl->row_valid = false;

    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const int cols = sqlite3_column_count(stmt);
        tbl->current_row.resize(static_cast<std::size_t>(cols));
        tbl->current_nulls.resize(static_cast<std::size_t>(cols));
        for (int c = 0; c < cols; ++c) {
            bool is_null = false;
            auto ci = static_cast<std::size_t>(c);
            tbl->current_row[ci] = format_sqlite_value(stmt, c, is_null);
            tbl->current_nulls[ci] = is_null;
        }
        tbl->row_valid = true;
    } else if (rc == SQLITE_DONE) {
        tbl->positioned = false;
    } else {
        sqlite3_finalize(stmt);
        return sqlite_error(db, "step row fetch");
    }
    sqlite3_finalize(stmt);
    return util::Result<void>{};
}

#endif // OPENADS_WITH_SQLITE

} // namespace

struct SqliteConnection::Impl {
#if defined(OPENADS_WITH_SQLITE)
    sqlite3* db = nullptr;
#endif
};

SqliteConnection::SqliteConnection() = default;

SqliteConnection::~SqliteConnection() { disconnect(); }

SqliteConnection::SqliteConnection(SqliteConnection&& other) noexcept
    : impl_(std::move(other.impl_)), db_path_(std::move(other.db_path_)) {}

SqliteConnection& SqliteConnection::operator=(SqliteConnection&& other) noexcept {
    if (this != &other) {
        disconnect();
        impl_    = std::move(other.impl_);
        db_path_ = std::move(other.db_path_);
    }
    return *this;
}

util::Result<SqliteConnection> SqliteConnection::open(const SqliteUri& uri) {
#if defined(OPENADS_WITH_SQLITE)
    SqliteConnection conn;
    conn.db_path_ = uri.path;
    conn.impl_    = std::make_unique<Impl>();

    sqlite3* raw = nullptr;
    // SQL passthrough (AdsExecuteSQLDirect) adds DDL/DML, so the backend opens
    // read-write and creates the file on first use — a Harbour application can
    // CREATE its own database via SQL.
    const int rc = sqlite3_open_v2(
        uri.path.c_str(), &raw,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        util::Error e = sqlite_error(raw, "sqlite3_open");
        if (raw) sqlite3_close(raw);
        return e;
    }
    if (!uri.cipher_key.empty()) {
        if (auto ck = apply_cipher_key(raw, uri.cipher_key); !ck) {
            sqlite3_close(raw);
            return ck.error();
        }
    }
    // Concurrency hardening. Without a busy timeout, any contended write
    // returns SQLITE_BUSY ("database is locked") immediately, so a workload
    // spread over several connections sheds a large fraction of its writes.
    // A bounded busy timeout turns that hard failure into a short wait, which
    // is the behaviour a multi-user ADS application expects. WAL additionally
    // lets readers run concurrently with a writer; it is a best-effort
    // optimisation (it cannot be enabled on :memory: or read-only media), so a
    // failure to switch journal mode is left non-fatal — the busy timeout is
    // the part that matters for correctness under contention.
    sqlite3_busy_timeout(raw, 5000);
    sqlite3_exec(raw, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    conn.impl_->db = raw;
    return conn;
#else
    (void)uri;
    return util::Error{5004, 0,
                       "sqlite backend requires OPENADS_WITH_SQLITE=ON", ""};
#endif
}

void SqliteConnection::disconnect() noexcept {
#if defined(OPENADS_WITH_SQLITE)
    if (impl_ && impl_->db) {
        // close_v2 tolerates outstanding prepared statements (zombie close)
        // instead of failing with SQLITE_BUSY and leaking the handle.
        sqlite3_close_v2(impl_->db);
        impl_->db = nullptr;
    }
#endif
    impl_.reset();
}

bool SqliteConnection::valid() const noexcept {
#if defined(OPENADS_WITH_SQLITE)
    return impl_ && impl_->db != nullptr;
#else
    return false;
#endif
}

namespace {

// Load (or reload) the rowid navigation list for `tbl`, honouring any
// `tbl->where_filter` push-down predicate. The WHERE fragment is trusted
// (produced by engine::try_emit_sql_where, which constrains operators,
// escapes string literals and only emits known functions), so it is spliced
// into the SELECT verbatim — mirroring how the table name is here.
util::Result<void> load_rowids(sqlite3* db, SqliteTable* tbl) {
    std::string sql = "SELECT rowid FROM \"" + tbl->name + "\"";
    if (!tbl->where_filter.empty()) {
        sql += " WHERE (" + tbl->where_filter + ")";
    }
    sql += " ORDER BY rowid";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), static_cast<int>(sql.size()),
                           &stmt, nullptr) != SQLITE_OK) {
        return sqlite_error(db, "prepare rowid list");
    }
    tbl->rowids.clear();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        tbl->rowids.push_back(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);

    tbl->cached_rec_count = static_cast<std::uint32_t>(tbl->rowids.size());
    tbl->rec_count_cached = true;
    tbl->positioned       = false;
    tbl->row_valid        = false;
    tbl->current_rowid    = 0;
    tbl->current_deleted  = false;
    tbl->pos              = 0;
    return util::Result<void>{};
}

}  // namespace

util::Result<std::unique_ptr<SqliteTable>>
SqliteConnection::open_table(const std::string& table_name) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid()) {
        return util::Error{5001, 0, "sqlite connection not open", ""};
    }
    if (!is_safe_identifier(table_name)) {
        return util::Error{5001, 0, "invalid table name", table_name};
    }

    auto tbl = std::make_unique<SqliteTable>();
    tbl->conn = this;
    tbl->name = table_name;

    if (auto r = load_rowids(impl_->db, tbl.get()); !r) {
        return r.error();
    }

    if (auto d = describe_table_impl(impl_->db, tbl.get()); !d) {
        return d.error();
    }

    return tbl;
#else
    (void)table_name;
    return util::Error{5004, 0,
                       "sqlite backend requires OPENADS_WITH_SQLITE=ON", ""};
#endif
}

util::Result<void>
SqliteConnection::set_filter(SqliteTable* tbl, const std::string& where) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid sqlite set_filter", ""};
    }
    tbl->where_filter = where;
    return load_rowids(impl_->db, tbl);   // reload the (filtered) rowid list
#else
    (void)tbl; (void)where;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

util::Result<void> SqliteConnection::goto_top(SqliteTable* tbl) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid sqlite goto_top", ""};
    }
    if (tbl->rowids.empty()) {
        tbl->positioned    = false;
        tbl->row_valid     = false;
        tbl->current_rowid = 0;
        tbl->pos           = 0;
        return util::Result<void>{};
    }
    tbl->pos           = 0;
    tbl->positioned    = true;
    tbl->current_rowid = tbl->rowids[0];
    return load_current_row(impl_->db, tbl);
#else
    (void)tbl;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

util::Result<void> SqliteConnection::goto_bottom(SqliteTable* tbl) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid sqlite goto_bottom", ""};
    }
    if (tbl->rowids.empty()) {
        tbl->positioned    = false;
        tbl->row_valid     = false;
        tbl->current_rowid = 0;
        tbl->pos           = 0;
        return util::Result<void>{};
    }
    tbl->pos           = tbl->rowids.size() - 1;
    tbl->positioned    = true;
    tbl->current_rowid = tbl->rowids[tbl->pos];
    return load_current_row(impl_->db, tbl);
#else
    (void)tbl;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

util::Result<void> SqliteConnection::skip(SqliteTable* tbl, std::int32_t step) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid sqlite skip", ""};
    }
    if (step == 0) return util::Result<void>{};
    if (tbl->rowids.empty()) {
        tbl->positioned = false;
        tbl->row_valid  = false;
        tbl->pos        = 0;
        return util::Result<void>{};
    }

    std::int64_t next = 0;
    if (!tbl->positioned) {
        // Not on a row: pos == 0 is BOF, pos == size is EOF.
        if (tbl->pos == 0) {
            if (step > 0) {
                next = step - 1;            // skip forward from BOF
            } else {
                return util::Error{5026, 0, "bof", ""};
            }
        } else {
            if (step < 0) {
                next = static_cast<std::int64_t>(tbl->pos) + step;  // back from EOF
            } else {
                return util::Result<void>{};  // stay at EOF
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
    if (static_cast<std::size_t>(next) >= tbl->rowids.size()) {
        tbl->positioned = false;
        tbl->row_valid  = false;
        tbl->pos        = tbl->rowids.size();
        return util::Result<void>{};
    }

    tbl->pos           = static_cast<std::size_t>(next);
    tbl->positioned    = true;
    tbl->current_rowid = tbl->rowids[tbl->pos];
    return load_current_row(impl_->db, tbl);
#else
    (void)tbl;
    (void)step;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

util::Result<bool> SqliteConnection::at_eof(SqliteTable* tbl) const {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid sqlite at_eof", ""};
    }
    if (tbl->rowids.empty()) return true;
    if (!tbl->positioned && tbl->pos >= tbl->rowids.size()) return true;
    return false;
#else
    (void)tbl;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

util::Result<bool> SqliteConnection::at_bof(SqliteTable* tbl) const {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid sqlite at_bof", ""};
    }
    if (tbl->rowids.empty()) return true;
    return !tbl->positioned && tbl->pos == 0;
#else
    (void)tbl;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

util::Result<std::uint32_t> SqliteConnection::record_count(SqliteTable* tbl) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid sqlite record_count", ""};
    }
    if (tbl->rec_count_cached) return tbl->cached_rec_count;
    tbl->cached_rec_count = static_cast<std::uint32_t>(tbl->rowids.size());
    tbl->rec_count_cached = true;
    return tbl->cached_rec_count;
#else
    (void)tbl;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

util::Result<std::vector<SqliteTable::FieldDesc>>
SqliteConnection::describe_table(SqliteTable* tbl) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid sqlite describe_table", ""};
    }
    if (tbl->fields_cached) return tbl->fields;
    return describe_table_impl(impl_->db, tbl);
#else
    (void)tbl;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

util::Result<void> SqliteConnection::read_field(SqliteTable* tbl,
                                                const std::string& field_name,
                                                std::string& buf,
                                                bool& is_null) const {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid sqlite read_field", ""};
    }
    if (!tbl->fields_cached) {
        return util::Error{5001, 0, "schema not cached", ""};
    }
    if (!tbl->row_valid) {
        return util::Error{5026, 0, "no current record", ""};
    }

    std::string want = field_name;
    for (auto& c : want) {
        c = static_cast<char>(
            std::toupper(static_cast<unsigned char>(c)));
    }

    for (std::size_t i = 0; i < tbl->fields.size(); ++i) {
        std::string have = tbl->fields[i].name;
        for (auto& c : have) {
            c = static_cast<char>(
                std::toupper(static_cast<unsigned char>(c)));
        }
        if (have == want) {
            if (i >= tbl->current_row.size()) {
                return util::Error{5001, 0, "row cache mismatch", ""};
            }
            is_null = tbl->current_nulls[i];
            buf     = tbl->current_row[i];
            return util::Result<void>{};
        }
    }
    return util::Error{5063, 0, "column not found", field_name};
#else
    (void)tbl;
    (void)field_name;
    (void)buf;
    (void)is_null;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

util::Result<bool> SqliteConnection::seek_index(SqliteTable* tbl,
                                                const std::string& column,
                                                const std::string& key,
                                                bool soft,
                                                bool last_key) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid sqlite seek", ""};
    }
    if (!is_safe_identifier(column)) {
        return util::Error{5001, 0, "invalid seek column", column};
    }
    if (!tbl->fields_cached) {
        if (auto d = describe_table_impl(impl_->db, tbl); !d) return d.error();
    }
    if (field_index_ci(*tbl, column) == static_cast<std::size_t>(-1)) {
        return util::Error{5063, 0, "seek column not found", column};
    }

    std::string sql;
    if (last_key) {
        sql = soft
            ? "SELECT rowid FROM \"" + tbl->name + "\" WHERE \"" + column +
              "\" <= ?1 ORDER BY \"" + column + "\" DESC LIMIT 1"
            : "SELECT rowid FROM \"" + tbl->name + "\" WHERE \"" + column +
              "\" = ?1 ORDER BY \"" + column + "\" DESC LIMIT 1";
    } else {
        sql = soft
            ? "SELECT rowid FROM \"" + tbl->name + "\" WHERE \"" + column +
              "\" >= ?1 ORDER BY \"" + column + "\" ASC LIMIT 1"
            : "SELECT rowid FROM \"" + tbl->name + "\" WHERE \"" + column +
              "\" = ?1 LIMIT 1";
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(impl_->db, sql.c_str(),
                           static_cast<int>(sql.size()),
                           &stmt, nullptr) != SQLITE_OK) {
        return sqlite_error(impl_->db, "prepare seek");
    }
    sqlite3_bind_text(stmt, 1, key.c_str(),
                      static_cast<int>(key.size()), SQLITE_STATIC);

    bool found = false;
    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const std::int64_t rowid = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
        if (auto p = position_at_rowid(impl_->db, tbl, rowid); !p) {
            return p.error();
        }
        found = tbl->positioned && tbl->row_valid;
        tbl->last_seek_found = found;
    } else if (rc == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        tbl->positioned      = false;
        tbl->row_valid       = false;
        found                = false;
        tbl->last_seek_found = false;
    } else {
        sqlite3_finalize(stmt);
        return sqlite_error(impl_->db, "step seek");
    }
    return found;
#else
    (void)tbl;
    (void)column;
    (void)key;
    (void)soft;
    (void)last_key;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

util::Result<std::unique_ptr<SqliteTable>>
SqliteConnection::run_sql(const std::string& sql) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid()) return util::Error{5001, 0, "sqlite connection not open", ""};
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(impl_->db, sql.c_str(),
                           static_cast<int>(sql.size()),
                           &stmt, nullptr) != SQLITE_OK) {
        return sqlite_error(impl_->db, "prepare sql");
    }

    const int cols = sqlite3_column_count(stmt);
    if (cols == 0) {
        // Non-result statement (INSERT/UPDATE/DELETE/DDL): run to completion
        // and report no cursor — SQLite already told us there are no columns.
        int rc;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) { /* no rows */ }
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) return sqlite_error(impl_->db, "exec sql");
        return std::unique_ptr<SqliteTable>{};
    }

    // Result-producing statement: materialize into a navigable cursor.
    auto tbl = std::make_unique<SqliteTable>();
    tbl->conn      = this;
    tbl->name      = "(result)";
    tbl->is_result = true;
    tbl->fields.reserve(static_cast<std::size_t>(cols));
    for (int c = 0; c < cols; ++c) {
        const char* cn = sqlite3_column_name(stmt, c);
        const char* dt = sqlite3_column_decltype(stmt, c);
        tbl->fields.push_back(map_sqlite_column(cn ? cn : "", dt, false));
    }
    tbl->fields_cached = true;

    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        std::vector<std::string> row(static_cast<std::size_t>(cols));
        std::vector<bool>        nul(static_cast<std::size_t>(cols));
        for (int c = 0; c < cols; ++c) {
            bool is_null = false;
            row[static_cast<std::size_t>(c)] =
                format_sqlite_value(stmt, c, is_null);
            nul[static_cast<std::size_t>(c)] = is_null;
        }
        tbl->result_rows.push_back(std::move(row));
        tbl->result_nulls.push_back(std::move(nul));
    }
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return sqlite_error(impl_->db, "step query");
    }
    sqlite3_finalize(stmt);

    tbl->cached_rec_count = static_cast<std::uint32_t>(tbl->result_rows.size());
    tbl->rec_count_cached = true;
    // Synthetic rowid slots so record_count + nav bounds reuse the shared path.
    tbl->rowids.resize(tbl->result_rows.size());
    tbl->positioned = false;
    tbl->pos        = 0;
    return tbl;
#else
    (void)sql;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

#if defined(OPENADS_WITH_SQLITE)
namespace {
std::string quote_ident_sqlite(const std::string& name) {
    std::string out = "\"";
    for (char c : name) { if (c == '"') out += '"'; out += c; }
    out += '"';
    return out;
}
}  // namespace
#endif

util::Result<void> SqliteConnection::append_blank(SqliteTable* tbl) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid sqlite append", ""};
    }
    if (!tbl->fields_cached) {
        if (auto d = describe_table_impl(impl_->db, tbl); !d) return d.error();
    }
    tbl->staging_row.assign(tbl->fields.size(), std::string{});
    tbl->staging_nulls.assign(tbl->fields.size(), true);
    tbl->pending_append = true;
    tbl->row_dirty      = true;
    tbl->row_valid      = true;
    tbl->positioned     = true;
    return util::Result<void>{};
#else
    (void)tbl;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

util::Result<void> SqliteConnection::set_field(
    SqliteTable* tbl, const std::string& field_name, const std::string& value) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid sqlite set_field", ""};
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
    (void)tbl; (void)field_name; (void)value;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

util::Result<void> SqliteConnection::flush_record(SqliteTable* tbl) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid sqlite flush", ""};
    }
    if (!tbl->row_dirty && !tbl->pending_append) return util::Result<void>{};
    if (!tbl->fields_cached) return util::Error{5001, 0, "schema not cached", ""};
    sqlite3* db = impl_->db;

    if (tbl->pending_append) {
        std::string cols, marks;
        std::vector<std::size_t> bound;
        for (std::size_t i = 0; i < tbl->fields.size(); ++i) {
            if (i < tbl->staging_nulls.size() && tbl->staging_nulls[i]) continue;
            if (!bound.empty()) { cols += ", "; marks += ", "; }
            cols += quote_ident_sqlite(tbl->fields[i].name);
            marks += "?";
            bound.push_back(i);
        }
        if (bound.empty()) {
            return util::Error{5001, 0, "insert has no columns", tbl->name};
        }
        const std::string sql = "INSERT INTO " + quote_ident_sqlite(tbl->name) +
                                " (" + cols + ") VALUES (" + marks + ")";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), static_cast<int>(sql.size()),
                               &stmt, nullptr) != SQLITE_OK) {
            return sqlite_error(db, "prepare insert");
        }
        for (std::size_t k = 0; k < bound.size(); ++k) {
            const std::string& v = tbl->staging_row[bound[k]];
            sqlite3_bind_text(stmt, static_cast<int>(k + 1), v.c_str(),
                              static_cast<int>(v.size()), SQLITE_TRANSIENT);
        }
        const int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) return sqlite_error(db, "exec insert");

        const std::int64_t new_rowid = sqlite3_last_insert_rowid(db);
        tbl->pending_append = false;
        tbl->row_dirty      = false;
        if (auto r = load_rowids(db, tbl); !r) return r.error();
        return position_at_rowid(db, tbl, new_rowid);
    }

    // UPDATE the current row, keyed by its rowid.
    if (!tbl->positioned || tbl->pos >= tbl->rowids.size()) {
        return util::Error{5026, 0, "no current record", ""};
    }
    const std::int64_t rowid = tbl->rowids[tbl->pos];
    std::string set_clause;
    std::vector<std::size_t> bound;
    for (std::size_t i = 0; i < tbl->fields.size(); ++i) {
        if (i >= tbl->staging_row.size()) continue;
        if (!bound.empty()) set_clause += ", ";
        set_clause += quote_ident_sqlite(tbl->fields[i].name) + " = ?";
        bound.push_back(i);
    }
    if (bound.empty()) { tbl->row_dirty = false; return util::Result<void>{}; }
    const std::string sql = "UPDATE " + quote_ident_sqlite(tbl->name) +
                            " SET " + set_clause + " WHERE rowid = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), static_cast<int>(sql.size()),
                           &stmt, nullptr) != SQLITE_OK) {
        return sqlite_error(db, "prepare update");
    }
    int p = 1;
    for (std::size_t i : bound) {
        if (i < tbl->staging_nulls.size() && tbl->staging_nulls[i]) {
            sqlite3_bind_null(stmt, p++);
        } else {
            const std::string& v = tbl->staging_row[i];
            sqlite3_bind_text(stmt, p++, v.c_str(),
                              static_cast<int>(v.size()), SQLITE_TRANSIENT);
        }
    }
    sqlite3_bind_int64(stmt, p, rowid);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return sqlite_error(db, "exec update");

    tbl->row_dirty = false;
    return position_at_rowid(db, tbl, rowid);
#else
    (void)tbl;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

util::Result<void> SqliteConnection::delete_record(SqliteTable* tbl) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid sqlite delete", ""};
    }
    if (tbl->pending_append || !tbl->positioned ||
        tbl->pos >= tbl->rowids.size()) {
        return util::Error{5026, 0, "no current record", ""};
    }
    sqlite3* db = impl_->db;
    const std::int64_t rowid = tbl->rowids[tbl->pos];
    const std::string sql = "DELETE FROM " + quote_ident_sqlite(tbl->name) +
                            " WHERE rowid = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), static_cast<int>(sql.size()),
                           &stmt, nullptr) != SQLITE_OK) {
        return sqlite_error(db, "prepare delete");
    }
    sqlite3_bind_int64(stmt, 1, rowid);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return sqlite_error(db, "exec delete");

    tbl->row_dirty      = false;
    tbl->pending_append = false;
    return load_rowids(db, tbl);
#else
    (void)tbl;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

} // namespace openads::sql_backend