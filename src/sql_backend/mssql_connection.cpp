#include "sql_backend/mssql_connection.h"

#if defined(OPENADS_WITH_MSSQL)

#include "openads/error.h"
#include "sql_backend/mssql_table.h"
#include "sql_backend/mssql_uri.h"
#include "sql_backend/tds_protocol.h"

#include <cctype>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace openads::sql_backend {

namespace {

// [name] with ']' doubled — safe SQL Server identifier quoting.
std::string quote_ident(const std::string& name) {
    std::string out = "[";
    for (char c : name) { if (c == ']') out += ']'; out += c; }
    out += ']';
    return out;
}

// N'...' literal with '\'' doubled. All staged values are bound as Unicode
// string literals; SQL Server implicit-converts to the target column type.
std::string quote_lit(const std::string& v) {
    std::string out = "N'";
    for (char c : v) { if (c == '\'') out += '\''; out += c; }
    out += '\'';
    return out;
}

std::size_t col_index_ci(const MssqlTable& t, const std::string& name) {
    for (std::size_t i = 0; i < t.data.columns.size(); ++i) {
        const std::string& cn = t.data.columns[i].name;
        if (cn.size() != name.size()) continue;
        bool eq = true;
        for (std::size_t k = 0; k < cn.size(); ++k) {
            if (std::tolower(static_cast<unsigned char>(cn[k])) !=
                std::tolower(static_cast<unsigned char>(name[k]))) { eq = false; break; }
        }
        if (eq) return i;
    }
    return static_cast<std::size_t>(-1);
}

// Build "[pk1] = N'v1' AND [pk2] = N'v2'" from a result row's PK cells.
std::string pk_where(const MssqlTable& t,
                     const std::vector<tds::TdsCell>& row) {
    std::string w;
    bool any = false;
    for (std::size_t i : t.pk_cols) {
        if (any) w += " AND ";
        w += quote_ident(t.data.columns[i].name);
        if (i < row.size() && row[i].is_null) w += " IS NULL";
        else w += " = " + quote_lit(i < row.size() ? row[i].value : std::string{});
        any = true;
    }
    return w;
}

} // namespace

struct MssqlConnection::Impl {
    TdsTlsChannel channel;
    bool          authenticated = false;
};

MssqlConnection::MssqlConnection() = default;
MssqlConnection::~MssqlConnection() = default;
MssqlConnection::MssqlConnection(MssqlConnection&&) noexcept = default;
MssqlConnection& MssqlConnection::operator=(MssqlConnection&&) noexcept = default;

bool MssqlConnection::valid() const noexcept {
    return impl_ && impl_->authenticated && impl_->channel.valid();
}

void MssqlConnection::disconnect() noexcept {
    if (impl_) {
        impl_->channel.close();
        impl_->authenticated = false;
    }
}

util::Result<MssqlConnection> MssqlConnection::open(const MssqlUri& uri) {
    // 1. Establish the TLS-in-TDS channel (TCP + PRELOGIN + tunnelled TLS).
    auto chan = TdsTlsChannel::connect(uri);
    if (!chan) return chan.error();

    MssqlConnection conn;
    conn.impl_ = std::make_unique<Impl>();
    conn.impl_->channel = std::move(chan).value();

    // 2. Build + send the LOGIN7 message over the encrypted session.
    tds::Login7Params params;
    params.hostname    = "OpenADS";
    params.username    = uri.user;
    params.password    = uri.password;
    params.app_name    = "OpenADS";
    params.server_name = uri.host;
    params.database    = uri.database;

    std::vector<std::uint8_t> login7 = tds::build_login7(params);
    // build_login7 already prepends the 8-byte TDS header; the channel's
    // send_tds adds its own header, so strip the embedded one and let the
    // channel frame (and segment) the LOGIN7 structure itself.
    std::vector<std::uint8_t> login7_payload(login7.begin() + 8, login7.end());

    if (auto r = conn.impl_->channel.send_tds(tds::TDS_PKT_LOGIN7,
                                              login7_payload); !r) {
        return r.error();
    }

    // 3. Read the login response token stream and parse it.
    auto reply = conn.impl_->channel.recv_tds();
    if (!reply) return reply.error();
    const std::vector<std::uint8_t>& payload = reply.value().second;

    tds::LoginResult res = tds::parse_login_response(payload.data(),
                                                     payload.size());
    if (!res.authenticated) {
        // Surface the SERVER's error (number + message) — never the password
        // or connection string.  Map to a login-failed family code so the ABI
        // returns a non-zero, recognisable error.
        std::int32_t code = static_cast<std::int32_t>(res.error_number);
        if (code == 0) code = openads::AE_LOGIN_FAILED;
        std::string msg = res.message.empty()
                              ? std::string("MSSQL login failed")
                              : res.message;
        return util::Error{code, 0, msg, ""};
    }

    conn.impl_->authenticated = true;
    return conn;
}

