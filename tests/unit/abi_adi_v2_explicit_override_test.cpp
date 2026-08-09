// AdsSetAdiV2() -- explicit in-process override for the ADI v2 tag layout.
//
// OPENADS_ADI_V2 is an environment variable, so it only reaches this DLL if
// set BEFORE the host process starts (confirmed empirically against a real
// Harbour host: an in-process putenv/SetEnvironmentVariable from the host
// does not propagate to this DLL's std::getenv(), even with both linked
// against the same UCRT). AdsSetAdiV2() sidesteps the environment entirely --
// a host flips an in-DLL flag directly, no process restart required.

#include "doctest.h"
#include "drivers/adi/adi_index.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {
// Always leave the override cleared (2) so later tests see the env var again
// -- AdiV2Scope-wrapped siblings rely on OPENADS_ADI_V2 actually controlling
// v2, and a leaked override=0/1 here would make that env var inert for the
// rest of the suite.
struct AdiV2OverrideGuard {
    ~AdiV2OverrideGuard() { AdsSetAdiV2(2); }
};

// Independent connection + table + tag per phase (own subfolder) instead of
// reusing one open table across phases: recreating the SAME tag name on a
// still-open handle doesn't guarantee a clean overwrite here, and this test
// only cares whether AdsSetAdiV2() steers list_tags(), not about reuse.
std::string one_tag_name(const fs::path& dir) {
    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[]    = "ovr.adt";
    UNSIGNED8 flddef[] = "CCODIGO,Character,10";
    ADSHANDLE hTable   = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);
    REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
    AdsSetString(hTable, (UNSIGNED8*)"CCODIGO", (UNSIGNED8*)"X0001", 5);
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);

    UNSIGNED8 idxfile[] = "ovr.adi";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, (UNSIGNED8*)"TCODIGO",
                             (UNSIGNED8*)"CCODIGO", nullptr, nullptr,
                             0, 0, &hIdx) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);

    auto tags = openads::drivers::adi::AdiIndex::list_tags(
        (dir / "ovr.adi").string(), (dir / "ovr.adt").string());
    REQUIRE(tags);
    REQUIRE(tags.value().size() == 1u);

    AdsDisconnect(hConn);
    return tags.value()[0];
}
} // namespace

TEST_CASE("AdsSetAdiV2: explicit override wins over OPENADS_ADI_V2") {
    AdiV2OverrideGuard _guard;
    fs::path base = fs::temp_directory_path() / "openads_adi_v2_override";

    // No override yet (and no OPENADS_ADI_V2 in this process) -> legacy,
    // list_tags() resolves by field name.
    {
        fs::path d = base / "off";
        std::error_code ec; fs::remove_all(d, ec); fs::create_directories(d, ec);
        CHECK(one_tag_name(d) == "CCODIGO");
    }

    // AdsSetAdiV2(1): explicit override, no env var involved.
    {
        REQUIRE(AdsSetAdiV2(1) == AE_SUCCESS);
        fs::path d = base / "on";
        std::error_code ec; fs::remove_all(d, ec); fs::create_directories(d, ec);
        CHECK(one_tag_name(d) == "TCODIGO");
    }

    // AdsSetAdiV2(0): explicit override back off.
    {
        REQUIRE(AdsSetAdiV2(0) == AE_SUCCESS);
        fs::path d = base / "off2";
        std::error_code ec; fs::remove_all(d, ec); fs::create_directories(d, ec);
        CHECK(one_tag_name(d) == "CCODIGO");
    }

    { std::error_code ec; fs::remove_all(base, ec); }
}
