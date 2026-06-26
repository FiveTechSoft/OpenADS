#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openads::sql_backend {

class SqliteConnection;

// Per-handle table state — mirrors network::RemoteTable layout so the
// ABI bridge can reuse the same caching patterns (schema, row, recno).
struct SqliteTable {
    SqliteConnection* conn = nullptr;
    std::string       name;

    struct FieldDesc {
        std::string   name;
        std::uint16_t type     = 0;  // ADS_* field type code
        std::uint32_t length   = 0;
        std::uint16_t decimals = 0;
        bool          nullable = true;
    };

    std::vector<FieldDesc> fields;
    bool                   fields_cached = false;

    std::vector<std::string> current_row;
    bool                     row_valid = false;

    std::int64_t current_rowid   = 0;
    bool         current_deleted = false;

    std::uint32_t cached_rec_count = 0;
    bool          rec_count_cached = false;

    // Navigation state (ordered by SQLite rowid).
    std::vector<std::int64_t> rowids;
    std::size_t               pos         = 0;
    bool                      positioned = false;
    std::vector<bool>         current_nulls;

    // Tier-2 push-down: when non-empty, the rowid list is loaded with this
    // SQL WHERE fragment so navigation only walks matching rows (the backend
    // filters via its own indexes). Set by SqliteConnection::set_filter from
    // a translated SET FILTER / AOF predicate; empty = no filter.
    std::string where_filter;

    bool last_seek_found = false;

    // Write staging (mirrors MariaTable/FirebirdTable): append_blank/set_field
    // stage column values; flush_record emits an INSERT (pending_append) or a
    // rowid-keyed UPDATE; delete_record a rowid-keyed DELETE.
    std::vector<std::string> staging_row;
    std::vector<bool>        staging_nulls;
    bool                     pending_append = false;
    bool                     row_dirty      = false;

    // Result-set cursor mode (AdsExecuteSQLDirect SELECT passthrough): rows are
    // materialized in memory instead of fetched per-rowid from a base table, so
    // navigation serves `current_row` straight from `result_rows[pos]`.
    bool                                  is_result = false;
    std::vector<std::vector<std::string>> result_rows;
    std::vector<std::vector<bool>>        result_nulls;
};

} // namespace openads::sql_backend