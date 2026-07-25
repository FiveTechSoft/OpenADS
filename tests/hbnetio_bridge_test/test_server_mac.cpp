// ============================================================================
// hbnetio Bridge Test Server (macOS / POSIX)
// ============================================================================

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <chrono>

enum : uint8_t {
    RPC_CALL     = 0xF0,
    RPC_CALL_ACK = 0xF1,
};

static bool send_all(int s, const void* buf, size_t len) {
    const char* p = (const char*)buf;
    while (len > 0) {
        ssize_t n = send(s, p, len, 0);
        if (n <= 0) return false;
        p += n; len -= n;
    }
    return true;
}

static bool recv_all(int s, void* buf, size_t len) {
    char* p = (char*)buf;
    while (len > 0) {
        ssize_t n = recv(s, p, len, 0);
        if (n <= 0) return false;
        p += n; len -= n;
    }
    return true;
}

static uint32_t get_u32(const void* p) {
    const uint8_t* b = (const uint8_t*)p;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static void put_u32(void* p, uint32_t v) {
    uint8_t* b = (uint8_t*)p;
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF);
    b[3] = (uint8_t)((v >> 24) & 0xFF);
}

static std::vector<uint8_t> pack_string(const std::string& s) {
    std::vector<uint8_t> r(4 + s.size());
    put_u32(r.data(), (uint32_t)s.size());
    memcpy(r.data() + 4, s.data(), s.size());
    return r;
}

// Built-in functions
static std::vector<uint8_t> builtin_echo(
    const std::vector<std::vector<uint8_t>>& params) {
    if (params.empty()) return pack_string("(empty)");
    return params[0];
}

static std::vector<uint8_t> builtin_version(
    const std::vector<std::vector<uint8_t>>&) {
    return pack_string("OpenADS hbnetio-bridge-test v1.0");
}

static std::vector<uint8_t> builtin_time(
    const std::vector<std::vector<uint8_t>>&) {
    auto now = std::chrono::system_clock::now();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    uint64_t v = (uint64_t)secs;
    std::vector<uint8_t> r(8);
    for (int i = 0; i < 8; ++i)
        r[i] = (uint8_t)((v >> (i * 8)) & 0xFF);
    return r;
}

static std::vector<uint8_t> builtin_add(
    const std::vector<std::vector<uint8_t>>& params) {
    uint32_t sum = 0;
    for (auto& p : params) {
        if (p.size() >= 4) sum += get_u32(p.data());
    }
    std::vector<uint8_t> r(4);
    put_u32(r.data(), sum);
    return r;
}

static bool unpack_params(const uint8_t* data, size_t len,
                           std::vector<std::vector<uint8_t>>& out) {
    if (len < 2) return false;
    uint16_t count = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    size_t off = 2;
    for (uint16_t i = 0; i < count; ++i) {
        if (off + 4 > len) return false;
        uint32_t plen = get_u32(data + off);
        off += 4;
        if (off + plen > len) return false;
        out.emplace_back(data + off, data + off + plen);
        off += plen;
    }
    return true;
}

static std::string unpack_name(const uint8_t* data, size_t len) {
    if (len < 1) return "";
    uint8_t nlen = data[0];
    if (1 + nlen > len) return "";
    return std::string((const char*)data + 1, nlen);
}

int main(int argc, char* argv[]) {
    int port = 9999;
    if (argc > 1) port = atoi(argv[1]);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    listen(listen_fd, 5);
    printf("hbnetio Bridge Test Server (macOS) on port %d\n", port);
    printf("Functions: echo, server_version, server_time, add\n");
    printf("Waiting...\n");

    using Func = std::function<std::vector<uint8_t>(
        const std::vector<std::vector<uint8_t>>&)>;
    std::map<std::string, Func> funcs;
    funcs["echo"] = builtin_echo;
    funcs["server_version"] = builtin_version;
    funcs["server_time"] = builtin_time;
    funcs["add"] = builtin_add;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t addrlen = sizeof(client_addr);
        int client = accept(listen_fd, (sockaddr*)&client_addr, &addrlen);
        if (client < 0) continue;

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        printf("[%s:%d] Connected\n", ip, ntohs(client_addr.sin_port));

        bool ok = true;
        while (ok) {
            uint8_t hdr[5];
            if (!recv_all(client, hdr, 5)) { ok = false; break; }

            uint32_t payload_len = ((uint32_t)hdr[0] << 24) |
                                    ((uint32_t)hdr[1] << 16) |
                                    ((uint32_t)hdr[2] << 8) |
                                    (uint32_t)hdr[3];
            uint8_t opcode = hdr[4];

            std::vector<uint8_t> payload(payload_len);
            if (payload_len > 0) {
                if (!recv_all(client, payload.data(), payload_len)) { ok = false; break; }
            }

            printf("  opcode=0x%02X len=%u\n", opcode, payload_len);

            if (opcode == RPC_CALL) {
                std::string name = unpack_name(payload.data(), payload.size());
                std::vector<std::vector<uint8_t>> params;
                if (payload.size() > 1 + name.size()) {
                    unpack_params(payload.data() + 1 + name.size(),
                                  payload.size() - 1 - name.size(), params);
                }
                printf("    -> \"%s\" (%zu params)\n", name.c_str(), params.size());

                auto it = funcs.find(name);
                std::vector<uint8_t> result_data;
                if (it != funcs.end()) {
                    result_data = it->second(params);
                }

                uint32_t ack_len = (uint32_t)result_data.size();
                uint8_t ack_hdr[5];
                ack_hdr[0] = (uint8_t)((ack_len >> 24) & 0xFF);
                ack_hdr[1] = (uint8_t)((ack_len >> 16) & 0xFF);
                ack_hdr[2] = (uint8_t)((ack_len >> 8) & 0xFF);
                ack_hdr[3] = (uint8_t)(ack_len & 0xFF);
                ack_hdr[4] = RPC_CALL_ACK;

                if (!send_all(client, ack_hdr, 5)) { ok = false; break; }
                if (ack_len > 0) {
                    if (!send_all(client, result_data.data(), ack_len)) { ok = false; break; }
                }
            } else {
                ok = false;
            }
        }
        printf("[%s:%d] Disconnected\n", ip, ntohs(client_addr.sin_port));
        close(client);
    }
    close(listen_fd);
    return 0;
}