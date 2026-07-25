// ============================================================================
// hbnetio Bridge Test Server
// ============================================================================
// Minimal TCP server that tests the RPC bridge wire protocol.
// Listens for RPC calls on port 9999, dispatches built-in functions.
//
// Usage: test_server.exe [port]
// ============================================================================

#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <string>
#include <vector>
#include <map>
#include <functional>

// RPC opcodes (matching rpc_bridge.h)
enum : std::uint8_t {
    RPC_CALL        = 0xF0,
    RPC_CALL_ACK    = 0xF1,
    RPC_PROC        = 0xF2,
    RPC_PROC_ACK    = 0xF3,
    RPC_STREAM_DATA = 0xFA,
};

// ---- Helpers ----------------------------------------------------------------
static bool send_all(SOCKET s, const void* buf, int len) {
    const char* p = (const char*)buf;
    while (len > 0) {
        int n = send(s, p, len, 0);
        if (n <= 0) return false;
        p += n; len -= n;
    }
    return true;
}

static bool recv_all(SOCKET s, void* buf, int len) {
    char* p = (char*)buf;
    while (len > 0) {
        int n = recv(s, p, len, 0);
        if (n <= 0) return false;
        p += n; len -= n;
    }
    return true;
}

// Little-endian helpers
static std::uint32_t get_u32(const void* p) {
    const std::uint8_t* b = (const std::uint8_t*)p;
    return (std::uint32_t)b[0] | ((std::uint32_t)b[1] << 8) |
           ((std::uint32_t)b[2] << 16) | ((std::uint32_t)b[3] << 24);
}

static void put_u32(void* p, std::uint32_t v) {
    std::uint8_t* b = (std::uint8_t*)p;
    b[0] = (std::uint8_t)(v & 0xFF);
    b[1] = (std::uint8_t)((v >> 8) & 0xFF);
    b[2] = (std::uint8_t)((v >> 16) & 0xFF);
    b[3] = (std::uint8_t)((v >> 24) & 0xFF);
}

// Pack a string: [u32 len][data]
static std::vector<std::uint8_t> pack_string(const std::string& s) {
    std::vector<std::uint8_t> r(4 + s.size());
    put_u32(r.data(), (std::uint32_t)s.size());
    memcpy(r.data() + 4, s.data(), s.size());
    return r;
}

// ---- Built-in functions -----------------------------------------------------

// echo: returns the first parameter back
static std::vector<std::uint8_t> builtin_echo(
    const std::vector<std::vector<std::uint8_t>>& params) {
    if (params.empty()) return pack_string("(empty)");
    return params[0]; // return raw bytes of first param
}

// server_version: returns version string
static std::vector<std::uint8_t> builtin_version(
    const std::vector<std::vector<std::uint8_t>>&) {
    return pack_string("OpenADS hbnetio-bridge-test v1.0");
}

// server_time: returns current epoch seconds as u64
static std::vector<std::uint8_t> builtin_time(
    const std::vector<std::vector<std::uint8_t>>&) {
    auto now = std::chrono::system_clock::now();
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    std::uint64_t v = (std::uint64_t)secs;
    std::vector<std::uint8_t> r(8);
    for (int i = 0; i < 8; ++i)
        r[i] = (std::uint8_t)((v >> (i * 8)) & 0xFF);
    return r;
}

// add: sums all u32 params
static std::vector<std::uint8_t> builtin_add(
    const std::vector<std::vector<std::uint8_t>>& params) {
    std::uint32_t sum = 0;
    for (auto& p : params) {
        if (p.size() >= 4) sum += get_u32(p.data());
    }
    std::vector<std::uint8_t> r(4);
    put_u32(r.data(), sum);
    return r;
}

// ---- Unpack params ----------------------------------------------------------

static bool unpack_params(const std::uint8_t* data, std::size_t len,
                           std::vector<std::vector<std::uint8_t>>& out) {
    if (len < 2) return false;
    std::uint16_t count = (std::uint16_t)data[0] | ((std::uint16_t)data[1] << 8);
    std::size_t off = 2;
    for (std::uint16_t i = 0; i < count; ++i) {
        if (off + 4 > len) return false;
        std::uint32_t plen = get_u32(data + off);
        off += 4;
        if (off + plen > len) return false;
        out.emplace_back(data + off, data + off + plen);
        off += plen;
    }
    return true;
}

