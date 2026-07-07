#include "network/mg_wire.h"

#include <chrono>
#include <cstring>

namespace openads::network {

namespace {

void put_u16(std::string& b, std::uint16_t v) {
    b.push_back(static_cast<char>(v & 0xFF));
    b.push_back(static_cast<char>((v >> 8) & 0xFF));
}
void put_u32(std::string& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
void put_u64(std::string& b, std::uint64_t v) {
    for (int i = 0; i < 8; ++i)
        b.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
void put_str(std::string& b, const std::string& s) {
    put_u16(b, static_cast<std::uint16_t>(s.size()));
    b.append(s);
}

// Cursor-based reader; sets ok=false on overrun.
struct Reader {
    const std::string& b;
    std::size_t        pos = 0;
    bool               ok  = true;

    std::uint16_t u16() {
        if (pos + 2 > b.size()) { ok = false; return 0; }
        std::uint16_t v = static_cast<std::uint16_t>(
            static_cast<std::uint8_t>(b[pos]) |
            (static_cast<std::uint16_t>(
                 static_cast<std::uint8_t>(b[pos + 1])) << 8));
        pos += 2;
        return v;
    }
    std::uint32_t u32() {
        if (pos + 4 > b.size()) { ok = false; return 0; }
        std::uint32_t v = 0;
        for (std::size_t i = 0; i < 4; ++i)
            v |= static_cast<std::uint32_t>(
                     static_cast<std::uint8_t>(b[pos + i])) << (8 * i);
        pos += 4;
        return v;
    }
    std::uint64_t u64() {
        if (pos + 8 > b.size()) { ok = false; return 0; }
        std::uint64_t v = 0;
        for (std::size_t i = 0; i < 8; ++i)
            v |= static_cast<std::uint64_t>(
                     static_cast<std::uint8_t>(b[pos + i])) << (8 * i);
        pos += 8;
        return v;
    }
    std::string str() {
        std::uint16_t n = u16();
        if (!ok || pos + n > b.size()) { ok = false; return {}; }
        std::string s = b.substr(pos, n);
        pos += n;
        return s;
    }
};

void put_tp(std::string& b,
            const std::chrono::system_clock::time_point& tp) {
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                    tp.time_since_epoch()).count();
    put_u64(b, static_cast<std::uint64_t>(secs));
}
std::chrono::system_clock::time_point get_tp(Reader& r) {
    return std::chrono::system_clock::time_point{
        std::chrono::seconds{static_cast<long long>(r.u64())}};
}

}  // namespace

std::string encode_mg_request(MgRequestKind kind, std::uint16_t arg) {
    std::string b;
    b.push_back(static_cast<char>(kind));
    put_u16(b, arg);
    return b;
}

util::Result<MgRequest> decode_mg_request(const std::string& payload) {
    if (payload.size() < 3)
        return openads::util::Error{1, 0, "short mg request", ""};
    MgRequest req;
    req.kind = static_cast<MgRequestKind>(
        static_cast<std::uint8_t>(payload[0]));
    req.arg = static_cast<std::uint16_t>(
        static_cast<std::uint8_t>(payload[1]) |
        (static_cast<std::uint16_t>(
             static_cast<std::uint8_t>(payload[2])) << 8));
    return req;
}

std::string encode_mg_snapshot(const mgmt::MgSnapshot& s) {
    std::string b;
    put_u32(b, s.users);
    put_u32(b, s.connections);
    put_u32(b, s.workareas);
    put_u32(b, s.tables);
    put_u32(b, s.indexes);
    put_u32(b, s.locks);
    put_u32(b, s.worker_threads);
    put_u16(b, s.server_type);
    put_u64(b, s.rss_bytes);
    put_u16(b, s.server_port);
    put_u64(b, s.uptime_seconds);
    put_u64(b, s.packets_in);
    put_u64(b, s.packets_out);
    put_u64(b, s.bytes_in);
    put_u64(b, s.bytes_out);
    put_u64(b, s.disconnects);
    put_u64(b, s.partial_connects);
    put_u64(b, s.operations);
    put_u64(b, s.logged_errors);
    put_u32(b, s.max_users);
    put_u32(b, s.max_connections);
    put_u32(b, s.max_workareas);
    put_u32(b, s.max_tables);
    put_u32(b, s.max_indexes);
    put_u32(b, s.max_locks);

    put_u32(b, static_cast<std::uint32_t>(s.user_list.size()));
    for (const auto& u : s.user_list) {
        put_str(b, u.name);
        put_str(b, u.address);
        put_str(b, u.os_login);
        put_u16(b, u.conn_no);
        put_tp(b, u.connected_at);
        put_u32(b, u.avg_cost_micros);
    }
    put_u32(b, static_cast<std::uint32_t>(s.table_list.size()));
    for (const auto& t : s.table_list) {
        put_str(b, t.name);
        put_str(b, t.user);
        put_u16(b, t.conn_no);
        put_u16(b, t.open_mode);
        put_u16(b, t.lock_type);
    }
    put_u32(b, static_cast<std::uint32_t>(s.index_list.size()));
    for (const auto& x : s.index_list) {
        put_str(b, x.name);
        put_str(b, x.tag);
        put_str(b, x.expression);
    }
    put_u32(b, static_cast<std::uint32_t>(s.lock_list.size()));
    for (const auto& l : s.lock_list) {
        put_str(b, l.user);
        put_u16(b, l.conn_no);
        put_u32(b, l.recno);
    }
    put_u32(b, static_cast<std::uint32_t>(s.thread_list.size()));
    for (const auto& t : s.thread_list) {
        put_u32(b, t.thread_no);
        put_u16(b, t.opcode);
        put_str(b, t.user);
        put_u16(b, t.conn_no);
        put_str(b, t.os_login);
    }
    return b;
}

util::Result<mgmt::MgSnapshot> decode_mg_snapshot(
    const std::string& payload) {
    Reader r{payload};
    mgmt::MgSnapshot s;
    s.users          = r.u32();
    s.connections    = r.u32();
    s.workareas      = r.u32();
    s.tables         = r.u32();
    s.indexes        = r.u32();
    s.locks          = r.u32();
    s.worker_threads = r.u32();
    s.server_type    = r.u16();
    s.rss_bytes      = r.u64();
    s.server_port    = r.u16();
    s.uptime_seconds   = r.u64();
    s.packets_in       = r.u64();
    s.packets_out      = r.u64();
    s.bytes_in         = r.u64();
    s.bytes_out        = r.u64();
    s.disconnects      = r.u64();
    s.partial_connects = r.u64();
    s.operations       = r.u64();
    s.logged_errors    = r.u64();
    s.max_users        = r.u32();
    s.max_connections  = r.u32();
    s.max_workareas    = r.u32();
    s.max_tables       = r.u32();
    s.max_indexes      = r.u32();
    s.max_locks        = r.u32();

    std::uint32_t nu = r.u32();
    for (std::uint32_t i = 0; r.ok && i < nu; ++i) {
        mgmt::MgUser u;
        u.name         = r.str();
        u.address      = r.str();
        u.os_login     = r.str();
        u.conn_no      = r.u16();
        u.connected_at = get_tp(r);
        u.avg_cost_micros = r.u32();
        s.user_list.push_back(std::move(u));
    }
    std::uint32_t nt = r.u32();
    for (std::uint32_t i = 0; r.ok && i < nt; ++i) {
        mgmt::MgTable t;
        t.name      = r.str();
        t.user      = r.str();
        t.conn_no   = r.u16();
        t.open_mode = r.u16();
        t.lock_type = r.u16();
        s.table_list.push_back(std::move(t));
    }
    std::uint32_t nx = r.u32();
    for (std::uint32_t i = 0; r.ok && i < nx; ++i) {
        mgmt::MgIndex x;
        x.name       = r.str();
        x.tag        = r.str();
        x.expression = r.str();
        s.index_list.push_back(std::move(x));
    }
    std::uint32_t nl = r.u32();
    for (std::uint32_t i = 0; r.ok && i < nl; ++i) {
        mgmt::MgLock l;
        l.user    = r.str();
        l.conn_no = r.u16();
        l.recno   = r.u32();
        s.lock_list.push_back(std::move(l));
    }
    std::uint32_t nh = r.u32();
    for (std::uint32_t i = 0; r.ok && i < nh; ++i) {
        mgmt::MgThread t;
        t.thread_no = r.u32();
        t.opcode    = r.u16();
        t.user      = r.str();
        t.conn_no   = r.u16();
        t.os_login  = r.str();
        s.thread_list.push_back(std::move(t));
    }
    if (!r.ok)
        return openads::util::Error{1, 0, "corrupt mg snapshot", ""};
    return s;
}

}  // namespace openads::network
