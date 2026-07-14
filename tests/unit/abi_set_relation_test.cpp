// AdsSetRelation — parent→child work-area relation. Moving the parent's
// cursor re-seeks the child's controlling order to the key produced by
// evaluating the relation expression against the parent's current record.
// A miss leaves the child at EOF (Clipper / ACE dbSetRelation semantics).
#include "doctest.h"
#include "openads/ace.h"
#include "openads/error.h"
#include "network/server.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {
std::string child_data(ADSHANDLE hChild) {
    UNSIGNED8 field[8] = "DATA";
    UNSIGNED8 buf[32];
    UNSIGNED32 len = sizeof(buf);
    if (AdsGetString(hChild, field, buf, &len, 0) != openads::AE_SUCCESS)
        return "<err>";
    std::string s((char*)buf, len);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

// Count the records reachable from the child's current top under its active
// scope: GotoTop, then Skip(1) until EOF.
int child_visible_count(ADSHANDLE hChild) {
    if (AdsGotoTop(hChild) != openads::AE_SUCCESS) return -1;
    int n = 0;
    for (;;) {
        UNSIGNED16 eof = 0;
        if (AdsAtEOF(hChild, &eof) != openads::AE_SUCCESS) return -1;
        if (eof) break;
        ++n;
        if (AdsSkip(hChild, 1) != openads::AE_SUCCESS) return -1;
    }
    return n;
}
}  // namespace