util::Result<tds::QueryResult> MssqlConnection::query(const std::string& sql) {
    if (!impl_ || !impl_->channel.valid() || !impl_->authenticated) {
        return util::Error{openads::AE_NO_CONNECTION, 0,
            "MSSQL not connected", ""};
    }
    // Send SQL batch packet.
    if (auto r = impl_->channel.send_tds(tds::TDS_PKT_SQLBATCH,
                                         tds::build_sql_batch(sql)); !r) {
        return r.error();
    }
    // Receive the server reply (may span multiple TDS packets, reassembled
    // by recv_tds with the 64 MiB cap).
    auto reply = impl_->channel.recv_tds();
    if (!reply) return reply.error();
    const auto& payload = reply.value().second;

    tds::QueryResult qr = tds::parse_query_response(payload.data(),
                                                    payload.size());
    if (!qr.ok) {
        if (!qr.unsupported_type.empty()) {
            // COLMETADATA contained a TDS type token we cannot decode.
            return util::Error{openads::AE_TYPE_MISMATCH, 0,
                "unsupported MSSQL column type: " + qr.unsupported_type, ""};
        }
        // Server ERROR token: surface the server error number if non-zero;
        // fall back to AE_PARSE_ERROR so callers get a distinct, non-zero code.
        // NEVER embed the sql string in the error (could contain sensitive data).
        std::int32_t code = qr.error_number
                              ? static_cast<std::int32_t>(qr.error_number)
                              : static_cast<std::int32_t>(openads::AE_PARSE_ERROR);
        std::string msg = qr.message.empty()
                              ? std::string("MSSQL query failed")
                              : qr.message;
        return util::Error{code, 0, msg, ""};
    }
    return qr;
}

// ---------------------------------------------------------------------------
// Navigational write
// ---------------------------------------------------------------------------

namespace {
// Re-run SELECT * and replace the table's buffered result (so record_count
// and navigation reflect the write). Resets the cursor to BOF.
util::Result<void> refetch(MssqlConnection& c, MssqlTable* tbl) {
    auto qr = c.query("SELECT * FROM " + quote_ident(tbl->table_name));
    if (!qr) return qr.error();
    if (!qr.value().ok) {
        return util::Error{static_cast<std::int32_t>(qr.value().error_number),
                           0, qr.value().message, ""};
    }
    tbl->data = std::move(qr).value();
    tbl->pos  = 0;
    tbl->bof  = true;
    tbl->eof  = tbl->data.rows.empty();
    return util::Result<void>{};
}
} // namespace

util::Result<void> MssqlConnection::append_blank(MssqlTable* tbl) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mssql append", ""};
    }
    const std::size_t n = tbl->data.columns.size();
    tbl->staging_row.assign(n, std::string{});
    tbl->staging_nulls.assign(n, true);
    tbl->pending_append = true;
    tbl->row_dirty      = true;
    return util::Result<void>{};
}

util::Result<void> MssqlConnection::set_field(
    MssqlTable* tbl, const std::string& field_name, const std::string& value) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mssql set_field", ""};
    }
    const std::size_t idx = col_index_ci(*tbl, field_name);
    if (idx == static_cast<std::size_t>(-1)) {
        return util::Error{5063, 0, "column not found", field_name};
    }
    const std::size_t n = tbl->data.columns.size();
    if (!tbl->row_dirty && !tbl->pending_append) {
        // Seed staging from the current row so unchanged columns survive UPDATE.
        tbl->staging_row.assign(n, std::string{});
        tbl->staging_nulls.assign(n, true);
        if (tbl->pos < tbl->data.rows.size()) {
            const auto& row = tbl->data.rows[tbl->pos];
            for (std::size_t i = 0; i < n && i < row.size(); ++i) {
                tbl->staging_row[i]   = row[i].value;
                tbl->staging_nulls[i] = row[i].is_null;
            }
        }
    }
    if (tbl->staging_row.size() < n) {
        tbl->staging_row.resize(n);
        tbl->staging_nulls.resize(n, true);
    }
    tbl->staging_row[idx]   = value;
    tbl->staging_nulls[idx] = false;
    tbl->row_dirty          = true;
    return util::Result<void>{};
}

