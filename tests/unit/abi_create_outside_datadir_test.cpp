// A create with an absolute path whose directory exists writes THERE, even
// when that directory is outside data_dir_.
//
// #142 fixed the case where the absolute path sits under data_dir_. The other
// half is an application staging work tables somewhere else and reading them
// back by the same absolute name — the ERP that reported #142 builds scratch
// copies under the TEMP drive. Folding those produced a path whose intermediate
// directories nobody creates, so the create failed and the caller could never
// find its table.
//
// The fold still applies to a path whose parent does NOT exist (a client name
// that means nothing on the server) and to a bare drive-root name — see
// abi_create_table_test, which pins the drive-root behaviour.

#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("create: absolute path outside data_dir with an existing parent") {
    fs::path data = fs::temp_directory_path() / "openads_create_out_data";
    fs::path work = fs::temp_directory_path() / "openads_create_out_work";
    {
        std::error_code ec;
        fs::remove_all(data, ec); fs::remove_all(work, ec);
        fs::create_directories(data, ec); fs::create_directories(work, ec);
    }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, data.string().c_str(), data.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 flddef[] = "CCODIGO,Character,10";

    SUBCASE("existing directory outside data_dir is honoured") {
        const fs::path target = work / "scratch.dbf";
        std::string tname = target.string();
        std::vector<UNSIGNED8> nbuf(tname.begin(), tname.end());
        nbuf.push_back(0);

        ADSHANDLE hTable = 0;
        REQUIRE(AdsCreateTable(hConn, nbuf.data(), nullptr, ADS_CDX, ADS_ANSI,
                               0, 0, 0, flddef, &hTable) == AE_SUCCESS);
        REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);

        // Written where the caller asked, not folded under data_dir_.
        CHECK(fs::exists(target));
        CHECK_FALSE(fs::exists(data / "scratch.dbf"));

        // And it reopens by the same absolute name.
        ADSHANDLE hT2 = 0;
        REQUIRE(AdsOpenTable(hConn, nbuf.data(), nullptr, ADS_CDX, ADS_ANSI,
                             ADS_READONLY, ADS_COMPATIBLE_LOCKING,
                             ADS_DEFAULT, &hT2) == AE_SUCCESS);
        AdsCloseTable(hT2);
    }

    SUBCASE("non-existent parent is still folded away from the caller's path") {
        // Folding is unchanged for a path the server cannot honour. It does not
        // land at the caller's location; whether the folded location is itself
        // creatable depends on its own intermediate directories (here they do
        // not exist either, so the create reports an error) — that behaviour is
        // pre-existing and not what this test is about.
        const fs::path target = work / "nope" / "deep" / "folded.dbf";
        std::string tname = target.string();
        std::vector<UNSIGNED8> nbuf(tname.begin(), tname.end());
        nbuf.push_back(0);

        ADSHANDLE hTable = 0;
        if (AdsCreateTable(hConn, nbuf.data(), nullptr, ADS_CDX, ADS_ANSI,
                           0, 0, 0, flddef, &hTable) == AE_SUCCESS) {
            AdsCloseTable(hTable);
        }
        CHECK_FALSE(fs::exists(target));
    }

    AdsDisconnect(hConn);
    {
        std::error_code ec;
        fs::remove_all(data, ec); fs::remove_all(work, ec);
    }
}
