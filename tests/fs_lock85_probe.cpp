// Independent processes must contend through real OS byte-range locks.
#include "engine/server_fs.h"
#include <filesystem>
#include <chrono>
#include <thread>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <string>
using namespace openads::engine;
namespace fs = std::filesystem;
int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "hold") {
        auto f = fs_open(argv[2], 2, false);
        if (!f) return 20;
        auto v = fs_lock_step(*f.value(), 128, 16, 0, 0);
        if (!v || !v.value()) return 21;
        std::ofstream marker(argv[3]); marker << "ready"; marker.close();
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        return 0;
    }
    if (argc == 5 && std::string(argv[1]) == "child") {
        auto f = fs_open(argv[2], 2, false);
        if (!f) return 10;
        const auto type = std::string(argv[3]) == "shared" ? 0x100 : 0;
        auto r = fs_lock_step(*f.value(), 0, 16, static_cast<std::uint16_t>(type), 0);
        if (!r) return 11;
        const bool want = std::string(argv[4]) == "free";
        return ( r.value() != 0 ) == want ? 0 : 12;
    }
    auto dir = fs::temp_directory_path() / ("openads-lock85-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    auto path = dir / "lock.bin";
    auto f = fs_open(path.string(), 2, true);
    if (!f) return 1;
    auto child = [&](const char* kind, const char* expected) {
        // ctest registers the binary with forward slashes; cmd.exe rejects
        // that in the child command line, so use native separators.
        std::string cmd = "\"" + fs::absolute(argv[0]).make_preferred().string() + "\" child \"" +
                          path.string() + "\" " + kind + " " + expected;
#if defined(_WIN32)
        // cmd.exe strips a leading quote plus the last quote on the line,
        // mangling the inner paths; an outer quote pair keeps them intact
        // (same pattern as the holder spawn in abi_oads_file_funcs_test).
        cmd = "\"" + cmd + "\"";
#endif
        return std::system(cmd.c_str()) == 0;
    };
    auto locked = fs_lock_step(*f.value(),0,16,0,0);
    if (!locked || !locked.value() || !child("exclusive","busy") ||
        !child("shared","busy")) return 2;
    if (!fs_lock_step(*f.value(),0,16,0,1) || !child("exclusive","free")) return 3;
    auto shared = fs_lock_step(*f.value(),0,16,0x100,0);
    if (!shared || !shared.value() || !child("shared","free") ||
        !child("exclusive","busy")) return 4;
    f.value().reset();
    if (!child("exclusive","free")) return 5;
    fs::remove_all(dir);
    std::cout << "FS-LOCK85-CROSS-PROCESS-PASS\n";
    return 0;
}