// Unpack function name from RPC_CALL frame: [u8 nameLen][name][packed_params]
static std::string unpack_name(const std::uint8_t* data, std::size_t len) {
    if (len < 1) return "";
    std::uint8_t nlen = data[0];
    if (1 + nlen > len) return "";
    return std::string((const char*)data + 1, nlen);
}

// ---- Main -------------------------------------------------------------------

int main(int argc, char* argv[]) {
    int port = 9999;
    if (argc > 1) port = atoi(argv[1]);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int yes = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((u_short)port);

    if (bind(listen_sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("bind failed: %d\n", WSAGetLastError());
        return 1;
    }
    listen(listen_sock, 5);
    printf("hbnetio Bridge Test Server listening on port %d\n", port);
    printf("Registered functions: echo, server_version, server_time, add\n");
    printf("Waiting for connections...\n");

    // Registry
    using Func = std::function<std::vector<std::uint8_t>(
        const std::vector<std::vector<std::uint8_t>>&)>;
    std::map<std::string, Func> funcs;
    funcs["echo"] = builtin_echo;
    funcs["server_version"] = builtin_version;
    funcs["server_time"] = builtin_time;
    funcs["add"] = builtin_add;

    while (true) {
        sockaddr_in client_addr{};
        int addrlen = sizeof(client_addr);
        SOCKET client = accept(listen_sock, (sockaddr*)&client_addr, &addrlen);
        if (client == INVALID_SOCKET) continue;

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        printf("[%s:%d] Client connected\n", ip, ntohs(client_addr.sin_port));

        // Handle client in a loop
        bool ok = true;
        while (ok) {
            // Read OpenADS wire header: [4-byte BE length][1-byte opcode]
            std::uint8_t hdr[5];
            if (!recv_all(client, hdr, 5)) { ok = false; break; }

            std::uint32_t payload_len = ((std::uint32_t)hdr[0] << 24) |
                                        ((std::uint32_t)hdr[1] << 16) |
                                        ((std::uint32_t)hdr[2] << 8)  |
                                        (std::uint32_t)hdr[3];
            std::uint8_t opcode = hdr[4];

            std::vector<std::uint8_t> payload(payload_len);
            if (payload_len > 0) {
                if (!recv_all(client, payload.data(), payload_len)) { ok = false; break; }
            }

            printf("  Recv opcode=0x%02X payload_len=%u\n", opcode, payload_len);

            if (opcode == RPC_CALL) {
                // Parse: [u8 nameLen][name][packed_params]
                std::string name = unpack_name(payload.data(), payload.size());
                std::vector<std::vector<std::uint8_t>> params;
                if (payload.size() > 1 + name.size()) {
                    unpack_params(payload.data() + 1 + name.size(),
                                  payload.size() - 1 - name.size(), params);
                }

                printf("    RPC_CALL -> \"%s\" (%zu params)\n", name.c_str(), params.size());

                // Lookup function
                auto it = funcs.find(name);
                std::vector<std::uint8_t> result_data;
                if (it != funcs.end()) {
                    result_data = it->second(params);
                    printf("    Result: %zu bytes\n", result_data.size());
                } else {
                    printf("    Function not found: %s\n", name.c_str());
                }

                // Send RPC_CALL_ACK: [opcode=0xF1][result_data]
                std::uint32_t ack_len = (std::uint32_t)result_data.size();
                std::uint8_t ack_hdr[5];
                ack_hdr[0] = (std::uint8_t)((ack_len >> 24) & 0xFF);
                ack_hdr[1] = (std::uint8_t)((ack_len >> 16) & 0xFF);
                ack_hdr[2] = (std::uint8_t)((ack_len >> 8) & 0xFF);
                ack_hdr[3] = (std::uint8_t)(ack_len & 0xFF);
                ack_hdr[4] = RPC_CALL_ACK;

                if (!send_all(client, ack_hdr, 5)) { ok = false; break; }
                if (ack_len > 0) {
                    if (!send_all(client, result_data.data(), ack_len)) { ok = false; break; }
                }
            } else {
                printf("    Unknown opcode 0x%02X\n", opcode);
                ok = false;
            }
        }

        printf("[%s:%d] Client disconnected\n", ip, ntohs(client_addr.sin_port));
        closesocket(client);
    }

    closesocket(listen_sock);
    WSACleanup();
    return 0;
}