#include "sql_backend/sqlite_connection.h"
#include "engine/sql_execution_budget.h"

#include "openads/ace.h"
#include "sql_backend/sql_acl_store.h"
#include "sql_backend/sqlite_backend.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

#if defined(OPENADS_WITH_SQLITE)
#include <sqlite3.h>
#endif

namespace openads::sql_backend {

namespace {

#if defined(OPENADS_WITH_SQLITE)

util::Result<void> load_current_row(sqlite3* db, SqliteTable* tbl);

// Restore connection limits on every return. Callback interrupts SQLite VM
// work; it is not a promise to interrupt blocked filesystem/busy waits.
struct RemoteSqliteGuard {
    sqlite3* db;
    bool active;
    int previous_length = 0;
    explicit RemoteSqliteGuard(sqlite3* database) : db(database),
        active(engine::sql_execution_budget.active) {
        if (!active) return;
        previous_length = sqlite3_limit(db, SQLITE_LIMIT_LENGTH, -1);
        sqlite3_limit(db, SQLITE_LIMIT_LENGTH,
                      std::min(previous_length, 64 * 1024 * 1024));
        sqlite3_progress_handler(db, 1000, [](void*) -> int {
            return engine::sql_execution_step(1000) ? 0 : 1;
        }, nullptr);
    }
    ~RemoteSqliteGuard() {
        if (active) {
            sqlite3_progress_handler(db, 0, nullptr, nullptr);
            sqlite3_limit(db, SQLITE_LIMIT_LENGTH, previous_length);
        }
    }
};


util::Result<void> ensure_colmeta_table(sqlite3* db) {
    const char* ddl =
        "CREATE TABLE IF NOT EXISTS \"OPENADS$COLMETA\" ("
        "table_name TEXT NOT NULL,"
        "col_name TEXT NOT NULL,"
        "xbase_type TEXT NOT NULL,"
        "col_length INTEGER NOT NULL,"
        "col_dec INTEGER NOT NULL,"
        "PRIMARY KEY (table_name, col_name))";
    char* err = nullptr;
    if (sqlite3_exec(db, ddl, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "colmeta ddl failed";
        if (err) sqlite3_free(err);
        return util::Error{5001, 0, msg, ""};
    }
    return util::Result<void>{};
}

std::uint16_t ads_type_from_xbase(char t) {
    switch (t) {
        case 'L': return ADS_LOGICAL;
        case 'N': return ADS_DOUBLE;
        case 'M': return ADS_MEMO;
        case 'B': return ADS_BINARY;
        case 'A': return ADS_AUTOINC;
        case 'D': return ADS_DATE;
        case 'C':
        default:  return ADS_STRING;
    }
}

char xbase_type_from_decl(const std::string& decl_type) {
    std::string u = decl_type;
    for (auto& ch : u) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    if (u.find("INT") != std::string::npos) return 'A';
    if (u.find("REAL") != std::string::npos ||
        u.find("FLOA") != std::string::npos ||
        u.find("DOUB") != std::string::npos ||
        u.find("NUM") != std::string::npos) {
        return 'N';
    }
    if (u.find("BLOB") != std::string::npos) return 'B';
    return 'C';
}

std::string sqlite_decl_for_xbase(char xt) {
    switch (xt) {
        case 'L': return "INTEGER";
        case 'N': return "REAL";
        case 'A': return "INTEGER";
        case 'B':
        case 'M': return "BLOB";
        case 'D':
        case 'C':
        default:  return "TEXT";
    }
}

std::string sqlite_cast_expr(const std::string& col_name,
                             char old_type,
                             char new_type) {
    const std::string q = "\"" + col_name + "\"";
    if (old_type == new_type) return q;
    if (old_type == 'C' && new_type == 'N') {
        return "CAST(" + q + " AS REAL)";
    }
    if (old_type == 'N' && new_type == 'C') {
        return "CAST(" + q + " AS TEXT)";
    }
    if (old_type == 'C' && new_type == 'L') {
        return "CASE WHEN TRIM(" + q + ") IN ('T','t','Y','y','1') "
               "THEN 1 ELSE 0 END";
    }
    if (old_type == 'L' && new_type == 'C') {
        return "CASE WHEN " + q + " IN (1,'1','T','t') THEN 'T' ELSE 'F' END";
    }
    if (old_type == 'N' && new_type == 'L') {
        return "CASE WHEN CAST(" + q + " AS REAL) != 0 THEN 1 ELSE 0 END";
    }
    if (old_type == 'L' && new_type == 'N') {
        return "CAST(CASE WHEN " + q + " IN (1,'1','T','t') "
               "THEN 1 ELSE 0 END AS REAL)";
    }
    return q;
}

void apply_colmeta_overrides(sqlite3* db, const std::string& table_name,
                             std::vector<SqliteTable::FieldDesc>& fields) {
    if (!is_safe_identifier(table_name)) return;
    const std::string sql =
        "SELECT col_name, xbase_type, col_length, col_dec "
        "FROM \"OPENADS$COLMETA\" WHERE table_name = \"" + table_name + "\"";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(),
                           static_cast<int>(sql.size()),
                           &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    std::unordered_map<std::string, SqliteTable::FieldDesc> meta;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* cn = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 0));
        const char* xt = reinterpret_cast<const char*>(
            sqlite3_column_text(stmt, 1));
        if (!cn || !xt || xt[0] == '\0') continue;
        SqliteTable::FieldDesc fd;
        fd.name = cn;
        fd.type = ads_type_from_xbase(xt[0]);
        fd.length = static_cast<std::uint32_t>(
            sqlite3_column_int(stmt, 2));
        fd.decimals = static_cast<std::uint16_t>(
            sqlite3_column_int(stmt, 3));
        meta[fd.name] = fd;
    }
    sqlite3_finalize(stmt);
    for (auto& f : fields) {
        auto it = meta.find(f.name);
        if (it == meta.end()) continue;
        f.type     = it->second.type;
        f.length   = it->second.length;
        f.decimals = it->second.decimals;
    }
}

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
    apply_colmeta_overrides(db, tbl->name, out);
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

    // Use the field optimizer's select fragment (may be "*" or a column list).
    const std::string sel = tbl->field_optimizer.select_fragment();
    const std::string sql =
        "SELECT " + sel + " FROM " + quote_ident_sqlite(tbl->name) +
        " WHERE rowid=?1";
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
    (void)ensure_colmeta_table(raw);
    (void)sqlite3_exec(raw,
                      acl_table_ddl(SqlDdlDialect::Sqlite).c_str(),
                      nullptr, nullptr, nullptr);
    (void)sqlite3_exec(raw,
                      member_table_ddl(SqlDdlDialect::Sqlite).c_str(),
                      nullptr, nullptr, nullptr);
    (void)sqlite3_exec(raw,
                      user_table_ddl(SqlDdlDialect::Sqlite).c_str(),
                      nullptr, nullptr, nullptr);
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
    std::string sql = "SELECT rowid FROM " + quote_ident_sqlite(tbl->name);
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
    // Store the raw WHERE in the where_builder's aof_filter slot and
    // produce the composed WHERE. The caller may later set additional
    // restrictors (scope, for clause, etc.) on the builder before
    // calling set_filter again.
    tbl->where_builder.aof_filter = where;
    tbl->where_filter = tbl->where_builder.build();
    return load_rowids(impl_->db, tbl);   // reload the (filtered) rowid list
