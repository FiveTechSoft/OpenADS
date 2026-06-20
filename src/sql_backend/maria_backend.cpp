#include "sql_backend/maria_backend.h"

#include "openads/ace.h"

#include <cctype>
#include <cstdio>

#if defined(OPENADS_WITH_MARIADB)
#include <mysql.h>
#endif

namespace openads::sql_backend {

MariaTable::FieldDesc map_maria_column(const char* name,
                                       const char* data_type,
                                       bool nullable,
                                       int char_max_len,
                                       int numeric_precision,
                                       int numeric_scale) {
    MariaTable::FieldDesc fd;
    fd.name     = name ? name : "";
    fd.nullable = nullable;

    std::string dt = data_type ? data_type : "";
    for (auto& c : dt) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (dt.find("int") != std::string::npos ||
        dt == "tinyint" || dt == "smallint" || dt == "mediumint" ||
        dt == "bigint") {
        fd.type     = ADS_INTEGER;
        fd.length   = 4;
        fd.decimals = 0;
    } else if (dt.find("double") != std::string::npos ||
               dt.find("float") != std::string::npos ||
               dt.find("decimal") != std::string::npos ||
               dt.find("numeric") != std::string::npos) {
        fd.type     = ADS_DOUBLE;
        fd.length   = 8;
        fd.decimals = numeric_scale > 0
                          ? static_cast<std::uint16_t>(numeric_scale)
                          : 6;
        (void)numeric_precision;
    } else if (dt.find("blob") != std::string::npos ||
               dt.find("binary") != std::string::npos) {
        fd.type     = ADS_BINARY;
        fd.length   = 10;
        fd.decimals = 0;
    } else {
        fd.type     = ADS_STRING;
        fd.length   = char_max_len > 0
                          ? static_cast<std::uint32_t>(char_max_len)
                          : 64;
        fd.decimals = 0;
    }
    return fd;
}

#if defined(OPENADS_WITH_MARIADB)

std::string format_maria_value(char** row, unsigned int col, bool& is_null) {
    is_null = false;
    if (row == nullptr || row[col] == nullptr) {
        is_null = true;
        return {};
    }
    return row[col];
}

util::Error maria_error(const char* context, const char* msg) {
    return util::Error{5001, 0,
                       std::string(context) + ": " + (msg ? msg : ""),
                       ""};
}

#else

std::string format_maria_value(char**, unsigned int, bool& is_null) {
    is_null = true;
    return {};
}

util::Error maria_error(const char* context, const char* msg) {
    (void)context;
    (void)msg;
    return util::Error{5004, 0, "mariadb backend disabled", ""};
}

#endif

std::size_t field_index_ci(const MariaTable& tbl, const std::string& name) {
    std::string want = name;
    for (auto& c : want) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    for (std::size_t i = 0; i < tbl.fields.size(); ++i) {
        std::string have = tbl.fields[i].name;
        for (auto& c : have) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        if (have == want) return i;
    }
    return static_cast<std::size_t>(-1);
}

} // namespace openads::sql_backend