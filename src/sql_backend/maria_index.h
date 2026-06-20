#pragma once

#include <string>

namespace openads::sql_backend {

struct MariaTable;

struct MariaIndex {
    MariaTable* parent          = nullptr;
    std::string column;
    bool        last_seek_found = false;
};

} // namespace openads::sql_backend