#else
    (void)tbl; (void)where;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

util::Result<void>
SqliteConnection::refresh_where_filter(SqliteTable* tbl) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid sqlite refresh_where_filter", ""};
    }
    tbl->where_filter = tbl->where_builder.build();
    return load_rowids(impl_->db, tbl);
#else
    (void)tbl;
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
    if (tbl->is_result) {
        tbl->cached_rec_count =
            static_cast<std::uint32_t>(tbl->result_rows.size());
        tbl->rec_count_cached = true;
        return tbl->cached_rec_count;
    }
    if (auto r = load_rowids(impl_->db, tbl); !r) return r.error();
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
            // Track column access for the field optimizer (learning mode).
            tbl->field_optimizer.note_column_read(field_name);
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

util::Result<void>
SqliteConnection::exec_sql(const std::string& sql) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid()) return util::Error{5001, 0, "sqlite connection not open", ""};
    RemoteSqliteGuard execution_guard(impl_->db);
    if (!engine::sql_execution_step())
        return util::Error{7079, 0, "SQLite execution budget exceeded", ""};
    char* err = nullptr;
    const int rc = sqlite3_exec(impl_->db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "exec failed";
        sqlite3_free(err);
        return util::Error{5001, 0, msg, sql};
    }
    return util::Result<void>{};
