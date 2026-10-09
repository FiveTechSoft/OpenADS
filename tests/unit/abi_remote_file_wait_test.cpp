// Remote file calls must not hold the process registry lock while waiting.
#include "doctest.h"
#include "abi/runtime.h"
#include "network/client.h"
#include "network/server.h"
#include "openads/ace.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <future>
#include <fstream>
#include <mutex>
#include <thread>

namespace {
std::mutex gate_mu;
std::condition_variable gate_cv;
bool entered = false, released = false;
std::atomic<unsigned> gated_opcode{0};
void block_file_frame(const void*, std::uint8_t op, std::uint32_t,
                      std::size_t, std::uint8_t, std::size_t, long long) {
    if (op != gated_opcode.load()) return;
    std::unique_lock<std::mutex> lk(gate_mu);
    entered = true;
    gate_cv.notify_all();
    gate_cv.wait(lk, [] { return released; });
}
}

TEST_CASE("remote file waits release process mutex and retain close-race lifetime") {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "oads_file_wait";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    { std::ofstream f(dir / "input.txt"); f << "abcdefgh"; }
    openads::network::Server srv;
    srv.set_enable_file_func(true);
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    auto uri = "tcp://127.0.0.1:" + std::to_string(srv.port()) + "/" + dir.generic_string();
    ADSHANDLE conn = 0, file = 0;
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(uri.data()), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &conn) == 0);
    REQUIRE(AdsFOpen(conn, (UNSIGNED8*)"input.txt", 2, &file) == 0);
    unsigned opcode = 0;
    SUBCASE("open") { opcode = 0xF2; }
    SUBCASE("create") { opcode = 0xF4; }
    SUBCASE("read") { opcode = 0xF8; }
    SUBCASE("write") { opcode = 0xFA; }
    SUBCASE("seek") { opcode = 0xFC; }
    SUBCASE("close") { opcode = 0xF6; }
    { std::lock_guard<std::mutex> lk(gate_mu); entered = released = false; }
    gated_opcode.store(opcode);
    openads::network::set_frame_trace_hook(&block_file_frame);
    ADSHANDLE created = 0;
    auto pending = std::async(std::launch::async, [&] {
        UNSIGNED32 n = 0;
        char buf[8]{};
        switch (opcode) {
        case 0xF2: return AdsFOpen(conn, (UNSIGNED8*)"input.txt", 2, &created);
        case 0xF4: return AdsFCreate(conn, (UNSIGNED8*)"created.txt", 0, &created);
        case 0xF8: return AdsFRead(file, buf, 4, &n);
        case 0xFA: return AdsFWrite(file, "X", 1, &n);
        case 0xFC: return AdsFSeek(file, 0, 0, &n);
        default: return AdsFClose(file);
        }
    });
    bool reached;
    { std::unique_lock<std::mutex> lk(gate_mu);
      reached = gate_cv.wait_for(lk, std::chrono::seconds(3), [] { return entered; }); }
    CHECK(reached);
    auto& state = openads::abi::detail::state();
    bool available = state.mu.try_lock();
    CHECK(available);
    if (available) state.mu.unlock();
    // Another operation on the same handle must queue without grabbing the
    // process mutex. Closing cannot free the retained in-flight record.
    std::atomic<bool> contender_started{false};
    auto contender = std::async(std::launch::async, [&] {
        contender_started.store(true);
        return AdsFClose(file);
    });
    while (!contender_started.load()) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    available = state.mu.try_lock();
    CHECK(available);
    if (available) state.mu.unlock();
    CHECK(contender.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);
    gated_opcode.store(0);
    { std::lock_guard<std::mutex> lk(gate_mu); released = true; }
    gate_cv.notify_all();
    CHECK(pending.get() == 0);
    const auto close_rc = contender.get();
    if (opcode == 0xF6) CHECK(close_rc != 0);
    else CHECK(close_rc == 0);
    openads::network::set_frame_trace_hook(nullptr);
    if (created) CHECK(AdsFClose(created) == 0);
    UNSIGNED32 n = 123;
    CHECK(AdsFWrite(file, "x", 1, &n) != 0);
    CHECK(n == 0);
    CHECK(AdsDisconnect(conn) == 0);
    srv.stop();
    fs::remove_all(dir, ec);
}

TEST_CASE("remote file handles remain safe after disconnect") {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "oads_file_disconnect";
    fs::create_directories(dir);
    openads::network::Server srv;
    srv.set_enable_file_func(true);
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    auto uri = "tcp://127.0.0.1:" + std::to_string(srv.port()) + "/" + dir.generic_string();
    ADSHANDLE conn = 0, file = 0;
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(uri.data()), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &conn) == 0);
    REQUIRE(AdsFCreate(conn, (UNSIGNED8*)"gone.txt", 0, &file) == 0);
    CHECK(AdsDisconnect(conn) == 0);
    UNSIGNED32 n = 123;
    CHECK(AdsFWrite(file, "x", 1, &n) != 0);
    CHECK(n == 0);
    CHECK(AdsFClose(file) != 0);
    CHECK(AdsFClose(file) != 0);
    srv.stop();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

#include "network/session_thread_launch.h"
#include <unordered_map>
#include <system_error>
TEST_CASE("session thread admission survives launch failure and permits next client") {
    std::unordered_map<std::uint64_t, std::thread> threads;
    CHECK_THROWS_AS(openads::network::detail::register_session_thread(threads, 1, []() -> std::thread {
        throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
    }), std::system_error);
    CHECK(threads.empty());
    std::atomic<bool> ran{false};
    openads::network::detail::register_session_thread(threads, 2, [&] {
        return std::thread([&] { ran.store(true); });
    });
    REQUIRE(threads.size() == 1);
    threads.at(2).join();
    CHECK(ran.load());
}

TEST_CASE("disconnect queues behind remote file reply without process lock") {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "oads_file_inflight_disconnect";
    fs::create_directories(dir);
    openads::network::Server srv;
    srv.set_enable_file_func(true);
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    auto uri = "tcp://127.0.0.1:" + std::to_string(srv.port()) + "/" + dir.generic_string();
    ADSHANDLE conn = 0, file = 0;
    REQUIRE(AdsConnect60(reinterpret_cast<UNSIGNED8*>(uri.data()), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &conn) == 0);
    REQUIRE(AdsFCreate(conn, (UNSIGNED8*)"inflight.txt", 0, &file) == 0);
    { std::lock_guard<std::mutex> lk(gate_mu); entered = released = false; }
    gated_opcode.store(0xFA);
    openads::network::set_frame_trace_hook(&block_file_frame);
    auto writing = std::async(std::launch::async, [&] {
        UNSIGNED32 n = 0;
        return AdsFWrite(file, "X", 1, &n);
    });
    bool reached;
    { std::unique_lock<std::mutex> lk(gate_mu);
      reached = gate_cv.wait_for(lk, std::chrono::seconds(3), [] { return entered; }); }
    CHECK(reached);
    auto disconnecting = std::async(std::launch::async, [&] { return AdsDisconnect(conn); });
    CHECK(disconnecting.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
    auto& state = openads::abi::detail::state();
    const bool available = state.mu.try_lock();
    CHECK(available);
    if (available) state.mu.unlock();
    gated_opcode.store(0);
    { std::lock_guard<std::mutex> lk(gate_mu); released = true; }
    gate_cv.notify_all();
    CHECK(writing.get() == 0);
    CHECK(disconnecting.get() == 0);
    openads::network::set_frame_trace_hook(nullptr);
    CHECK(AdsFClose(file) != 0);
    srv.stop();
    std::error_code ec;
    fs::remove_all(dir, ec);
}
