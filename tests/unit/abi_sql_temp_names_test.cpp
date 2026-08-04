// Join / union / aggregate temps are ADT so result-column names survive in
// full. Guards the materialise_temp_adt() conversion — see
// docs/materialised-cursor-temps.md for the constraint matrix and the two
// prior regressions (#136, #146). The DBF temps these paths used to write
// truncated every name to 10 characters: aliases, merged R_<name> spellings
// and ADT source columns alike. Do not adjust the expectations.

#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string field_name(ADSHANDLE hCur, UNSIGNED16 n) {
    UNSIGNED8 buf[160] = {0};
    UNSIGNED16 len = sizeof(buf);
    if (AdsGetFieldName(hCur, n, buf, &len) != 0) return "<err>";
    return std::string(reinterpret_cast<const char*>(buf));
}

std::string field_text(ADSHANDLE hCur, const char* name) {
    UNSIGNED8 fld[64] = {0};
    std::strncpy(reinterpret_cast<char*>(fld), name, sizeof(fld) - 1);
    UNSIGNED8 val[128] = {0};
    UNSIGNED32 len = sizeof(val);
    if (AdsGetString(hCur, fld, val, &len, ADS_NONE) != 0) return "<err>";
    return std::string(reinterpret_cast<const char*>(val),
                       static_cast<std::size_t>(len));
}

void set_str(ADSHANDLE h, const char* f, const char* v) {
    UNSIGNED8 fld[64] = {0};
    std::strncpy(reinterpret_cast<char*>(fld), f, sizeof(fld) - 1);
    UNSIGNED8 val[64] = {0};
    std::strncpy(reinterpret_cast<char*>(val), v, sizeof(val) - 1);
    REQUIRE(AdsSetString(h, fld, val,
                         static_cast<UNSIGNED32>(std::strlen(v))) == 0);
}

// Two ADT tables whose column names all exceed the 11-byte DBF name slot —
// impossible to represent in a DBF temp without truncation.
void stage_tables(ADSHANDLE hConn) {
    UNSIGNED8 t1[] = "claims.adt";
    // AsciiNumeric (ADT type 2) mirrors what SAP writes for an N(10,2)
    // ADT column and keeps the declared scale in the descriptor; the
    // letter/name "Numeric" would map to a binary DOUBLE via adt_spec_for
    // and drop it.
    UNSIGNED8 d1[] =
        "Claim_Reference_Number,Character,14;"
        "Total_Amount_Billed,AsciiNumeric,10,2;"
        "Service_Start_Date,Date,8;"
        "Insurance_Carrier_Code,Character,6";
    ADSHANDLE h1 = 0;
    REQUIRE(AdsCreateTable(hConn, t1, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           d1, &h1) == 0);
    const struct { const char* ref; const char* amt;
                   const char* dt;  const char* ins; } rows[] = {
        {"CL-000000001", "10.50", "20240115", "AETNA"},
        {"CL-000000002", "20.25", "20240116", "AETNA"},
        {"CL-000000003", "5.00",  "20240117", "CIGNA"},
    };
    for (const auto& r : rows) {
        REQUIRE(AdsAppendRecord(h1) == 0);
        set_str(h1, "Claim_Reference_Number", r.ref);
        set_str(h1, "Total_Amount_Billed",    r.amt);
        set_str(h1, "Service_Start_Date",     r.dt);
        set_str(h1, "Insurance_Carrier_Code", r.ins);
        REQUIRE(AdsWriteRecord(h1) == 0);
    }
    REQUIRE(AdsCloseTable(h1) == 0);

    UNSIGNED8 t2[] = "carriers.adt";
    UNSIGNED8 d2[] =
        "Insurance_Carrier_Code,Character,6;"
        "Insurance_Carrier_Name,Character,30";
    ADSHANDLE h2 = 0;
    REQUIRE(AdsCreateTable(hConn, t2, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           d2, &h2) == 0);
    const struct { const char* code; const char* nm; } crs[] = {
        {"AETNA", "Aetna Health Insurance"},
        {"CIGNA", "Cigna Medical Group"},
    };
    for (const auto& r : crs) {
        REQUIRE(AdsAppendRecord(h2) == 0);
        set_str(h2, "Insurance_Carrier_Code", r.code);
        set_str(h2, "Insurance_Carrier_Name", r.nm);
        REQUIRE(AdsWriteRecord(h2) == 0);
    }
    REQUIRE(AdsCloseTable(h2) == 0);
}

bool dir_has_temp(const fs::path& dir, const char* prefix) {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.path().filename().string().rfind(prefix, 0) == 0) return true;
    }
    return false;
}

} // namespace