#else
    (void)sql;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

util::Result<std::unique_ptr<SqliteTable>>
SqliteConnection::run_sql(const std::string& sql) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid()) return util::Error{5001, 0, "sqlite connection not open", ""};
    RemoteSqliteGuard execution_guard(impl_->db);
    if (!engine::sql_execution_step())
        return util::Error{7079, 0, "SQLite execution budget exceeded", ""};
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

    std::uint64_t result_bytes = 0;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (execution_guard.active &&
            (tbl->result_rows.size() >= 100000 || !engine::sql_execution_step())) {
            sqlite3_finalize(stmt);
            return util::Error{7079, 0, "SQLite result row/work budget exceeded", ""};
        }
        // Bound vector/cell overhead and raw cell bytes before copying.
        std::uint64_t row_bytes = std::uint64_t{64} + static_cast<std::uint64_t>(cols) * std::uint64_t{64};
        for (int c = 0; c < cols; ++c) {
            const int length = sqlite3_column_bytes(stmt, c);
            row_bytes += static_cast<std::uint64_t>(length) + std::uint64_t{32};
        }
        if (execution_guard.active && row_bytes > 64ull * 1024ull * 1024ull - result_bytes) {
            sqlite3_finalize(stmt);
            return util::Error{7079, 0, "SQLite result memory budget exceeded", ""};
        }
        if (execution_guard.active) result_bytes += row_bytes;
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

