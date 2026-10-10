#include "mgmt/mg_health.h"
#include "platform/proc.h"
#include <algorithm>
#include <set>
#include <sstream>
#include <locale>

namespace openads::mgmt {
namespace {
std::string quote(const std::string& text) {
    std::string out = "\"";
    const char* hex = "0123456789abcdef";
    for (char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + "\"";
}
}
std::string health_json(const MgSnapshot& s, const std::string& version) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"schema_version\":1,\"version\":" << quote(version)
        << ",\"scope\":" << quote(s.server_type == 1 ? "server" : "local_process");
    auto number = [&](const char* key, std::uint64_t n) { out << ",\"" << key << "\":" << n; };
    auto usage = [&](const char* key, std::uint32_t now, std::uint32_t peak) {
        out << ",\"" << key << "\":{\"current\":" << now
            << ",\"max_used\":" << std::max(now, peak) << ",\"rejected\":null}";
    };
    usage("users", s.users, s.max_users);
    usage("connections", s.connections, s.max_connections);
    usage("workareas", s.workareas, s.max_workareas);
    usage("table_handles", s.tables, s.max_tables);
    usage("index_bindings", s.indexes, s.max_indexes);
    usage("locks", s.locks, s.max_locks);
    out << ",\"worker_threads\":{\"current\":" << s.worker_threads
        << ",\"max_used\":null,\"rejected\":null}";
    std::set<std::string> tables, indexes;
    for (const auto& t : s.table_list) tables.insert(t.name);
    for (const auto& i : s.index_list) indexes.insert(i.name);
    number("distinct_table_paths", tables.size());
    number("distinct_index_paths", indexes.size());
    number("uptime_seconds", s.uptime_seconds);
    number("operations", s.operations);
    number("logged_errors", s.logged_errors);
    number("rss_bytes", s.rss_bytes);
    number("server_port", s.server_port);
    out << ",\"max_sessions\":" << (s.max_sessions_source.empty() ? "null" : std::to_string(s.max_sessions))
        << ",\"max_sessions_source\":" << (s.max_sessions_source.empty() ? "null" : quote(s.max_sessions_source));
    number("packets_in", s.packets_in); number("packets_out", s.packets_out);
    number("bytes_in", s.bytes_in); number("bytes_out", s.bytes_out);
    number("disconnects", s.disconnects); number("partial_connects", s.partial_connects);
    const auto count = platform::process_fd_count(), limit = platform::process_fd_limit();
    out << ",\"os_fd_count\":" << (count ? std::to_string(*count) : "null")
        << ",\"os_fd_soft_limit\":" << (limit ? std::to_string(*limit) : "null")
        << ",\"parked_handles\":null"
        << ",\"semantics\":{\"workareas\":\"server-open table handles, includes client-parked handles; not Harbour Select() areas\","
        << "\"users\":\"management user entries, not distinct people or application instances\","
        << "\"distinct_paths\":\"exact path strings, not canonical filesystem identities\","
        << "\"max_used\":\"workarea/table-handle maxima retain open events; other maxima keep existing telemetry semantics\","
        << "\"parked_handles\":\"client-only state, unavailable at server\","
        << "\"rejected\":\"not measured\","
        << "\"snapshot\":\"best-effort concurrent sample; management query session may be included\"}}";
    return out.str();
}
}
