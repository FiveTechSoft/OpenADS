#pragma once

#include <cstdint>
#include <string>

namespace openads::sql_backend {

struct MariaUri {
    std::string host;
    std::string user;
    std::string password;
    std::string database;
    std::uint16_t port = 3306;
};

bool parse_maria_uri(const std::string& uri, MariaUri& out);

} // namespace openads::sql_backend