util::Result<void> SqliteConnection::append_blank(SqliteTable* tbl) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid sqlite append", ""};
    }
    if (tbl->is_result) {
        return util::Error{5004, 0, "write not supported on result cursor", ""};
    }
    if (!tbl->fields_cached) {
        if (auto d = describe_table_impl(impl_->db, tbl); !d) return d.error();
    }
    const std::size_t n = tbl->fields.size();
    tbl->staging_row.assign(n, std::string{});
    tbl->staging_nulls.assign(n, true);
    tbl->pending_append = true;
    tbl->row_dirty      = true;
    tbl->row_valid      = true;
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
    if (tbl->is_result) {
        return util::Error{5004, 0, "write not supported on result cursor", ""};
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
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

util::Result<void> SqliteConnection::flush_record(SqliteTable* tbl) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid sqlite flush", ""};
    }
    if (tbl->is_result) {
        return util::Error{5004, 0, "write not supported on result cursor", ""};
    }
    if (!tbl->row_dirty && !tbl->pending_append) return util::Result<void>{};
    if (!tbl->fields_cached) {
        return util::Error{5001, 0, "schema not cached", ""};
    }

    sqlite3* db = impl_->db;
    const std::string qtbl = quote_ident_sqlite(tbl->name);

    if (tbl->pending_append) {
        std::string cols, marks;
        std::vector<std::size_t> col_idxs;
        for (std::size_t i = 0; i < tbl->fields.size(); ++i) {
            if (i >= tbl->staging_nulls.size() || tbl->staging_nulls[i]) {
                continue;
            }
            if (!cols.empty()) {
                cols += ", ";
                marks += ", ";
            }
            cols += quote_ident_sqlite(tbl->fields[i].name);
            marks += "?";
            col_idxs.push_back(i);
        }
        if (col_idxs.empty()) {
            return util::Error{5001, 0, "insert has no columns", tbl->name};
        }
        const std::string sql =
            "INSERT INTO " + qtbl + " (" + cols + ") VALUES (" + marks + ")";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(),
                               static_cast<int>(sql.size()),
                               &stmt, nullptr) != SQLITE_OK) {
            return sqlite_error(db, "prepare insert");
        }
        for (std::size_t b = 0; b < col_idxs.size(); ++b) {
            const std::string& v = tbl->staging_row[col_idxs[b]];
            sqlite3_bind_text(stmt, static_cast<int>(b + 1), v.c_str(),
                              static_cast<int>(v.size()), SQLITE_TRANSIENT);
        }
        const int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) return sqlite_error(db, "insert");

        const std::int64_t new_rowid = sqlite3_last_insert_rowid(db);
        tbl->pending_append = false;
        tbl->row_dirty      = false;
        if (auto r = load_rowids(db, tbl); !r) return r.error();
        return position_at_rowid(db, tbl, new_rowid);
    }

    if (!tbl->positioned || tbl->pos >= tbl->rowids.size()) {
        return util::Error{5026, 0, "no current record", ""};
    }
    const std::int64_t rowid = tbl->rowids[tbl->pos];

    std::string set_clause;
    for (std::size_t i = 0; i < tbl->fields.size(); ++i) {
        if (!set_clause.empty()) set_clause += ", ";
        set_clause += quote_ident_sqlite(tbl->fields[i].name) + " = ?";
    }
    const std::string sql =
        "UPDATE " + qtbl + " SET " + set_clause + " WHERE rowid = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(),
                           static_cast<int>(sql.size()),
                           &stmt, nullptr) != SQLITE_OK) {
        return sqlite_error(db, "prepare update");
    }
    for (std::size_t i = 0; i < tbl->fields.size(); ++i) {
        const int bind_idx = static_cast<int>(i + 1);
        if (i < tbl->staging_nulls.size() && tbl->staging_nulls[i]) {
            sqlite3_bind_null(stmt, bind_idx);
        } else if (i < tbl->staging_row.size()) {
            const std::string& v = tbl->staging_row[i];
            sqlite3_bind_text(stmt, bind_idx, v.c_str(),
                              static_cast<int>(v.size()), SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(stmt, bind_idx);
        }
    }
    sqlite3_bind_int64(stmt, static_cast<int>(tbl->fields.size() + 1), rowid);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return sqlite_error(db, "update");

    tbl->row_dirty = false;
    tbl->current_rowid = rowid;
    return load_current_row(db, tbl);
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
    if (tbl->is_result) {
        return util::Error{5004, 0, "write not supported on result cursor", ""};
    }
    if (tbl->pending_append) {
        return util::Error{5026, 0, "no current record", ""};
    }
    if (!tbl->positioned || tbl->pos >= tbl->rowids.size()) {
        return util::Error{5026, 0, "no current record", ""};
    }
    const std::int64_t rowid = tbl->rowids[tbl->pos];

    const std::string sql =
        "DELETE FROM " + quote_ident_sqlite(tbl->name) + " WHERE rowid = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(impl_->db, sql.c_str(),
                           static_cast<int>(sql.size()),
                           &stmt, nullptr) != SQLITE_OK) {
        return sqlite_error(impl_->db, "prepare delete");
    }
    sqlite3_bind_int64(stmt, 1, rowid);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return sqlite_error(impl_->db, "delete");

    tbl->row_dirty      = false;
    tbl->pending_append = false;
    if (auto r = load_rowids(impl_->db, tbl); !r) return r.error();
    return util::Result<void>{};
#else
    (void)tbl;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

#if defined(OPENADS_WITH_SQLITE)
namespace {

util::Result<void> ensure_lock_table(sqlite3* db) {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS \"OPENADS$LOCKS\" ("
        "key TEXT PRIMARY KEY NOT NULL)";
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "create lock table";
        sqlite3_free(err);
        return util::Error{5001, 0, msg, ""};
    }
    return util::Result<void>{};
}

