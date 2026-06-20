#pragma once

#include "sql_backend/maria_table.h"
#include "util/result.h"

namespace openads::sql_backend {

MariaTable::FieldDesc map_maria_column(const char* name,
                                       const char* data_type,
                                       bool nullable,
                                       int char_max_len,
                                       int numeric_precision,
                                       int numeric_scale);

std::string format_maria_value(char** row, unsigned int col, bool& is_null);

util::Error maria_error(const char* context, const char* msg);

std::size_t field_index_ci(const MariaTable& tbl, const std::string& name);

} // namespace openads::sql_backend