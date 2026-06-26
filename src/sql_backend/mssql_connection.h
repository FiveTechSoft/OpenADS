#pragma once

// Native MS SQL Server backend connection.  Orchestrates the full TDS 7.4
// handshake over a TLS-in-TDS channel:
//   TCP connect -> PRELOGIN -> tunnelled TLS handshake -> LOGIN7 -> LOGINACK.
//
// v1 scope: authentication only (open / disconnect).  Table navigation and
// query execution build on this channel in later slices.

#if defined(OPENADS_WITH_MSSQL)

#include "sql_backend/tds_protocol.h"
#include "sql_backend/tds_tls_channel.h"
#include "util/result.h"

#include <memory>
#include <string>

namespace openads::sql_backend {

struct MssqlUri;    // sql_backend/mssql_uri.h
struct MssqlTable;  // sql_backend/mssql_table.h

class MssqlConnection {
public:
    MssqlConnection();
    ~MssqlConnection();

    MssqlConnection(MssqlConnection&&) noexcept;
    MssqlConnection& operator=(MssqlConnection&&) noexcept;

    MssqlConnection(const MssqlConnection&)            = delete;
    MssqlConnection& operator=(const MssqlConnection&) = delete;

    // Connect + authenticate.  On a wrong password / bad login the server
    // sends a TDS ERROR token; open() returns that error number (NEVER the
    // password or connection string).
    static util::Result<MssqlConnection> open(const MssqlUri& uri);

    void disconnect() noexcept;
    bool valid() const noexcept;

    // Execute a SQL batch and return the decoded result set.
    // The connection must be authenticated (valid() == true).
    // SQL text is backend-generated; NEVER put secrets or credentials in sql.
    util::Result<tds::QueryResult> query(const std::string& sql);

    // Navigational write (mirrors the other SQL backends): append_blank stages
    // a blank row, set_field stages one column, flush_record emits an INSERT
    // (pending_append) or a PK-keyed UPDATE, delete_record a PK-keyed DELETE.
    // The result set is re-fetched after each write so navigation/count stay
    // consistent. Requires the table to have a discovered primary key.
    util::Result<void> append_blank(MssqlTable* tbl);
    util::Result<void> set_field(MssqlTable* tbl,
                                 const std::string& field_name,
                                 const std::string& value);
    util::Result<void> flush_record(MssqlTable* tbl);
    util::Result<void> delete_record(MssqlTable* tbl);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace openads::sql_backend

#endif // defined(OPENADS_WITH_MSSQL)