TEST_CASE("join/union/aggregate temps keep column names longer than 10 chars") {
    fs::path dir = fs::temp_directory_path() / "openads_temp_names";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    UNSIGNED8 srv[260] = {0};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    stage_tables(hConn);

    ADSHANDLE hStmt = 0;
    REQUIRE(AdsCreateSQLStatement(hConn, &hStmt) == 0);
    auto run = [&](const char* text) {
        UNSIGNED8 sql[512] = {0};
        std::memcpy(sql, text, std::strlen(text));
        ADSHANDLE hCur = 0;
        REQUIRE(AdsExecuteSQLDirect(hStmt, sql, &hCur) == 0);
        return hCur;
    };

    SUBCASE("2-table join: full merged names incl. untruncated R_ prefix") {
        ADSHANDLE hCur = run(
            "SELECT * FROM claims c INNER JOIN carriers r "
            "ON c.Insurance_Carrier_Code = r.Insurance_Carrier_Code");
        // Left columns keep their full names; right-side columns keep
        // their FULL merged R_ spelling (DBF truncated "R_Insurance_…"
        // to "R_Insuranc", which also made them unreachable by name).
        CHECK(field_name(hCur, 1) == "Claim_Reference_Number");
        CHECK(field_name(hCur, 2) == "Total_Amount_Billed");
        CHECK(field_name(hCur, 4) == "Insurance_Carrier_Code");
        CHECK(field_name(hCur, 5) == "R_Insurance_Carrier_Code");
        CHECK(field_name(hCur, 6) == "R_Insurance_Carrier_Name");
        REQUIRE(AdsGotoTop(hCur) == 0);
        // Values survive the temp: numeric keeps its N(10,2) scale, the
        // ADT date round-trips through the 4-byte JDN, and a long value
        // is reachable BY its full name — the part truncation broke.
        CHECK(field_text(hCur, "Total_Amount_Billed").find("10.50")
              != std::string::npos);
        CHECK(field_text(hCur, "Service_Start_Date") == "20240115");
        CHECK(field_text(hCur, "R_Insurance_Carrier_Name")
                  .find("Aetna Health") != std::string::npos);
        REQUIRE(AdsCloseTable(hCur) == 0);
    }

    SUBCASE("scalar aggregate: alias longer than 10 chars survives") {
        ADSHANDLE hCur = run(
            "SELECT SUM(Total_Amount_Billed) AS Total_Amount_For_All_Claims "
            "FROM claims");
        CHECK(field_name(hCur, 1) == "Total_Amount_For_All_Claims");
        REQUIRE(AdsGotoTop(hCur) == 0);
        CHECK(field_text(hCur, "Total_Amount_For_All_Claims").find("35.75")
              != std::string::npos);
        REQUIRE(AdsCloseTable(hCur) == 0);
    }

    SUBCASE("GROUP BY: long key name and long alias survive") {
        ADSHANDLE hCur = run(
            "SELECT Insurance_Carrier_Code, "
            "SUM(Total_Amount_Billed) AS Total_Amount_Per_Carrier "
            "FROM claims GROUP BY Insurance_Carrier_Code");
        CHECK(field_name(hCur, 1) == "Insurance_Carrier_Code");
        CHECK(field_name(hCur, 2) == "Total_Amount_Per_Carrier");
        REQUIRE(AdsGotoTop(hCur) == 0);   // AETNA group sorts first
        CHECK(field_text(hCur, "Insurance_Carrier_Code").find("AETNA")
              != std::string::npos);
        CHECK(field_text(hCur, "Total_Amount_Per_Carrier").find("30.75")
              != std::string::npos);
        REQUIRE(AdsCloseTable(hCur) == 0);
    }

    SUBCASE("UNION: long name survives and dedup still works") {
        ADSHANDLE hCur = run(
            "SELECT Claim_Reference_Number FROM claims "
            "UNION "
            "SELECT Claim_Reference_Number FROM claims");
        CHECK(field_name(hCur, 1) == "Claim_Reference_Number");
        UNSIGNED32 n = 0;
        REQUIRE(AdsGetRecordCount(hCur, ADS_IGNOREFILTERS, &n) == 0);
        CHECK(n == 3);   // UNION dedups the duplicated member
        REQUIRE(AdsCloseTable(hCur) == 0);
    }

    SUBCASE("join + aggregate: long alias survives") {
        ADSHANDLE hCur = run(
            "SELECT SUM(Total_Amount_Billed) AS Joined_Total_Amount_Billed "
            "FROM claims c INNER JOIN carriers r "
            "ON c.Insurance_Carrier_Code = r.Insurance_Carrier_Code");
        CHECK(field_name(hCur, 1) == "Joined_Total_Amount_Billed");
        REQUIRE(AdsGotoTop(hCur) == 0);
        CHECK(field_text(hCur, "Joined_Total_Amount_Billed").find("35.75")
              != std::string::npos);
        REQUIRE(AdsCloseTable(hCur) == 0);
    }

    SUBCASE("join + GROUP BY: long alias survives (_jgrp_ path)") {
        ADSHANDLE hCur = run(
            "SELECT SUM(Total_Amount_Billed) AS Grouped_Join_Total_Amount "
            "FROM claims c INNER JOIN carriers r "
            "ON c.Insurance_Carrier_Code = r.Insurance_Carrier_Code "
            "GROUP BY c.Insurance_Carrier_Code");
        CHECK(field_name(hCur, 1) == "Grouped_Join_Total_Amount");
        REQUIRE(AdsGotoTop(hCur) == 0);   // AETNA group sorts first
        CHECK(field_text(hCur, "Grouped_Join_Total_Amount").find("30.75")
              != std::string::npos);
        REQUIRE(AdsCloseTable(hCur) == 0);
    }

    SUBCASE("temps are deleted when their cursor closes (DBF era leaked)") {
        ADSHANDLE hCur = run(
            "SELECT SUM(Total_Amount_Billed) AS Total_Amount_For_All_Claims "
            "FROM claims");
        REQUIRE(AdsCloseTable(hCur) == 0);
        CHECK(!dir_has_temp(dir, "_agg_"));

        ADSHANDLE hJoin = run(
            "SELECT * FROM claims c INNER JOIN carriers r "
            "ON c.Insurance_Carrier_Code = r.Insurance_Carrier_Code");
        REQUIRE(AdsCloseTable(hJoin) == 0);
        CHECK(!dir_has_temp(dir, "_join_"));
    }

    REQUIRE(AdsCloseSQLStatement(hStmt) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
