// Tests for AdsOpenIndex path resolution when the caller qualifies the index
// path with the same subdirectory component as the table's own parent path.
//
// Scenario (the "double-prefix" bug):
//   - Connection root:  <tmp>/
//   - Table path:       <tmp>/data/t.dbf  → table_dir = <tmp>/data
//   - Caller passes:    "data/t.cdx"      (same subdir prefix as table_dir)
//   - Naive join:       <tmp>/data/data/t.cdx  ← does not exist → error 5103
//   - Expected:         <tmp>/data/t.cdx  ← basename fallback succeeds
//
// We also verify the clean path (basename only, no prefix) still works.

#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Build a null-terminated UNSIGNED8 buffer from a std::string.
static void fill_buf(UNSIGNED8* buf, std::size_t buf_sz, const std::string& s) {
    std::memcpy(buf, s.c_str(), s.size() + 1 < buf_sz ? s.size() + 1 : buf_sz);
    buf[buf_sz - 1] = '\0';
}

// Create a minimal CDX table + single-tag index in `dir`, using base name
// `leaf` (without extension). Returns AE_SUCCESS (0) on success.
// On return *phTable is an open table handle in the connection rooted at `dir`.
static UNSIGNED32 setup_table_and_index(const fs::path& dir,
                                        const char* leaf,
                                        ADSHANDLE hConn,
                                        ADSHANDLE* phTable) {
    UNSIGNED8 srv[512]{};
    fill_buf(srv, sizeof(srv), dir.string());

    std::string dbf_name = std::string(leaf) + ".dbf";
    std::string cdx_name = std::string(leaf) + ".cdx";

    UNSIGNED8 tname[64]{};
    fill_buf(tname, sizeof(tname), dbf_name);

    UNSIGNED8 fields[] = "ID,N,5,0;NAME,C,10,0";
    ADSHANDLE hT = 0;
    UNSIGNED32 rc = AdsCreateTable(hConn, tname, nullptr,
                                   ADS_CDX, 0, 0, 0, 0, fields, &hT);
    if (rc != 0) return rc;

    // Create one index tag so the CDX file is written to disk.
    UNSIGNED8 bag[64]{};
    fill_buf(bag, sizeof(bag), cdx_name);
    UNSIGNED8 tag[]  = "BY_ID";
    UNSIGNED8 expr[] = "ID";
    ADSHANDLE hIdx = 0;
    rc = AdsCreateIndex61(hT, bag, tag, expr, nullptr, nullptr, 0, 0, &hIdx);
    if (rc != 0) { AdsCloseTable(hT); return rc; }

    *phTable = hT;
    return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// TEST 1: double-prefix — "data/t.cdx" when table_dir is already .../data/
// ---------------------------------------------------------------------------

TEST_CASE("AdsOpenIndex: double-prefix relative path falls back to basename") {
    const fs::path base = fs::temp_directory_path() / "openads_oi_dblpfx";
    const fs::path data = base / "data";

    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(data);

    // Phase 1 — create the table + index rooted inside data/.
    {
        UNSIGNED8 srv[512]{};
        fill_buf(srv, sizeof(srv), data.string());
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);

        ADSHANDLE hT = 0;
        REQUIRE(setup_table_and_index(data, "t", hConn, &hT) == 0);

        AdsCloseTable(hT);
        AdsDisconnect(hConn);
    }

    // Verify the files actually exist before proceeding.
    REQUIRE(fs::exists(data / "t.dbf"));
    REQUIRE(fs::exists(data / "t.cdx"));

    // Phase 2 — re-open from the PARENT directory and exercise the path fix.
    UNSIGNED8 srv[512]{};
    fill_buf(srv, sizeof(srv), base.string());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    // Open the table with a subdir-qualified path: "data/t.dbf"
    // → t->path() = <base>/data/t.dbf → table_dir = <base>/data
    UNSIGNED8 tbl[64]{};
    // Use platform-appropriate separator.  ADS/Windows accepts forward slash.
    fill_buf(tbl, sizeof(tbl), "data/t.dbf");
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hConn, tbl, nullptr, ADS_CDX,
                         0, 0, 0, ADS_DEFAULT, &hT) == 0);

    // Now call AdsOpenIndex with the same subdir prefix: "data/t.cdx"
    // Without the fix this would try <base>/data/data/t.cdx → error 5103.
    UNSIGNED8 idx_path[64]{};
    fill_buf(idx_path, sizeof(idx_path), "data/t.cdx");
    ADSHANDLE arr[8] = {0};
    UNSIGNED16 cap = 8;
    UNSIGNED32 rc = AdsOpenIndex(hT, idx_path, arr, &cap);
    INFO("AdsOpenIndex(\"data/t.cdx\") rc=" << rc);
    REQUIRE(rc == 0);
    CHECK(cap >= 1u);

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(base, ec);
}

// ---------------------------------------------------------------------------
// TEST 3: index in a different folder than the table
// ---------------------------------------------------------------------------
//
// Scenario (the user-reported ADSCDX/5103 bug):
//   - Connection root:  <tmp>/
//   - Table path:       <tmp>/data/t.dbf       -> table_dir = <tmp>/data
//   - Index lives in:   <tmp>/indexes/t.cdx
//   - Caller passes:    "indexes/t.cdx"        (relative to connection root)
//   - Old code:         table_dir/indexes/t.cdx -> does not exist -> error 5103
//   - Fixed code:       also tries conn_root/indexes/t.cdx -> finds it
//
// This covers ERP scenarios where indexes are kept in a dedicated folder
// separate from the data files, and the user always passes a qualified path.

