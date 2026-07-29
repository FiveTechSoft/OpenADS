// Regression: SQL against an ADT table whose file is named ".DAT".
//
// Reported by RusSoft (2026-07-28). Their ERP names every table .DAT
// regardless of format, and after migrating a company to ADT every
// `SELECT ... FROM [articulo.dat] WHERE UPPER(a.creffacart) <> 'N'`
// failed with 7200 / "Column not found: creffacart", while the very same
// table opened fine through AdsOpenTable.
//
// Root cause: Harbour's contrib/rddads forwards the statement table type
// to AdsStmtSetTableType ONLY for ADS_CDX and ADS_VFP -- an ADS_ADT
// statement never reaches the engine, because on the real Advantage
// engine a statement already defaults to ADT. OpenADS defaults to CDX,
// so the ADT file was opened with the CDX driver, its header parsed as a
// DBF one, and every referenced column came back missing. The extension
// could not disambiguate either: ".dat" names no format.
//
// Fix: Connection::resolve_table_file sniffs the file header ("Advantage
// Table") and aligns the driver with what is actually on disk.
//
// The test deliberately does NOT call AdsStmtSetTableType -- that is the
// whole point: it reproduces what a Harbour client can express.

#include "doctest.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Run SQL on a statement created with the DEFAULT table type, i.e. the
// engine gets no hint at all about ADT vs DBF.
UNSIGNED32 run_sql_untyped(ADSHANDLE hConn, const std::string& sql,
                           ADSHANDLE* phCursor) {
    ADSHANDLE hStmt = 0;
    UNSIGNED32 rc = AdsCreateSQLStatement(hConn, &hStmt);
    if (rc != AE_SUCCESS) return rc;
    std::vector<UNSIGNED8> q(sql.size() + 1, 0);
    std::memcpy(q.data(), sql.c_str(), sql.size());
    rc = AdsExecuteSQLDirect(hStmt, q.data(), phCursor);
    AdsCloseSQLStatement(hStmt);
    return rc;
}

// Create a table of `type`, then rename its data file to <stem>.DAT --
// the RusSoft on-disk convention. Returns the .DAT name.
std::string make_table_as_dat(ADSHANDLE hConn, const fs::path& dir,
                              const char* stem, UNSIGNED16 type) {
    const std::string created =
        std::string(stem) + (type == ADS_ADT ? ".adt" : ".dbf");
    const std::string dat = std::string(stem) + ".DAT";

    std::error_code ec;
    fs::remove(dir / created, ec);
    fs::remove(dir / dat, ec);

    std::vector<UNSIGNED8> tbl(created.begin(), created.end());
    tbl.push_back(0);
    UNSIGNED8 flddef[] = "CCODIGOCON,Character,3;CREFFACART,Character,1";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsCreateTable(hConn, tbl.data(), nullptr, type, ADS_ANSI,
                           0, 0, 0, flddef, &hTable) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);

    fs::rename(dir / created, dir / dat, ec);
    REQUIRE(!ec);
    REQUIRE(fs::exists(dir / dat));
    REQUIRE_FALSE(fs::exists(dir / created));
    return dat;
}

ADSHANDLE connect_to(const fs::path& dir) {
    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);
    return hConn;
}

}  // namespace

TEST_CASE("SQL on an ADT table named .DAT resolves its columns") {
    fs::path tmp = fs::temp_directory_path() / "openads_adt_dat_sql";
    { std::error_code ec; fs::create_directories(tmp, ec); }

    ADSHANDLE hConn = connect_to(tmp);
    const std::string dat = make_table_as_dat(hConn, tmp, "artadt", ADS_ADT);

    SUBCASE("SELECT * finds the ADT columns") {
        ADSHANDLE hCur = 0;
        UNSIGNED32 rc =
            run_sql_untyped(hConn, "select * from [" + dat + "]", &hCur);
        REQUIRE(rc == AE_SUCCESS);

        UNSIGNED16 ncols = 0;
        REQUIRE(AdsGetNumFields(hCur, &ncols) == AE_SUCCESS);
        CHECK(ncols == 2);

        UNSIGNED8  fname[64]{};
        UNSIGNED16 fnlen = sizeof(fname);
        REQUIRE(AdsGetFieldName(hCur, 2, fname, &fnlen) == AE_SUCCESS);
        CHECK(std::string(reinterpret_cast<char*>(fname)) == "CREFFACART");
        AdsCloseTable(hCur);
    }

    SUBCASE("WHERE on a column no longer reports 7200 column-not-found") {
        // This is the exact shape of the failing ERP query.
        ADSHANDLE hCur = 0;
        UNSIGNED32 rc = run_sql_untyped(
            hConn,
            "select * from [" + dat + "] as a where upper(a.creffacart) <> 'N'",
            &hCur);
        CHECK(rc == AE_SUCCESS);
        if (rc == AE_SUCCESS) AdsCloseTable(hCur);
    }

    AdsDisconnect(hConn);
}

TEST_CASE("SQL on a DBF table named .DAT keeps working") {
    // Guard against the fix flipping the other way: a DBF container with
    // the same .DAT extension must still open with the CDX driver, both
    // when the statement says nothing and when it says ADT.
    fs::path tmp = fs::temp_directory_path() / "openads_dbf_dat_sql";
    { std::error_code ec; fs::create_directories(tmp, ec); }

    ADSHANDLE hConn = connect_to(tmp);
    const std::string dat = make_table_as_dat(hConn, tmp, "artdbf", ADS_CDX);

    ADSHANDLE hCur = 0;
    UNSIGNED32 rc = run_sql_untyped(
        hConn,
        "select * from [" + dat + "] as a where upper(a.creffacart) <> 'N'",
        &hCur);
    CHECK(rc == AE_SUCCESS);
    if (rc == AE_SUCCESS) AdsCloseTable(hCur);

    // Same file, but the caller explicitly (and wrongly) claims ADT.
    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == AE_SUCCESS);
    REQUIRE(AdsStmtSetTableType(hStmt, ADS_ADT) == AE_SUCCESS);
    const std::string sql = "select * from [" + dat + "]";
    std::vector<UNSIGNED8> q(sql.size() + 1, 0);
    std::memcpy(q.data(), sql.c_str(), sql.size());
    ADSHANDLE hCur2 = 0;
    CHECK(AdsExecuteSQLDirect(hStmt, q.data(), &hCur2) == AE_SUCCESS);
    if (hCur2) AdsCloseTable(hCur2);
    AdsCloseSQLStatement(hStmt);

    AdsDisconnect(hConn);
}