std::string sqlite_lock_key(const std::string& logical) {
    std::string out;
    out.reserve(logical.size() * 2 + 2);
    out.push_back('\'');
    for (char c : logical) {
        if (c == '\'') out.push_back('\'');
        out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

std::string record_lock_key(const SqliteTable& tbl, std::size_t pos) {
    std::string k = "R\x1f" + tbl.name;
    if (pos < tbl.rowids.size()) {
        k += "\x1f" + std::to_string(tbl.rowids[pos]);
    }
    return k;
}

util::Result<void> lock_key(sqlite3* db, const std::string& key, bool acquire) {
    if (auto r = ensure_lock_table(db); !r) return r.error();
    const std::string sql = acquire
        ? "INSERT INTO \"OPENADS$LOCKS\" (key) VALUES (" +
          sqlite_lock_key(key) + ")"
        : "DELETE FROM \"OPENADS$LOCKS\" WHERE key = " + sqlite_lock_key(key);
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (rc == SQLITE_OK) return util::Result<void>{};
    std::string msg = err ? err : (acquire ? "lock failed" : "unlock failed");
    sqlite3_free(err);
    if (acquire && rc == SQLITE_CONSTRAINT) {
        return util::Error{5035, 0, "record locked", ""};
    }
    return util::Error{5001, 0, msg, ""};
}

}  // namespace
#endif

util::Result<void> SqliteConnection::lock_record(SqliteTable* tbl,
                                               std::uint32_t recno) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr || tbl->is_result) {
        return util::Error{5001, 0, "invalid sqlite lock", ""};
    }
    const std::size_t pos =
        (recno == 0) ? tbl->pos : static_cast<std::size_t>(recno - 1);
    if (!tbl->positioned || pos >= tbl->rowids.size()) {
        return util::Error{5026, 0, "no current record", ""};
    }
    return lock_key(impl_->db, record_lock_key(*tbl, pos), true);
#else
    (void)tbl; (void)recno;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

util::Result<void> SqliteConnection::unlock_record(SqliteTable* tbl,
                                                 std::uint32_t recno) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr || tbl->is_result) {
        return util::Error{5001, 0, "invalid sqlite unlock", ""};
    }
    const std::size_t pos =
        (recno == 0) ? tbl->pos : static_cast<std::size_t>(recno - 1);
    if (pos >= tbl->rowids.size()) return util::Result<void>{};
    return lock_key(impl_->db, record_lock_key(*tbl, pos), false);
#else
    (void)tbl; (void)recno;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

util::Result<void> SqliteConnection::lock_table(SqliteTable* tbl) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr || tbl->is_result) {
        return util::Error{5001, 0, "invalid sqlite lock", ""};
    }
    auto r = lock_key(impl_->db, "T\x1f" + tbl->name, true);
    if (!r) return r.error();
    return util::Result<void>{};
#else
    (void)tbl;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

util::Result<void> SqliteConnection::unlock_table(SqliteTable* tbl) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid() || tbl == nullptr || tbl->is_result) {
        return util::Error{5001, 0, "invalid sqlite unlock", ""};
    }
    return lock_key(impl_->db, "T\x1f" + tbl->name, false);
#else
    (void)tbl;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