TEST_CASE("AdsSetRelation drives a child by a character key as the parent moves") {
    const auto dir = fs::temp_directory_path() / "openads_setrelation_char";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == openads::AE_SUCCESS);

    // Parent: a list of keys (one CHAR field).
    UNSIGNED8 pdef[]  = "CODE,Character,4";
    UNSIGNED8 pname[] = "par";
    ADSHANDLE hP = 0;
    REQUIRE(AdsCreateTable(hConn, pname, nullptr, ADS_CDX, 0, 0, 0, 0, pdef, &hP)
            == openads::AE_SUCCESS);
    const char* codes[] = {"AAAA", "BBBB", "CCCC", "ZZZZ"};
    for (const char* c : codes) {
        REQUIRE(AdsAppendRecord(hP) == openads::AE_SUCCESS);
        UNSIGNED8 f[8] = "CODE";
        REQUIRE(AdsSetString(hP, f, (UNSIGNED8*)c, 4) == openads::AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hP) == openads::AE_SUCCESS);
    }

    // Child: CODE + DATA, indexed on CODE. "ZZZZ" is intentionally absent.
    UNSIGNED8 cdef[]  = "CODE,Character,4;DATA,Character,8";
    UNSIGNED8 cname[] = "chi";
    ADSHANDLE hC = 0;
    REQUIRE(AdsCreateTable(hConn, cname, nullptr, ADS_CDX, 0, 0, 0, 0, cdef, &hC)
            == openads::AE_SUCCESS);
    UNSIGNED8 bag[]  = "chi.cdx";
    UNSIGNED8 tag[]  = "BYCODE";
    UNSIGNED8 cexpr[] = "CODE";
    ADSHANDLE hCI = 0;
    REQUIRE(AdsCreateIndex61(hC, bag, tag, cexpr, nullptr, nullptr, 0, 0, &hCI)
            == openads::AE_SUCCESS);
    struct { const char* code; const char* data; } rows[] = {
        {"AAAA", "alpha"}, {"BBBB", "beta"}, {"CCCC", "gamma"},
    };
    for (auto& r : rows) {
        REQUIRE(AdsAppendRecord(hC) == openads::AE_SUCCESS);
        UNSIGNED8 fc[8] = "CODE";
        UNSIGNED8 fd[8] = "DATA";
        REQUIRE(AdsSetString(hC, fc, (UNSIGNED8*)r.code, 4) == openads::AE_SUCCESS);
        REQUIRE(AdsSetString(hC, fd, (UNSIGNED8*)r.data,
                             (UNSIGNED32)std::strlen(r.data)) == openads::AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hC) == openads::AE_SUCCESS);
    }

    // Relate child to parent on the CODE expression.
    UNSIGNED8 rexpr[] = "CODE";
    REQUIRE(AdsSetRelation(hP, hC, rexpr) == openads::AE_SUCCESS);

    // SetRelation positions the child against the parent's current record.
    REQUIRE(AdsGotoTop(hP) == openads::AE_SUCCESS);
    CHECK(child_data(hC) == "alpha");

    REQUIRE(AdsSkip(hP, 1) == openads::AE_SUCCESS);     // BBBB
    CHECK(child_data(hC) == "beta");

    REQUIRE(AdsSkip(hP, 1) == openads::AE_SUCCESS);     // CCCC
    CHECK(child_data(hC) == "gamma");

    // Parent on "ZZZZ" — no child match: the child lands at EOF.
    REQUIRE(AdsSkip(hP, 1) == openads::AE_SUCCESS);     // ZZZZ
    UNSIGNED16 eof = 9;
    REQUIRE(AdsAtEOF(hC, &eof) == openads::AE_SUCCESS);
    CHECK(eof == 1);

    // Back up to a hit: the relation re-seeks the child.
    REQUIRE(AdsGotoTop(hP) == openads::AE_SUCCESS);     // AAAA
    CHECK(child_data(hC) == "alpha");

    // After clearing, the child no longer tracks the parent.
    REQUIRE(AdsClearRelation(hP) == openads::AE_SUCCESS);
    REQUIRE(AdsSkip(hP, 1) == openads::AE_SUCCESS);     // BBBB, but child stays
    CHECK(child_data(hC) == "alpha");

    REQUIRE(AdsCloseTable(hC) == openads::AE_SUCCESS);
    REQUIRE(AdsCloseTable(hP) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsSetRelation drives a child by a numeric key as the parent moves") {
    const auto dir = fs::temp_directory_path() / "openads_setrelation_num";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == openads::AE_SUCCESS);

    // Parent rows hold the foreign key as a numeric field.
    UNSIGNED8 pdef[]  = "CID,Numeric,4,0";
    UNSIGNED8 pname[] = "ord";
    ADSHANDLE hP = 0;
    REQUIRE(AdsCreateTable(hConn, pname, nullptr, ADS_CDX, 0, 0, 0, 0, pdef, &hP)
            == openads::AE_SUCCESS);
    const double ids[] = {30, 10, 20};
    for (double id : ids) {
        REQUIRE(AdsAppendRecord(hP) == openads::AE_SUCCESS);
        UNSIGNED8 f[8] = "CID";
        REQUIRE(AdsSetDouble(hP, f, id) == openads::AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hP) == openads::AE_SUCCESS);
    }

    // Child keyed on its numeric ID.
    UNSIGNED8 cdef[]  = "ID,Numeric,4,0;DATA,Character,8";
    UNSIGNED8 cname[] = "cust";
    ADSHANDLE hC = 0;
    REQUIRE(AdsCreateTable(hConn, cname, nullptr, ADS_CDX, 0, 0, 0, 0, cdef, &hC)
            == openads::AE_SUCCESS);
    UNSIGNED8 bag[]  = "cust.cdx";
    UNSIGNED8 tag[]  = "BYID";
    UNSIGNED8 cexpr[] = "ID";
    ADSHANDLE hCI = 0;
    REQUIRE(AdsCreateIndex61(hC, bag, tag, cexpr, nullptr, nullptr, 0, 0, &hCI)
            == openads::AE_SUCCESS);
    struct { double id; const char* data; } rows[] = {
        {10, "ten"}, {20, "twenty"}, {30, "thirty"},
    };
    for (auto& r : rows) {
        REQUIRE(AdsAppendRecord(hC) == openads::AE_SUCCESS);
        UNSIGNED8 fid[8] = "ID";
        UNSIGNED8 fd[8]  = "DATA";
        REQUIRE(AdsSetDouble(hC, fid, r.id) == openads::AE_SUCCESS);
        REQUIRE(AdsSetString(hC, fd, (UNSIGNED8*)r.data,
                             (UNSIGNED32)std::strlen(r.data)) == openads::AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hC) == openads::AE_SUCCESS);
    }

    // Relate on the parent's CID expression -> child's numeric ID index.
    UNSIGNED8 rexpr[] = "CID";
    REQUIRE(AdsSetRelation(hP, hC, rexpr) == openads::AE_SUCCESS);

    REQUIRE(AdsGotoTop(hP) == openads::AE_SUCCESS);     // CID 30
    CHECK(child_data(hC) == "thirty");
    REQUIRE(AdsSkip(hP, 1) == openads::AE_SUCCESS);     // CID 10
    CHECK(child_data(hC) == "ten");
    REQUIRE(AdsSkip(hP, 1) == openads::AE_SUCCESS);     // CID 20
    CHECK(child_data(hC) == "twenty");

    REQUIRE(AdsCloseTable(hC) == openads::AE_SUCCESS);
    REQUIRE(AdsCloseTable(hP) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsSetScopedRelation constrains the child to the matching key group") {
    const auto dir = fs::temp_directory_path() / "openads_scopedrelation";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == openads::AE_SUCCESS);

    // Parent: two group codes.
    UNSIGNED8 pdef[]  = "GRP,Character,1";
    UNSIGNED8 pname[] = "grp";
    ADSHANDLE hP = 0;
    REQUIRE(AdsCreateTable(hConn, pname, nullptr, ADS_CDX, 0, 0, 0, 0, pdef, &hP)
            == openads::AE_SUCCESS);
    for (const char* g : {"A", "B"}) {
        REQUIRE(AdsAppendRecord(hP) == openads::AE_SUCCESS);
        UNSIGNED8 f[8] = "GRP";
        REQUIRE(AdsSetString(hP, f, (UNSIGNED8*)g, 1) == openads::AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hP) == openads::AE_SUCCESS);
    }

    // Child: many rows per group, indexed on GRP. A=3 rows, B=1 row.
    UNSIGNED8 cdef[]  = "GRP,Character,1;DATA,Character,8";
    UNSIGNED8 cname[] = "item";
    ADSHANDLE hC = 0;
    REQUIRE(AdsCreateTable(hConn, cname, nullptr, ADS_CDX, 0, 0, 0, 0, cdef, &hC)
            == openads::AE_SUCCESS);
    UNSIGNED8 bag[]  = "item.cdx";
    UNSIGNED8 tag[]  = "BYGRP";
    UNSIGNED8 cexpr[] = "GRP";
    ADSHANDLE hCI = 0;
    REQUIRE(AdsCreateIndex61(hC, bag, tag, cexpr, nullptr, nullptr, 0, 0, &hCI)
            == openads::AE_SUCCESS);
    struct { const char* grp; const char* data; } rows[] = {
        {"A", "a1"}, {"B", "b1"}, {"A", "a2"}, {"A", "a3"},
    };
    for (auto& r : rows) {
        REQUIRE(AdsAppendRecord(hC) == openads::AE_SUCCESS);
        UNSIGNED8 fg[8] = "GRP";
        UNSIGNED8 fd[8] = "DATA";
        REQUIRE(AdsSetString(hC, fg, (UNSIGNED8*)r.grp, 1) == openads::AE_SUCCESS);
        REQUIRE(AdsSetString(hC, fd, (UNSIGNED8*)r.data,
                             (UNSIGNED32)std::strlen(r.data)) == openads::AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hC) == openads::AE_SUCCESS);
    }

    UNSIGNED8 rexpr[] = "GRP";
    REQUIRE(AdsSetScopedRelation(hP, hC, rexpr) == openads::AE_SUCCESS);

    // Parent on "A": the child sees only the three A rows.
    REQUIRE(AdsGotoTop(hP) == openads::AE_SUCCESS);
    CHECK(child_visible_count(hC) == 3);

    // GotoBottom on the child stays within the A scope.
    REQUIRE(AdsGotoBottom(hC) == openads::AE_SUCCESS);
    UNSIGNED8 fg[8] = "GRP";
    UNSIGNED8 gbuf[4];
    UNSIGNED32 glen = sizeof(gbuf);
    REQUIRE(AdsGetString(hC, fg, gbuf, &glen, 0) == openads::AE_SUCCESS);
    CHECK(std::string((char*)gbuf, 1) == "A");

    // Parent on "B": the child now sees only the single B row.
    REQUIRE(AdsSkip(hP, 1) == openads::AE_SUCCESS);
    CHECK(child_visible_count(hC) == 1);

    // Clearing the relation releases the scope: the whole child is visible.
    REQUIRE(AdsClearRelation(hP) == openads::AE_SUCCESS);
    CHECK(child_visible_count(hC) == 4);

    REQUIRE(AdsCloseTable(hC) == openads::AE_SUCCESS);
    REQUIRE(AdsCloseTable(hP) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);
    fs::remove_all(dir, ec);
}

