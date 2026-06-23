#pragma once

#if defined(OPENADS_WITH_MSSQL)

#include <cstdint>
#include <string>

namespace openads::sql_backend {

// A parsed `mssql://` or `tds://` connection URI.
// Form: mssql://user:password@host[:port]/database
// The user and password fields are percent-decoded.
struct MssqlUri {
    std::string host;
    uint16_t    port             = 1433;
    std::string user;
    std::string password;
    std::string database;
    bool        trust_server_cert = true;
};

// Parse a URI of the form  mssql://user:pass@host[:port]/database
// or the equivalent tds://… scheme.
// Returns false if the URI scheme is not recognised, or if host/database
// are empty after parsing.
bool parse_mssql_uri(const std::string& uri, MssqlUri& out);

} // namespace openads::sql_backend

#endif // defined(OPENADS_WITH_MSSQL)
