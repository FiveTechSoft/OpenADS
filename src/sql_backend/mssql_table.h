#pragma once

// Buffered read-only table over an MS SQL Server result set.
// The entire SELECT * result is materialised in memory at open time;
// navigation (GoTop / Skip / GoBottom / AtEOF / AtBOF) is pure
// arithmetic over the in-memory buffer — zero wire round-trips after open.
//
// v1 scope: READ-ONLY.  Write / seek methods return AE_FUNCTION_NOT_AVAILABLE
// via the ABI layer; they are not even declared here.

#if defined(OPENADS_WITH_MSSQL)

#include "sql_backend/tds_protocol.h"
#include "util/result.h"

#include <cstdint>
#include <memory>
#include <string>

namespace openads::sql_backend {

class MssqlConnection;  // mssql_connection.h

struct MssqlTable {
    // Entire result set buffered in memory.
    tds::QueryResult data;

    // 0-based cursor position; data.rows.size() == EOF sentinel.
    std::size_t pos = 0;

    // BOF / EOF flags (mirrors OdbcTable convention).
    bool bof = false;
    bool eof = false;

    // Last seek result — always false (no seek in v1).
    bool last_found = false;

    // --------------------------------------------------------------------
    // Factory
    // --------------------------------------------------------------------

    /// Execute "SELECT * FROM [table_name]" on conn, buffer the result.
    /// Returns AE_INTERNAL_ERROR if table_name fails the safe-identifier
    /// check.  Returns the server error on a bad query.
    static util::Result<std::unique_ptr<MssqlTable>>
        open(MssqlConnection& c, const std::string& table_name);

    /// Wrap an already-decoded QueryResult (used by AdsExecuteSQLDirect
    /// passthrough on an MssqlConnection handle).
    static std::unique_ptr<MssqlTable> from_result(tds::QueryResult qr);

    // --------------------------------------------------------------------
    // Navigation
    // --------------------------------------------------------------------

    void go_top();
    void go_bottom();
    void skip(long n);
    bool at_bof() const;
    bool at_eof() const;

    // --------------------------------------------------------------------
    // Schema
    // --------------------------------------------------------------------

    std::size_t   field_count()    const;
    std::string   field_name(std::size_t i)     const;
    std::uint16_t field_type(std::size_t i)     const;  // ADS_* constant
    std::uint32_t field_length(std::size_t i)   const;
    std::uint16_t field_decimals(std::size_t i) const;

    // --------------------------------------------------------------------
    // Position / count
    // --------------------------------------------------------------------

    std::uint32_t record_num()   const;  // 1-based; 0 at BOF / empty
    std::uint32_t record_count() const;

    // --------------------------------------------------------------------
    // Data access
    // --------------------------------------------------------------------

    /// Returns the string value and null flag for column i of the current row.
    /// Returns false if there is no current row (BOF / EOF) or i is out of range.
    bool get_field(std::size_t i, std::string& out, bool& is_null) const;
};

} // namespace openads::sql_backend

#endif // defined(OPENADS_WITH_MSSQL)
