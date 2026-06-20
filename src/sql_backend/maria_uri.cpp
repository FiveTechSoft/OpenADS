#include "sql_backend/maria_uri.h"

#include <cstdlib>

namespace openads::sql_backend {

namespace {

bool parse_authority(const std::string& auth, MariaUri& out) {
    std::string userinfo;
    std::string hostport;
    const auto at = auth.find('@');
    if (at == std::string::npos) {
        hostport = auth;
    } else {
        userinfo = auth.substr(0, at);
        hostport = auth.substr(at + 1);
    }
    if (!userinfo.empty()) {
        const auto colon = userinfo.find(':');
        if (colon == std::string::npos) {
            out.user = userinfo;
        } else {
            out.user     = userinfo.substr(0, colon);
            out.password = userinfo.substr(colon + 1);
        }
    }
    if (hostport.empty()) return false;
    if (hostport.front() == '[') {
        const auto close = hostport.find(']');
        if (close == std::string::npos) return false;
        out.host = hostport.substr(1, close - 1);
        if (close + 1 < hostport.size() && hostport[close + 1] == ':') {
            out.port = static_cast<std::uint16_t>(
                std::strtoul(hostport.c_str() + close + 2, nullptr, 10));
        }
    } else {
        const auto colon = hostport.rfind(':');
        if (colon != std::string::npos) {
            const std::string port_str = hostport.substr(colon + 1);
            bool all_digits = !port_str.empty();
            for (char c : port_str) {
                if (c < '0' || c > '9') {
                    all_digits = false;
                    break;
                }
            }
            if (all_digits) {
                out.host = hostport.substr(0, colon);
                out.port = static_cast<std::uint16_t>(
                    std::strtoul(port_str.c_str(), nullptr, 10));
            } else {
                out.host = hostport;
            }
        } else {
            out.host = hostport;
        }
    }
    return !out.host.empty();
}

} // namespace

bool parse_maria_uri(const std::string& uri, MariaUri& out) {
    static constexpr const char* kMaria = "mariadb://";
    static constexpr const char* kMysql = "mysql://";
    const auto mlen  = std::char_traits<char>::length(kMaria);
    const auto mylen = std::char_traits<char>::length(kMysql);

    std::string rest;
    if (uri.size() >= mlen && uri.compare(0, mlen, kMaria) == 0) {
        rest = uri.substr(mlen);
    } else if (uri.size() >= mylen && uri.compare(0, mylen, kMysql) == 0) {
        rest = uri.substr(mylen);
    } else {
        return false;
    }

    out = MariaUri{};
    const auto slash = rest.find('/');
    std::string authority = rest;
    if (slash != std::string::npos) {
        authority = rest.substr(0, slash);
        out.database = rest.substr(slash + 1);
        const auto q = out.database.find('?');
        if (q != std::string::npos) {
            out.database = out.database.substr(0, q);
        }
    }
    if (!parse_authority(authority, out)) return false;
    return true;
}

} // namespace openads::sql_backend