#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openads::sql_backend {

class MariaConnection;

struct MariaTable {
    MariaConnection* conn = nullptr;
    std::string        name;

    struct FieldDesc {
        std::string   name;
        std::uint16_t type     = 0;
        std::uint32_t length   = 0;
        std::uint16_t decimals = 0;
        bool          nullable = true;
    };

    std::vector<FieldDesc> fields;
    bool                   fields_cached = false;

    std::vector<std::string> current_row;
    std::vector<bool>        current_nulls;
    bool                     row_valid = false;

    std::uint32_t current_recno      = 0;
    bool          current_deleted    = false;
    std::uint32_t cached_rec_count   = 0;
    bool          rec_count_cached   = false;

    std::vector<std::string> pk_columns;
    struct PkRow {
        std::vector<std::string> values;
    };
    std::vector<PkRow> pk_snapshot;
    std::size_t        pos              = 0;
    bool               positioned       = false;
    bool               last_seek_found  = false;
};

} // namespace openads::sql_backend