TEST_CASE("AdsSetRelation drives remote child over tcp://") {
    const auto dir = fs::temp_directory_path() / "openads_setrelation_wire";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    {
        UNSIGNED8 srv[512];
        std::string d = dir.string();
        std::memcpy(srv, d.c_str(), d.size() + 1);
        ADSHANDLE hConn = 0;
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
                == openads::AE_SUCCESS);

        UNSIGNED8 pdef[]  = "CODE,Character,4";
        UNSIGNED8 pname[] = "par";
        ADSHANDLE hP = 0;
        REQUIRE(AdsCreateTable(hConn, pname, nullptr, ADS_CDX, 0, 0, 0, 0, pdef, &hP)
                == openads::AE_SUCCESS);
        const char* codes[] = {"AAAA", "BBBB", "CCCC"};
        for (const char* c : codes) {
            REQUIRE(AdsAppendRecord(hP) == openads::AE_SUCCESS);
            UNSIGNED8 f[8] = "CODE";
            REQUIRE(AdsSetString(hP, f, (UNSIGNED8*)c, 4) == openads::AE_SUCCESS);
            REQUIRE(AdsWriteRecord(hP) == openads::AE_SUCCESS);
        }

        UNSIGNED8 cdef[]  = "CODE,Character,4;DATA,Character,8";
        UNSIGNED8 cname[] = "chi";
        ADSHANDLE hC = 0;
        REQUIRE(AdsCreateTable(hConn, cname, nullptr, ADS_CDX, 0, 0, 0, 0, cdef, &hC)
                == openads::AE_SUCCESS);
        UNSIGNED8 bag[]  = "chi.cdx";
        UNSIGNED8 tag[]  = "BYCODE";
        UNSIGNED8 cexpr[] = "CODE";
        ADSHANDLE hCI = 0;
        REQUIRE(AdsCreateIndex61(hC, bag, tag, cexpr, nullptr, nullptr, 0, 0, &hCI)
                == openads::AE_SUCCESS);
        struct { const char* code; const char* data; } rows[] = {
            {"AAAA", "alpha"}, {"BBBB", "beta"}, {"CCCC", "gamma"},
        };
        for (auto& r : rows) {
            REQUIRE(AdsAppendRecord(hC) == openads::AE_SUCCESS);
            UNSIGNED8 fc[8] = "CODE";
            UNSIGNED8 fd[8] = "DATA";
            REQUIRE(AdsSetString(hC, fc, (UNSIGNED8*)r.code, 4) == openads::AE_SUCCESS);
            REQUIRE(AdsSetString(hC, fd, (UNSIGNED8*)r.data,
                                 (UNSIGNED32)std::strlen(r.data)) == openads::AE_SUCCESS);
            REQUIRE(AdsWriteRecord(hC) == openads::AE_SUCCESS);
        }
        REQUIRE(AdsCloseTable(hC) == openads::AE_SUCCESS);
        REQUIRE(AdsCloseTable(hP) == openads::AE_SUCCESS);
        REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);
    }

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    std::string path = dir.string();
    std::replace(path.begin(), path.end(), '\\', '/');
    std::string uri = "tcp://127.0.0.1:" + std::to_string(srv.port()) + "/" + path;

    std::array<UNSIGNED8, 512> srv_uri{};
    REQUIRE(uri.size() < srv_uri.size());
    std::memcpy(srv_uri.data(), uri.c_str(), uri.size() + 1);

    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv_uri.data(), ADS_REMOTE_SERVER, nullptr, nullptr, 0, &hConn)
            == openads::AE_SUCCESS);

    ADSHANDLE hP = 0, hC = 0;
    UNSIGNED8 pn[] = "par";
    UNSIGNED8 cn[] = "chi";
    REQUIRE(AdsOpenTable(hConn, pn, nullptr, ADS_CDX, ADS_ANSI, ADS_READONLY,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hP)
            == openads::AE_SUCCESS);
    REQUIRE(AdsOpenTable(hConn, cn, nullptr, ADS_CDX, ADS_ANSI, ADS_READONLY,
                         ADS_COMPATIBLE_LOCKING, ADS_DEFAULT, &hC)
            == openads::AE_SUCCESS);

    UNSIGNED8 bag[] = "chi.cdx";
    ADSHANDLE ahIdx[8] = {0};
    UNSIGNED16 nidx = 8;
    REQUIRE(AdsOpenIndex(hC, bag, ahIdx, &nidx) == openads::AE_SUCCESS);
    REQUIRE(nidx >= 1);
    REQUIRE(AdsSetIndexOrderByHandle(hC, ahIdx[0]) == openads::AE_SUCCESS);

    UNSIGNED8 rexpr[] = "CODE";
    REQUIRE(AdsSetRelation(hP, hC, rexpr) == openads::AE_SUCCESS);

    REQUIRE(AdsGotoTop(hP) == openads::AE_SUCCESS);
    CHECK(child_data(hC) == "alpha");

    REQUIRE(AdsSkip(hP, 1) == openads::AE_SUCCESS);
    CHECK(child_data(hC) == "beta");

    // RCB 07/14/2026: DO NOT SHORTEN THIS SCAN. Everything below the first Skip
    // is here on purpose, and the test used to stop right above this line.
    //
    // That is precisely why a real wrong-data bug lived in shipped code for
    // months: the relation read the parent's key with a wire GetField, which
    // asks for the SERVER's current record — and the server's cursor LAGS the
    // client's logical position by however many rows were served out of the
    // read-ahead block. The FIRST skip of a scan happened to go to the wire (the
    // block was still empty), so the two cursors coincidentally agreed and the
    // child looked correct. Every skip after it drained the block locally, the
    // server cursor fell behind, and the child silently followed a STALE parent
    // row. Confirmed against the pre-fix tree: this asserted "gamma" and got
    // "beta".
    //
    // The lesson, and the reason for the comment: a ONE-STEP test cannot see a
    // lag bug, because the bug IS the second step. Any test covering a cursor
    // that can run ahead of the server has to walk at least three rows.
    REQUIRE(AdsSkip(hP, 1) == openads::AE_SUCCESS);
    CHECK(child_data(hC) == "gamma");

    // And again from the top. Since M12.24 the GotoTop ack comes back warm, so
    // here even the FIRST skip is served from the client cache and never touches
    // the wire — a different lag path to the one above, and the one that made
    // the bug reproduce immediately instead of on step two.
    REQUIRE(AdsGotoTop(hP) == openads::AE_SUCCESS);
    CHECK(child_data(hC) == "alpha");
    REQUIRE(AdsSkip(hP, 1) == openads::AE_SUCCESS);
    CHECK(child_data(hC) == "beta");
    REQUIRE(AdsSkip(hP, 1) == openads::AE_SUCCESS);
    CHECK(child_data(hC) == "gamma");

    REQUIRE(AdsCloseTable(hC) == openads::AE_SUCCESS);
    REQUIRE(AdsCloseTable(hP) == openads::AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == openads::AE_SUCCESS);
    srv.stop();
    fs::remove_all(dir, ec);
}
