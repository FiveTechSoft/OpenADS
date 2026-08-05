// Repro for the Russoft ".DAT" companion-path bug in the ADI driver.
//
// The Russoft ERP keeps ADT-format data in files with a .DAT extension
// (ExtFile='.DAT') and a sibling .ADI compound index. The ADI driver used to
// derive the companion data path by blindly swapping the index extension to
// ".adt" (adt_path_for). For a .DAT table that file does not exist, so every
// implicit-path code path (list_tags / open_named / add_tag) failed with
// ERROR_FILE_NOT_FOUND, surfaced as ADSCDX/5103 "CreateFileA".
//
// The first tag survived because AdsCreateIndex61 passes the real table path
// (t->path()) into AdiIndex::create via CreateParams::adt_path. The SECOND tag
// took the `exists` branch, which called list_tags WITHOUT the data path and
// crashed. This test creates an ADT table on disk with a .DAT extension and
// adds a second (distinct-field) tag — it must succeed end to end.

#include "doctest.h"
#include "drivers/adi/adi_index.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

TEST_CASE("ADI: second tag on an ADT table stored as .DAT (Russoft convention)") {
    fs::path tmp = fs::temp_directory_path() / "openads_adi_dat_ext";
    { std::error_code ec; fs::create_directories(tmp, ec); }
    { std::error_code ec;
      fs::remove(tmp / "raddao.adt", ec);
      fs::remove(tmp / "raddao.DAT", ec);
      fs::remove(tmp / "raddao.adi", ec); }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    // Create an ADT-format table (AdsCreateTable forces a .adt extension).
    UNSIGNED8 tbl[]    = "raddao.adt";
    UNSIGNED8 flddef[] = "CCODIGOCON,Character,3;CDOCUMETRA,Character,8";
    ADSHANDLE hTable   = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);

    // Rename the data file to .DAT — the Russoft on-disk convention.
    { std::error_code ec;
      fs::rename(tmp / "raddao.adt", tmp / "raddao.DAT", ec);
      REQUIRE(!ec); }
    REQUIRE(fs::exists(tmp / "raddao.DAT"));
    REQUIRE_FALSE(fs::exists(tmp / "raddao.adt"));

    // Re-open the .DAT as an ADT table.
    UNSIGNED8 dat[] = "raddao.DAT";
    hTable = 0;
    REQUIRE(AdsOpenTable(hConn, dat, nullptr, ADS_ADT,
                         0, 0, 0, ADS_DEFAULT, &hTable) == AE_SUCCESS);

    UNSIGNED8 idxfile[] = "raddao.adi";

    // First tag — exercises AdiIndex::create (passes t->path() explicitly,
    // so it always worked).
    ADSHANDLE hIdx1 = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, (UNSIGNED8*)"ORD1",
                             (UNSIGNED8*)"CCODIGOCON", nullptr, nullptr,
                             0, 0, &hIdx1) == AE_SUCCESS);

    // Second tag — used to take the `exists` branch and crash with 5103
    // because list_tags derived raddao.adt (absent). Must now succeed.
    ADSHANDLE hIdx2 = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, (UNSIGNED8*)"ORD2",
                             (UNSIGNED8*)"CDOCUMETRA", nullptr, nullptr,
                             0, 0, &hIdx2) == AE_SUCCESS);

    // The .adi must now hold two distinct tags. list_tags is also called with
    // only the .adi path here, which exercises the adt_path_for .DAT fallback.
    auto tags = openads::drivers::adi::AdiIndex::list_tags(
        (tmp / "raddao.adi").string());
    REQUIRE(tags);
    CHECK(tags.value().size() == 2u);

    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);

    { std::error_code ec; fs::remove_all(tmp, ec); }
}
