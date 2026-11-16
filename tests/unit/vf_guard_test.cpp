#include "doctest.h"
#include "engine/vf_guard.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#ifndef _WIN32
#include <unistd.h>
#include <sys/stat.h>
TEST_CASE("vf metadata pins survive parent and leaf substitution") {
    namespace fs=std::filesystem;
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    const auto base=fs::temp_directory_path()/("openads-vf-pin-"+std::to_string(stamp));
    struct Cleanup { fs::path p;~Cleanup(){std::error_code ec;fs::remove_all(p,ec);} } cleanup{base};
    const auto root=base/"root",outside=base/"outside";
    fs::create_directories(root/"parent");fs::create_directories(outside);
    std::ofstream(root/"parent"/"file")<<"inside";std::ofstream(outside/"file")<<"outside";
    {
        openads::engine::vf::PathGuard guard;
        const auto pin=guard.pin(root.string(),(root/"parent"/"file").string(),false);
        REQUIRE(pin);
        fs::rename(root/"parent",root/"saved");fs::create_directory_symlink(outside,root/"parent");
        CHECK(::unlink(pin.value().c_str())==0);
        CHECK(fs::exists(outside/"file"));CHECK_FALSE(fs::exists(root/"saved"/"file"));
    }
    {
        openads::engine::vf::PathGuard guard;
        CHECK_FALSE(guard.pin(root.string(),(root/"parent"/"file").string(),true));
        CHECK_FALSE(guard.pin(root.string(),(outside/"file").string(),false));
    }
    fs::remove(root/"parent");fs::rename(root/"saved",root/"parent");
    std::ofstream(root/"parent"/"file")<<"inside";
    {
        openads::engine::vf::PathGuard guard;
        const auto pin=guard.pin(root.string(),(root/"parent"/"file").string(),true);
        REQUIRE(pin);
        fs::rename(root/"parent"/"file",root/"parent"/"saved");
        fs::create_symlink(outside/"file",root/"parent"/"file");
        CHECK(::chmod(pin.value().c_str(),0400)==0);
        struct stat st{};REQUIRE(::stat((outside/"file").c_str(),&st)==0);
        CHECK((st.st_mode&0200)!=0);
    }
}
#endif
