#if defined(OPENADS_WITH_MSSQL)

#include "sql_backend/mssql_uri.h"

#include <cctype>
#include <cstdlib>

namespace openads::sql_backend {

namespace {

// Percent-decode a URI component (e.g. user or password).
// Unknown or malformed escape sequences are passed through as-is.
std::string percent_decode(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '%' && i + 2 < src.size()
                && std::isxdigit(static_cast<unsigned char>(src[i + 1]))
                && std::isxdigit(static_cast<unsigned char>(src[i + 2]))) {
            const char hex[3] = { src[i + 1], src[i + 2], '\0' };
            out += static_cast<char>(static_cast<unsigned char>(
                std::strtoul(hex, nullptr, 16)));
            i += 2;
        } else {
            out += src[i];
        }
    }
    return out;
}

} // namespace

bool parse_mssql_uri(const std::string& uri, MssqlUri& out) {
    // Recognised schemes
    static constexpr const char* kSchemeMssql = "mssql://";
    static constexpr const char* kSchemeTds   = "tds://";
    static const std::size_t     kLenMssql    = 8; // strlen("mssql://")
    static const std::size_t     kLenTds      = 6; // strlen("tds://")

    std::size_t prefix_len = 0;
    if (uri.size() >= kLenMssql && uri.compare(0, kLenMssql, kSchemeMssql) == 0) {
        prefix_len = kLenMssql;
    } else if (uri.size() >= kLenTds && uri.compare(0, kLenTds, kSchemeTds) == 0) {
        prefix_len = kLenTds;
    } else {
        return false;
    }

    // Remainder after scheme: user:pass@host[:port]/database
    const std::string rest = uri.substr(prefix_len);

    // Split user:pass from the @-delimited host part
    const auto at_pos = rest.find('@');
    if (at_pos == std::string::npos) {
        return false;
    }

    const std::string credentials = rest.substr(0, at_pos);
    const std::string hostpart    = rest.substr(at_pos + 1);

    // Credentials: user[:password]
    const auto colon_pos = credentials.find(':');
    if (colon_pos == std::string::npos) {
        out.user     = percent_decode(credentials);
        out.password = {};
    } else {
        out.user     = percent_decode(credentials.substr(0, colon_pos));
        out.password = percent_decode(credentials.substr(colon_pos + 1));
    }

    // Host part: host[:port]/database
    const auto slash_pos = hostpart.find('/');
    if (slash_pos == std::string::npos) {
        return false;
    }

    const std::string hostport = hostpart.substr(0, slash_pos);
    out.database               = hostpart.substr(slash_pos + 1);

    // Parse optional port from hostport (host:port or just host)
    const auto port_colon = hostport.rfind(':');
    if (port_colon == std::string::npos) {
        out.host = hostport;
        out.port = 1433;
    } else {
        const std::string port_str = hostport.substr(port_colon + 1);
        // Validate: all digits
        bool all_digits = !port_str.empty();
        for (char c : port_str) {
            if (!std::isdigit(static_cast<unsigned char>(c))) {
                all_digits = false;
                break;
            }
        }
        if (all_digits) {
            out.host = hostport.substr(0, port_colon);
            const unsigned long p = std::strtoul(port_str.c_str(), nullptr, 10);
            out.port = static_cast<uint16_t>(p > 65535u ? 1433u : p);
        } else {
            // Treat the whole thing as the host (IPv6 or no port)
            out.host = hostport;
            out.port = 1433;
        }
    }

    out.trust_server_cert = true;

    // Require non-empty host and database
    if (out.host.empty() || out.database.empty()) {
        return false;
    }

    return true;
}

} // namespace openads::sql_backend

#endif // defined(OPENADS_WITH_MSSQL)