TEST_CASE("AdsOpenIndex: index in different folder resolves via connection root") {
    const fs::path base    = fs::temp_directory_path() / "openads_oi_diffdir";
    const fs::path data    = base / "data";
    const fs::path indexes = base / "indexes";

    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(data);
    fs::create_directories(indexes);

    // Phase 1 - create the table in data/ and the index in indexes/.
    {
        UNSIGNED8 srv[512]{};
        fill_buf(srv, sizeof(srv), data.string());
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);

        // Create table in data/.
        UNSIGNED8 tname[64]{};
        fill_buf(tname, sizeof(tname), "t.dbf");
        UNSIGNED8 fields[] = "ID,N,5,0;NAME,C,10,0";
        ADSHANDLE hT = 0;
        REQUIRE(AdsCreateTable(hConn, tname, nullptr,
                               ADS_CDX, 0, 0, 0, 0, fields, &hT) == 0);

        // Create the index explicitly in the indexes/ directory.
        UNSIGNED8 bag[256]{};
        fill_buf(bag, sizeof(bag), (indexes / "t.cdx").string());
        UNSIGNED8 tag[]  = "BY_ID";
        UNSIGNED8 expr[] = "ID";
        ADSHANDLE hIdx = 0;
        UNSIGNED32 rc = AdsCreateIndex61(hT, bag, tag, expr,
                                         nullptr, nullptr, 0, 0, &hIdx);
        REQUIRE(rc == 0);
        AdsCloseIndex(hIdx);
        AdsCloseTable(hT);
        AdsDisconnect(hConn);
    }

    // Verify the files exist in their respective directories.
    REQUIRE(fs::exists(data    / "t.dbf"));
    REQUIRE(fs::exists(indexes / "t.cdx"));
    // Confirm the index is NOT next to the table (ensures the test is valid).
    REQUIRE_FALSE(fs::exists(data / "t.cdx"));

    // Phase 2 - re-open and exercise the connection-root path resolution.
    UNSIGNED8 srv[512]{};
    fill_buf(srv, sizeof(srv), base.string());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    // Open the table from the data/ subdirectory.
    UNSIGNED8 tbl[64]{};
    fill_buf(tbl, sizeof(tbl), "data/t.dbf");
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hConn, tbl, nullptr, ADS_CDX,
                         0, 0, 0, ADS_DEFAULT, &hT) == 0);

    // Call AdsOpenIndex with a path relative to the connection root.
    // "indexes/t.cdx" must resolve to <tmp>/indexes/t.cdx, NOT
    // <tmp>/data/indexes/t.cdx (which doesn't exist).
    UNSIGNED8 idx_path[256]{};
    fill_buf(idx_path, sizeof(idx_path), "indexes/t.cdx");
    ADSHANDLE arr[8] = {0};
    UNSIGNED16 cap = 8;
    UNSIGNED32 rc = AdsOpenIndex(hT, idx_path, arr, &cap);
    INFO("AdsOpenIndex(\"indexes/t.cdx\") rc=" << rc);
    REQUIRE(rc == 0);
    CHECK(cap >= 1u);

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(base, ec);
}
// ---------------------------------------------------------------------------
// TEST 2: clean path — basename only ("t.cdx") must still work
// ---------------------------------------------------------------------------

TEST_CASE("AdsOpenIndex: basename-only relative path still works") {
    const fs::path base = fs::temp_directory_path() / "openads_oi_basename";
    const fs::path data = base / "data";

    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(data);

    // Phase 1 — create table + index rooted inside data/.
    {
        UNSIGNED8 srv[512]{};
        fill_buf(srv, sizeof(srv), data.string());
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);

        ADSHANDLE hT = 0;
        REQUIRE(setup_table_and_index(data, "r", hConn, &hT) == 0);

        AdsCloseTable(hT);
        AdsDisconnect(hConn);
    }

    REQUIRE(fs::exists(data / "r.dbf"));
    REQUIRE(fs::exists(data / "r.cdx"));

    // Phase 2 — re-open from parent, use basename only for index.
    UNSIGNED8 srv[512]{};
    fill_buf(srv, sizeof(srv), base.string());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 tbl[64]{};
    fill_buf(tbl, sizeof(tbl), "data/r.dbf");
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(hConn, tbl, nullptr, ADS_CDX,
                         0, 0, 0, ADS_DEFAULT, &hT) == 0);

    // Basename only: "r.cdx" → table_dir / "r.cdx" = <base>/data/r.cdx ✓
    UNSIGNED8 idx_path[64]{};
    fill_buf(idx_path, sizeof(idx_path), "r.cdx");
    ADSHANDLE arr[8] = {0};
    UNSIGNED16 cap = 8;
    UNSIGNED32 rc = AdsOpenIndex(hT, idx_path, arr, &cap);
    INFO("AdsOpenIndex(\"r.cdx\") rc=" << rc);
    REQUIRE(rc == 0);
    CHECK(cap >= 1u);

    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    fs::remove_all(base, ec);
}