util::Result<void> SqliteConnection::restructure_change(
    const std::string& table_name,
    const std::vector<SqlDdlColumn>& changes) {
#if defined(OPENADS_WITH_SQLITE)
    if (!valid()) {
        return util::Error{5001, 0, "sqlite connection not open", ""};
    }
    if (!is_safe_identifier(table_name)) {
        return util::Error{5001, 0, "unsafe table name", table_name};
    }
    if (changes.empty()) return util::Result<void>{};

    std::unordered_map<std::string, SqlDdlColumn> change_map;
    for (const auto& c : changes) {
        if (!is_safe_identifier(c.name)) {
            return util::Error{5001, 0, "unsafe column name", c.name};
        }
        change_map[c.name] = c;
    }

    struct PragmaCol {
        std::string name;
        std::string decl_type;
        bool        notnull = false;
        bool        pk      = false;
    };
    std::vector<PragmaCol> cols;
    const std::string pragma = "PRAGMA table_info(\"" + table_name + "\")";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(impl_->db, pragma.c_str(),
                           static_cast<int>(pragma.size()),
                           &stmt, nullptr) != SQLITE_OK) {
        return sqlite_error(impl_->db, "pragma table_info");
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PragmaCol c;
        const char* n = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        c.name      = n ? n : "";
        c.decl_type = t ? t : "TEXT";
        c.notnull   = sqlite3_column_int(stmt, 3) != 0;
        c.pk        = sqlite3_column_int(stmt, 5) != 0;
        cols.push_back(std::move(c));
    }
    sqlite3_finalize(stmt);
    if (cols.empty()) {
        return util::Error{5001, 0, "table not found", table_name};
    }

    std::unordered_map<std::string, char> old_types;
    if (auto r = ensure_colmeta_table(impl_->db); r) {
        const std::string meta_sql =
            "SELECT col_name, xbase_type FROM \"OPENADS$COLMETA\" "
            "WHERE table_name = \"" + table_name + "\"";
        sqlite3_stmt* meta_stmt = nullptr;
        if (sqlite3_prepare_v2(impl_->db, meta_sql.c_str(),
                               static_cast<int>(meta_sql.size()),
                               &meta_stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(meta_stmt) == SQLITE_ROW) {
                const char* cn = reinterpret_cast<const char*>(
                    sqlite3_column_text(meta_stmt, 0));
                const char* xt = reinterpret_cast<const char*>(
                    sqlite3_column_text(meta_stmt, 1));
                if (cn && xt && xt[0] != '\0') {
                    old_types[cn] = xt[0];
                }
            }
            sqlite3_finalize(meta_stmt);
        }
    }
    for (const auto& c : cols) {
        if (old_types.find(c.name) == old_types.end()) {
            old_types[c.name] = xbase_type_from_decl(c.decl_type);
        }
    }

    const std::string staging = table_name + "__openads_new";
    std::string create = "CREATE TABLE \"" + staging + "\" (";
    std::string col_list;
    std::string select_list;
    for (std::size_t i = 0; i < cols.size(); ++i) {
        if (i) {
            create += ", ";
            col_list += ", ";
            select_list += ", ";
        }
        char new_xt = old_types[cols[i].name];
        if (auto it = change_map.find(cols[i].name); it != change_map.end()) {
            new_xt = it->second.xbase_type;
        }
        const std::string decl = sqlite_decl_for_xbase(new_xt);
        create += "\"" + cols[i].name + "\" " + decl;
        if (cols[i].pk) create += " PRIMARY KEY";
        else if (cols[i].notnull) create += " NOT NULL";
        col_list += "\"" + cols[i].name + "\"";
        select_list += sqlite_cast_expr(cols[i].name,
                                        old_types[cols[i].name], new_xt);
    }
    create += ")";
    if (auto r = exec_sql(create); !r) return r.error();
    const std::string ins =
        "INSERT INTO \"" + staging + "\" (" + col_list + ") SELECT " +
        select_list + " FROM \"" + table_name + "\"";
    if (auto r = exec_sql(ins); !r) return r.error();
    if (auto r = exec_sql("DROP TABLE \"" + table_name + "\""); !r) {
        return r.error();
    }
    const std::string rename =
        "ALTER TABLE \"" + staging + "\" RENAME TO \"" + table_name + "\"";
    if (auto r = exec_sql(rename); !r) return r.error();
    if (auto r = ensure_colmeta_table(impl_->db); !r) return r.error();
    for (const auto& c : changes) {
        const std::string upsert =
            "INSERT INTO \"OPENADS$COLMETA\" "
            "(table_name, col_name, xbase_type, col_length, col_dec) "
            "VALUES (\"" + table_name + "\", \"" + c.name + "\", '" +
            std::string(1, c.xbase_type) + "', " +
            std::to_string(static_cast<unsigned>(c.length)) + ", " +
            std::to_string(static_cast<unsigned>(c.dec)) + ") "
            "ON CONFLICT(table_name, col_name) DO UPDATE SET "
            "xbase_type=excluded.xbase_type, "
            "col_length=excluded.col_length, "
            "col_dec=excluded.col_dec";
        if (auto ex = exec_sql(upsert); !ex) return ex.error();
    }
    return util::Result<void>{};
#else
    (void)table_name;
    (void)changes;
    return util::Error{5004, 0, "sqlite backend disabled", ""};
#endif
}

} // namespace openads::sql_backend