util::Result<void> MssqlConnection::flush_record(MssqlTable* tbl) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mssql flush", ""};
    }
    if (!tbl->row_dirty && !tbl->pending_append) return util::Result<void>{};
    const std::size_t n = tbl->data.columns.size();

    if (tbl->pending_append) {
        std::string cols, vals;
        bool any = false;
        for (std::size_t i = 0; i < n; ++i) {
            if (i < tbl->staging_nulls.size() && tbl->staging_nulls[i]) continue;
            if (any) { cols += ", "; vals += ", "; }
            cols += quote_ident(tbl->data.columns[i].name);
            vals += quote_lit(tbl->staging_row[i]);
            any = true;
        }
        if (!any) {
            return util::Error{5001, 0, "insert has no columns", tbl->table_name};
        }
        const std::string sqlq = "INSERT INTO " + quote_ident(tbl->table_name) +
                                 " (" + cols + ") VALUES (" + vals + ")";
        auto r = query(sqlq);
        if (!r) return r.error();
        if (!r.value().ok) {
            return util::Error{static_cast<std::int32_t>(r.value().error_number),
                               0, r.value().message, sqlq};
        }
        tbl->pending_append = false;
        tbl->row_dirty      = false;
        return refetch(*this, tbl);
    }

    // UPDATE the current row, keyed on its primary key.
    if (tbl->pk_cols.empty()) {
        return util::Error{5004, 0, "mssql update requires a primary key",
                           tbl->table_name};
    }
    if (tbl->pos >= tbl->data.rows.size()) {
        return util::Error{5026, 0, "no current record", ""};
    }
    const std::string where = pk_where(*tbl, tbl->data.rows[tbl->pos]);
    std::vector<bool> is_pk(n, false);
    for (std::size_t i : tbl->pk_cols) if (i < n) is_pk[i] = true;
    std::string setc;
    bool any = false;
    for (std::size_t i = 0; i < n; ++i) {
        if (is_pk[i] || i >= tbl->staging_row.size()) continue;
        if (any) setc += ", ";
        setc += quote_ident(tbl->data.columns[i].name) + " = " +
                ((i < tbl->staging_nulls.size() && tbl->staging_nulls[i])
                     ? std::string("NULL")
                     : quote_lit(tbl->staging_row[i]));
        any = true;
    }
    if (!any) { tbl->row_dirty = false; return refetch(*this, tbl); }
    const std::string sqlq = "UPDATE " + quote_ident(tbl->table_name) +
                             " SET " + setc + " WHERE " + where;
    auto r = query(sqlq);
    if (!r) return r.error();
    if (!r.value().ok) {
        return util::Error{static_cast<std::int32_t>(r.value().error_number),
                           0, r.value().message, sqlq};
    }
    tbl->row_dirty = false;
    return refetch(*this, tbl);
}

util::Result<void> MssqlConnection::delete_record(MssqlTable* tbl) {
    if (!valid() || tbl == nullptr) {
        return util::Error{5001, 0, "invalid mssql delete", ""};
    }
    if (tbl->pending_append) {
        return util::Error{5026, 0, "no current record", ""};
    }
    if (tbl->pk_cols.empty()) {
        return util::Error{5004, 0, "mssql delete requires a primary key",
                           tbl->table_name};
    }
    if (tbl->pos >= tbl->data.rows.size()) {
        return util::Error{5026, 0, "no current record", ""};
    }
    const std::string sqlq = "DELETE FROM " + quote_ident(tbl->table_name) +
                             " WHERE " + pk_where(*tbl, tbl->data.rows[tbl->pos]);
    auto r = query(sqlq);
    if (!r) return r.error();
    if (!r.value().ok) {
        return util::Error{static_cast<std::int32_t>(r.value().error_number),
                           0, r.value().message, sqlq};
    }
    tbl->row_dirty      = false;
    tbl->pending_append = false;
    return refetch(*this, tbl);
}

} // namespace openads::sql_backend

#endif // defined(OPENADS_WITH_MSSQL)
