#pragma once

#include <string>

namespace openads::sql_backend {

bool is_safe_identifier(const std::string& name);

} // namespace openads::sql_backend