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
#ifdef __APPLE__
        CHECK(::unlinkat(guard.descriptor(),guard.leaf_name().c_str(),0)==0);
#else
        CHECK(::unlink(pin.value().c_str())==0);
#endif
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

#include "engine/vf_path.h"
TEST_CASE("vf path spelling keeps canonical targets inside the selected root") {
    namespace fs=std::filesystem;
    using namespace openads::engine::vf;
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    const auto base=fs::temp_directory_path()/("openads-vf-path-"+std::to_string(stamp));
    struct Cleanup { fs::path p;~Cleanup(){std::error_code e;fs::remove_all(p,e);} } cleanup{base};
    fs::create_directories(base/"root"/"folder");fs::create_directories(base/"outside");
    std::ofstream(base/"root"/"folder"/"file")<<"inside";
    std::ofstream(base/"outside"/"file")<<"outside";
    CHECK(resolve_path((base/"root").string(),"."));
    CHECK(resolve_path((base/"root").string(),"folder/."));
    CHECK(resolve_path((base/"root").string(),"folder/new/child"));
    CHECK(resolve_path((base/"root").string(),"folder/*"));
    CHECK(resolve_path((base/"root").string(),"folder/../folder/file"));
    CHECK_FALSE(resolve_path((base/"root").string(),"../outside/file"));
    std::error_code ec;
    fs::create_symlink(base/"root"/"folder"/"file",base/"root"/"inside-link",ec);
    if(!ec) {
        CHECK(resolve_path((base/"root").string(),"inside-link"));
        CHECK(canonical_path(base/"root"/"inside-link",ec)==canonical_path(base/"root"/"folder"/"file",ec));
        PathGuard pin;CHECK(pin.pin((base/"root").string(),(base/"root"/"inside-link").string(),true));
    }
    ec.clear();fs::create_directory_symlink(base/"root"/"folder",base/"root"/"inside-dir",ec);
    if(!ec) {
        PathGuard pin;
        CHECK(pin.pin((base/"root").string(),(base/"root"/"inside-dir"/"file").string(),false));
    }
    ec.clear();fs::create_directory_symlink(base/"outside",base/"root"/"outside-dir",ec);
    if(!ec) {
        PathGuard pin;
        CHECK_FALSE(pin.pin((base/"root").string(),(base/"root"/"outside-dir"/"file").string(),false));
    }
    ec.clear();fs::create_symlink(base/"outside"/"file",base/"root"/"outside-link",ec);
    if(!ec)CHECK_FALSE(resolve_path((base/"root").string(),"outside-link"));
#ifdef _WIN32
    auto extended=fs::path(L"\\\\?\\"+fs::absolute(base/"root"/"folder"/"file").wstring());
    CHECK(canonical_path(extended,ec)==canonical_path(base/"root"/"folder"/"file",ec));
    CHECK(normalized_path(extended).is_absolute());
    CHECK(normalized_path(L"\\\\?\\C:\\data\\file")==fs::path(L"C:\\data\\file"));
    CHECK(normalized_path(L"\\\\?\\UNC\\server\\share\\file")==fs::path(L"\\\\server\\share\\file"));
#endif
}
