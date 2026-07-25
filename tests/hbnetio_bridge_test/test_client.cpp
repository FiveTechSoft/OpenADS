// ============================================================================
// hbnetio Bridge Test Client
// ============================================================================
// Connects to the test server and exercises all RPC functions.
// Compile on macOS: clang++ -std=c++17 -o test_client test_client.cpp -lpthread
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
#include <chrono>
#include <thread>

// RPC opcodes
enum : std::uint8_t {
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

static void put_u32(void* p, uint32_t v) {
    uint8_t* b = (uint8_t*)p;
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF);
    b[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint32_t get_u32(const void* p) {
    const uint8_t* b = (const uint8_t*)p;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

// Pack a string param: [u32 len][data]
static std::vector<uint8_t> pack_string(const std::string& s) {
    std::vector<uint8_t> r(4 + s.size());
    put_u32(r.data(), (uint32_t)s.size());
    memcpy(r.data() + 4, s.data(), s.size());
    return r;
}

// Pack a u32 param: [u32 value]
static std::vector<uint8_t> pack_u32(uint32_t v) {
    std::vector<uint8_t> r(4);
    put_u32(r.data(), v);
    return r;
}

// Pack params list: [u16 count][param1_len][param1_data]...
static std::vector<uint8_t> pack_params(
    const std::vector<std::vector<uint8_t>>& params) {
    std::vector<uint8_t> r;
    uint16_t count = (uint16_t)params.size();
    r.push_back((uint8_t)(count & 0xFF));
    r.push_back((uint8_t)((count >> 8) & 0xFF));
    for (auto& p : params) {
        uint32_t len = (uint32_t)p.size();
        r.push_back((uint8_t)(len & 0xFF));
        r.push_back((uint8_t)((len >> 8) & 0xFF));
        r.push_back((uint8_t)((len >> 16) & 0xFF));
        r.push_back((uint8_t)((len >> 24) & 0xFF));
        r.insert(r.end(), p.begin(), p.end());
    }
    return r;
}

// Send RPC call: [4-byte BE length][opcode][payload]
// payload = [u8 nameLen][name][packed_params]
static bool rpc_call(int sock, const std::string& name,
                      const std::vector<std::vector<uint8_t>>& params,
                      std::vector<uint8_t>& result) {
    // Build payload
    std::vector<uint8_t> payload;
    uint8_t nlen = (uint8_t)std::min(name.size(), (size_t)255);
    payload.push_back(nlen);
    payload.insert(payload.end(), name.begin(), name.begin() + nlen);
    auto pp = pack_params(params);
    payload.insert(payload.end(), pp.begin(), pp.end());

    // Build wire frame
    uint32_t frame_len = (uint32_t)payload.size();
    uint8_t hdr[5];
    hdr[0] = (uint8_t)((frame_len >> 24) & 0xFF);
    hdr[1] = (uint8_t)((frame_len >> 16) & 0xFF);
    hdr[2] = (uint8_t)((frame_len >> 8) & 0xFF);
    hdr[3] = (uint8_t)(frame_len & 0xFF);
    hdr[4] = RPC_CALL;

    if (!send_all(sock, hdr, 5)) return false;
    if (frame_len > 0 && !send_all(sock, payload.data(), frame_len)) return false;

    // Read reply: [4-byte BE length][opcode][result_data]
    uint8_t reply_hdr[5];
    if (!recv_all(sock, reply_hdr, 5)) return false;
    uint32_t reply_len = ((uint32_t)reply_hdr[0] << 24) |
                          ((uint32_t)reply_hdr[1] << 16) |
                          ((uint32_t)reply_hdr[2] << 8) |
                          (uint32_t)reply_hdr[3];
    uint8_t reply_opcode = reply_hdr[4];

    if (reply_len > 0) {
        result.resize(reply_len);
        if (!recv_all(sock, result.data(), reply_len)) return false;
    } else {
        result.clear();
    }

    printf("    Reply opcode=0x%02X data_len=%u\n", reply_opcode, (unsigned)result.size());
    return true;
}

// ---- Tests ------------------------------------------------------------------

static int tests_run = 0;
static int tests_passed = 0;

static void check(bool cond, const char* desc) {
    tests_run++;
    if (cond) {
        tests_passed++;
        printf("  [PASS] %s\n", desc);
    } else {
        printf("  [FAIL] %s\n", desc);
    }
}

int main(int argc, char* argv[]) {
    const char* server_ip = "10.2.0.2"; // Windows host on this LAN
    int port = 9999;

    if (argc > 1) server_ip = argv[1];
    if (argc > 2) port = atoi(argv[2]);

    printf("hbnetio Bridge Test Client\n");
    printf("Connecting to %s:%d...\n\n", server_ip, port);

    // Connect
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, server_ip, &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("ERROR: Cannot connect to %s:%d\n", server_ip, port);
        return 1;
    }
    printf("Connected!\n\n");

    // Test 1: server_version
    printf("Test 1: server_version\n");
    {
        std::vector<uint8_t> result;
        auto t0 = std::chrono::steady_clock::now();
        bool ok = rpc_call(sock, "server_version", {}, result);
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        check(ok, "rpc_call succeeded");
        check(result.size() > 4, "result has data");

        // Decode string: skip u32 length prefix
        if (result.size() > 4) {
            uint32_t slen = get_u32(result.data());
            if (4 + slen <= result.size()) {
                std::string ver((char*)result.data() + 4, slen);
                printf("    Version: \"%s\"\n", ver.c_str());
                check(ver.find("hbnetio-bridge-test") != std::string::npos,
                      "version contains expected string");
            }
        }
        printf("    Round-trip: %lld µs\n\n", (long long)ms);
    }

    // Test 2: echo
    printf("Test 2: echo\n");
    {
        std::string msg = "Hello from Windows test server!";
        auto packed = pack_string(msg);
        std::vector<uint8_t> result;
        bool ok = rpc_call(sock, "echo", {packed}, result);
        check(ok, "echo call succeeded");

        if (result.size() > 4) {
            uint32_t slen = get_u32(result.data());
            if (4 + slen <= result.size()) {
                std::string echoed((char*)result.data() + 4, slen);
                printf("    Echoed: \"%s\"\n", echoed.c_str());
                check(echoed == msg, "echo matches input");
            }
        }
    }
    printf("\n");

    // Test 3: add
    printf("Test 3: add(100, 200, 300)\n");
    {
        auto p1 = pack_u32(100);
        auto p2 = pack_u32(200);
        auto p3 = pack_u32(300);
        std::vector<uint8_t> result;
        bool ok = rpc_call(sock, "add", {p1, p2, p3}, result);
        check(ok, "add call succeeded");

        if (result.size() >= 4) {
            uint32_t sum = get_u32(result.data());
            printf("    Result: %u\n", sum);
            check(sum == 600, "sum is 600");
        }
    }
    printf("\n");

    // Test 4: server_time
    printf("Test 4: server_time\n");
    {
        std::vector<uint8_t> result;
        bool ok = rpc_call(sock, "server_time", {}, result);
        check(ok, "server_time call succeeded");

        if (result.size() >= 8) {
            uint64_t server_ts = 0;
            for (int i = 0; i < 8; ++i)
                server_ts |= ((uint64_t)result[i]) << (i * 8);

            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            uint64_t client_ts = (uint64_t)now;

            printf("    Server time: %llu\n", (unsigned long long)server_ts);
            printf("    Client time: %llu\n", (unsigned long long)client_ts);
            int64_t diff = (int64_t)server_ts - (int64_t)client_ts;
            printf("    Drift: %lld seconds\n", (long long)diff);
            check(diff > -10 && diff < 10, "time drift < 10 seconds");
        }
    }
    printf("\n");

    // Test 5: function not found
    printf("Test 5: nonexistent function\n");
    {
        std::vector<uint8_t> result;
        bool ok = rpc_call(sock, "nonexistent_xyz", {}, result);
        check(ok, "call returned (even for missing func)");
        check(result.empty(), "result is empty for missing function");
    }
    printf("\n");

    // Test 6: performance — 100 round-trips
    printf("Test 6: Performance (100 echo round-trips)\n");
    {
        auto packed = pack_string("bench");
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 100; ++i) {
            std::vector<uint8_t> result;
            if (!rpc_call(sock, "echo", {packed}, result)) {
                check(false, "round-trip succeeded");
                break;
            }
        }
        auto t1 = std::chrono::steady_clock::now();
        auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        printf("    100 round-trips in %lld µs (avg %lld µs/rtt)\n",
               (long long)total_us, (long long)(total_us / 100));
        check(total_us > 0, "bench completed");
    }
    printf("\n");

    close(sock);

    // Summary
    printf("========================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);
    printf("========================================\n");

    return (tests_passed == tests_run) ? 0 : 1;
}