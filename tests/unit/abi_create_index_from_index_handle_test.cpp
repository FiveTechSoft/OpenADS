// rddads ordCreate() with an active scope (or an ordCondSet() WHILE) calls
// AdsCreateIndex61(hOrdCurrent, ...) -- an INDEX handle -- and passes the
// scope as the WHILE text. Field report (D1TTYPE INQUIRE, temp
// K:\TEMP\kimqwq.IDX): over TCP this failed with 5000 "unknown table", and
// the native create ignored the WHILE, so a scoped temp index held every
// record. Both paths must accept the index handle and honour the WHILE.
#include "doctest.h"
#include "network/server.h"
#include "openads/ace.h"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

ADSHANDLE connect_to(const std::string& where, UNSIGNED16 type) {
    std::vector<UNSIGNED8> buf(where.begin(), where.end());
    buf.push_back(0);
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(buf.data(), type, nullptr, nullptr, 0, &h) == 0);
    return h;
}

void make_table(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "data");
    fs::create_directories(dir / "temp");
    ADSHANDLE conn = connect_to((dir / "data").string(), ADS_LOCAL_SERVER);
    UNSIGNED8 fields[] = "NAME,Character,10;AMT,Numeric,8,0";
    UNSIGNED8 tname[]  = "scoped.dbf";
    ADSHANDLE t = 0;
    REQUIRE(AdsCreateTable(conn, tname, nullptr, ADS_CDX, 0, 0, 0, 64,
                           fields, &t) == 0);
    for (int i = 0; i < 40; ++i) {
        REQUIRE(AdsAppendRecord(t) == 0);
        UNSIGNED8 fN[] = "NAME", fA[] = "AMT";
        const std::string v = "N" + std::to_string(100 + i);
        AdsSetString(t, fN, (UNSIGNED8*)v.c_str(), static_cast<UNSIGNED32>(v.size()));
        AdsSetDouble(t, fA, 40 - i);
        REQUIRE(AdsWriteRecord(t) == 0);
    }
    ADSHANDLE hi = 0;
    UNSIGNED8 bag[] = "scoped.cdx", tag[] = "TNAME", expr[] = "NAME";
    REQUIRE(AdsCreateIndex61(t, bag, tag, expr, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hi) == 0);
    REQUIRE(AdsCloseTable(t) == 0);
    REQUIRE(AdsDisconnect(conn) == 0);
}

void create_from_scoped_order(ADSHANDLE conn, const fs::path& temp_dir) {
    UNSIGNED8 name[] = "scoped.dbf";
    ADSHANDLE t = 0;
    REQUIRE(AdsOpenTable(conn, name, nullptr, ADS_CDX, ADS_ANSI,
                         ADS_COMPATIBLE_LOCKING, ADS_IGNORERIGHTS,
                         ADS_SHARED, &t) == 0);
    ADSHANDLE ord = 0;
    REQUIRE(AdsGetIndexHandleByOrder(t, 1, &ord) == 0);
    UNSIGNED8 top[] = "N110", bottom[] = "N119";
    REQUIRE(AdsSetScope(ord, ADS_TOP, top, 4, ADS_STRINGKEY) == 0);
    REQUIRE(AdsSetScope(ord, ADS_BOTTOM, bottom, 4, ADS_STRINGKEY) == 0);

    // Exactly what rddads builds for this scope.
    UNSIGNED8 whilex[] = "NAME>=\"N110\".AND.NAME<=\"N119\"";
    const std::string bag = (temp_dir / "kimqwq.IDX").string();
    std::vector<UNSIGNED8> bagb(bag.begin(), bag.end());
    bagb.push_back(0);
    UNSIGNED8 tag[] = "kimqwq", expr[] = "STR(AMT,8)";
    ADSHANDLE hnew = 0;
    const UNSIGNED32 rc = AdsCreateIndex61(ord, bagb.data(), tag, expr,
                                           nullptr, whilex, ADS_DEFAULT,
                                           ADS_DEFAULT, &hnew);
    REQUIRE(rc == 0);
    REQUIRE(hnew != 0);

    UNSIGNED32 keys = 0;
    CHECK(AdsGetKeyCount(hnew, ADS_IGNOREFILTERS, &keys) == 0);
    CHECK(keys == 10);
    // Walk it: only N110..N119, in AMT order (ascending = N119 first).
    REQUIRE(AdsGotoTop(hnew) == 0);
    int rows = 0;
    for (;;) {
        UNSIGNED16 eof = 0;
        AdsAtEOF(t, &eof);
        if (eof) break;
        UNSIGNED8 buf[16] = {0};
        UNSIGNED32 len = sizeof buf;
        UNSIGNED8 fN[] = "NAME";
        REQUIRE(AdsGetField(t, fN, buf, &len, ADS_NONE) == 0);
        std::string v(reinterpret_cast<char*>(buf), len);
        while (!v.empty() && v.back() == ' ') v.pop_back();
        INFO("row " << rows << " = " << v);
        CHECK(v >= "N110");
        CHECK(v <= "N119");
        if (rows == 0) CHECK(v == "N119");
        ++rows;
        REQUIRE(AdsSkip(hnew, 1) == 0);
        if (rows > 50) break;
    }
    CHECK(rows == 10);
    AdsCloseTable(t);
}

} // namespace

TEST_CASE("AdsCreateIndex61 on an index handle with a scope WHILE") {
    const auto dir = fs::temp_directory_path() / "openads_create_from_index";
    SUBCASE("local") {
        make_table(dir);
        ADSHANDLE c = connect_to((dir / "data").string(), ADS_LOCAL_SERVER);
        create_from_scoped_order(c, dir / "temp");
        AdsDisconnect(c);
    }
    SUBCASE("remote") {
        make_table(dir);
        openads::network::Server srv;
        REQUIRE(srv.start("127.0.0.1", 0).has_value());
        ADSHANDLE c = connect_to("tcp://127.0.0.1:" + std::to_string(srv.port()) +
                                 "/" + (dir / "data").generic_string(),
                                 ADS_REMOTE_SERVER);
        create_from_scoped_order(c, dir / "temp");
        AdsDisconnect(c);
        srv.stop();
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// rddads ordCreate() into a separate .IDX bag. Field report: "indexing to an
// IDX file closes the CDX file" and OrdKey() on the new IDX tag returned "".
// Two rddads sequences:
//  plain INDEX ON (no ordCondSet): OrdListClear (flush + AdsCloseAllIndexes)
//    then AdsCreateIndex61(hTable). The structural CDX must stay open.
//  ordCondSet(USECURRENT/WHILE): AdsCreateIndex61(hOrdCurrent) then
//    AdsOpenIndex(hTable, bag) -- current = ahIndex[0].
namespace {

std::string index_expr(ADSHANDLE ix) {
    UNSIGNED8 buf[256] = {0};
    UNSIGNED16 len = sizeof buf;
    if (AdsGetIndexExpr(ix, buf, &len) != 0) return "<err>";
    return std::string(reinterpret_cast<char*>(buf), len);
}

void idx_bag_keeps_cdx(ADSHANDLE conn, const fs::path& temp_dir, bool use_current) {
    UNSIGNED8 name[] = "scoped.dbf";
    ADSHANDLE t = 0;
    REQUIRE(AdsOpenTable(conn, name, nullptr, ADS_CDX, ADS_ANSI,
                         ADS_COMPATIBLE_LOCKING, ADS_IGNORERIGHTS,
                         ADS_SHARED, &t) == 0);
    UNSIGNED16 n = 0;
    REQUIRE(AdsGetNumIndexes(t, &n) == 0);
    REQUIRE(n == 2);
    ADSHANDLE ord = 0;
    REQUIRE(AdsGetIndexHandleByOrder(t, 1, &ord) == 0);

    const std::string bag = (temp_dir / (use_current ? "cur.IDX" : "plain.IDX")).string();
    std::vector<UNSIGNED8> bagb(bag.begin(), bag.end());
    bagb.push_back(0);
    UNSIGNED8 tag[] = "TMPX", expr[] = "STR(AMT,8)";
    UNSIGNED8 empty[] = "";
    ADSHANDLE hnew = 0;
    if (!use_current) {
        AdsFlushFileBuffers(t);
        REQUIRE(AdsCloseAllIndexes(t) == 0);
        REQUIRE(AdsCreateIndex61(t, bagb.data(), tag, expr, nullptr, empty,
                                 ADS_COMPOUND, ADS_DEFAULT, &hnew) == 0);
    } else {
        UNSIGNED8 whilex[] = "NAME>=\"N110\"";
        REQUIRE(AdsCreateIndex61(ord, bagb.data(), tag, expr, nullptr, whilex,
                                 ADS_COMPOUND, ADS_DEFAULT, &hnew) == 0);
        ADSHANDLE ah[256];
        UNSIGNED16 alen = 256;
        const UNSIGNED32 orc = AdsOpenIndex(t, bagb.data(), ah, &alen);
        CAPTURE(orc);
        CHECK((orc == 0 || orc == AE_INDEX_ALREADY_OPEN));
        REQUIRE(alen >= 1);
        hnew = ah[0];
    }
    CHECK(index_expr(hnew) == "STR(AMT,8)");

    // The structural bag's tags are still open next to the IDX tag.
    REQUIRE(AdsGetNumIndexes(t, &n) == 0);
    CHECK(n == 3);
    ADSHANDLE h1 = 0;
    UNSIGNED8 t1[] = "TNAME", t2[] = "TAMT";
    CHECK(AdsGetIndexHandle(t, t1, &h1) == 0);
    CHECK(index_expr(h1) == "NAME");
    ADSHANDLE hx = 0;
    CHECK(AdsGetIndexHandle(t, tag, &hx) == 0);
    CHECK(index_expr(hx) == "STR(AMT,8)");
    // The kept CDX tag still drives navigation: TNAME top is N100.
    REQUIRE(AdsGetIndexHandle(t, t1, &h1) == 0);
    REQUIRE(AdsGotoTop(h1) == 0);
    UNSIGNED8 buf[16] = {0};
    UNSIGNED32 len = sizeof buf;
    UNSIGNED8 fN[] = "NAME";
    REQUIRE(AdsGetField(t, fN, buf, &len, ADS_NONE) == 0);
    CHECK(std::string(reinterpret_cast<char*>(buf), 4) == "N100");
    CHECK(AdsGetIndexHandle(t, t2, &h1) == 0);
    AdsCloseTable(t);

    // Reopen, OrdListClear, OrdListAdd(the IDX): CDX + IDX, IDX key known.
    REQUIRE(AdsOpenTable(conn, name, nullptr, ADS_CDX, ADS_ANSI,
                         ADS_COMPATIBLE_LOCKING, ADS_IGNORERIGHTS,
                         ADS_SHARED, &t) == 0);
    AdsFlushFileBuffers(t);
    REQUIRE(AdsCloseAllIndexes(t) == 0);
    ADSHANDLE ah[256];
    UNSIGNED16 alen = 256;
    REQUIRE(AdsOpenIndex(t, bagb.data(), ah, &alen) == 0);
    REQUIRE(alen == 1);
    CHECK(index_expr(ah[0]) == "STR(AMT,8)");
    REQUIRE(AdsGetNumIndexes(t, &n) == 0);
    CHECK(n == 3);
    CHECK(AdsGetIndexHandle(t, t1, &h1) == 0);
    CHECK(index_expr(h1) == "NAME");
    AdsCloseTable(t);
}

void add_second_tag(const fs::path& dir) {
    ADSHANDLE conn = connect_to((dir / "data").string(), ADS_LOCAL_SERVER);
    UNSIGNED8 name[] = "scoped.dbf";
    ADSHANDLE t = 0;
    REQUIRE(AdsOpenTable(conn, name, nullptr, ADS_CDX, ADS_ANSI,
                         ADS_COMPATIBLE_LOCKING, ADS_IGNORERIGHTS,
                         ADS_EXCLUSIVE, &t) == 0);
    ADSHANDLE hi = 0;
    UNSIGNED8 bag[] = "scoped.cdx", tag[] = "TAMT", expr[] = "AMT";
    REQUIRE(AdsCreateIndex61(t, bag, tag, expr, nullptr, nullptr,
                             ADS_COMPOUND, 512, &hi) == 0);
    AdsCloseTable(t);
    AdsDisconnect(conn);
}

} // namespace

TEST_CASE("ordCreate into a separate IDX keeps the structural CDX open") {
    const auto dir = fs::temp_directory_path() / "openads_idx_keeps_cdx";
    for (const bool use_current : {false, true}) {
        CAPTURE(use_current);
        SUBCASE("local") {
            make_table(dir);
            add_second_tag(dir);
            ADSHANDLE c = connect_to((dir / "data").string(), ADS_LOCAL_SERVER);
            idx_bag_keeps_cdx(c, dir / "temp", use_current);
            AdsDisconnect(c);
        }
        SUBCASE("remote") {
            make_table(dir);
            add_second_tag(dir);
            openads::network::Server srv;
            REQUIRE(srv.start("127.0.0.1", 0).has_value());
            ADSHANDLE c = connect_to("tcp://127.0.0.1:" + std::to_string(srv.port()) +
                                     "/" + (dir / "data").generic_string(),
                                     ADS_REMOTE_SERVER);
            idx_bag_keeps_cdx(c, dir / "temp", use_current);
            AdsDisconnect(c);
            srv.stop();
        }
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// rddads INDEX ON <k> TAG <t> TO <table> on a table with NO structural CDX
// yet (dballcmp "orders" battery): OrdListClear + AdsCreateIndex61(hTable,
// "<table>", tag) twice. Over TCP the first tag became unfindable
// (OrdSetFocus("NAME") -> 0) once the second was created.
static void index_on_new_bag(ADSHANDLE conn) {
    UNSIGNED8 name[] = "nobag.dbf";
    ADSHANDLE t = 0;
    REQUIRE(AdsOpenTable(conn, name, nullptr, ADS_CDX, ADS_ANSI,
                         ADS_COMPATIBLE_LOCKING, ADS_IGNORERIGHTS,
                         ADS_SHARED, &t) == 0);
    UNSIGNED16 n = 9;
    REQUIRE(AdsGetNumIndexes(t, &n) == 0);
    REQUIRE(n == 0);
    UNSIGNED8 bag[] = "nobag", tag1[] = "NAME", e1[] = "NAME";
    UNSIGNED8 tag2[] = "NUM", e2[] = "STR(AMT,8)", empty[] = "";
    ADSHANDLE h1 = 0, h2 = 0;
    AdsFlushFileBuffers(t);
    REQUIRE(AdsCloseAllIndexes(t) == 0);
    REQUIRE(AdsCreateIndex61(t, bag, tag1, e1, nullptr, empty, ADS_COMPOUND,
                             ADS_DEFAULT, &h1) == 0);
    REQUIRE(AdsGotoTop(h1) == 0);
    AdsFlushFileBuffers(t);
    REQUIRE(AdsCloseAllIndexes(t) == 0);
    REQUIRE(AdsCreateIndex61(t, bag, tag2, e2, nullptr, empty, ADS_COMPOUND,
                             ADS_DEFAULT, &h2) == 0);
    REQUIRE(AdsGotoTop(h2) == 0);
    UNSIGNED16 ord = 0;
    CHECK(AdsGetIndexOrderByHandle(h2, &ord) == 0);
    CHECK(ord == 2);
    REQUIRE(AdsGetNumIndexes(t, &n) == 0);
    CHECK(n == 2);
    ADSHANDLE hn = 0;
    CHECK(AdsGetIndexHandle(t, tag1, &hn) == 0);
    CHECK(index_expr(hn) == "NAME");
    CHECK(AdsGetIndexOrderByHandle(hn, &ord) == 0);
    CHECK(ord == 1);
    AdsCloseTable(t);
}

TEST_CASE("INDEX ON ... TAG ... TO <table> creates the structural bag from scratch") {
    const auto dir = fs::temp_directory_path() / "openads_index_on_new_bag";
    auto mk = [&] {
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        ADSHANDLE c = connect_to(dir.string(), ADS_LOCAL_SERVER);
        UNSIGNED8 fields[] = "NAME,Character,10;AMT,Numeric,8,0";
        UNSIGNED8 tname[] = "nobag.dbf";
        ADSHANDLE t = 0;
        REQUIRE(AdsCreateTable(c, tname, nullptr, ADS_CDX, 0, 0, 0, 64, fields, &t) == 0);
        for (int i = 0; i < 5; ++i) {
            REQUIRE(AdsAppendRecord(t) == 0);
            UNSIGNED8 fN[] = "NAME";
            const std::string v = "N" + std::to_string(i);
            AdsSetString(t, fN, (UNSIGNED8*)v.c_str(), static_cast<UNSIGNED32>(v.size()));
            REQUIRE(AdsWriteRecord(t) == 0);
        }
        AdsCloseTable(t);
        AdsDisconnect(c);
    };
    SUBCASE("local") {
        mk();
        ADSHANDLE c = connect_to(dir.string(), ADS_LOCAL_SERVER);
        index_on_new_bag(c);
        AdsDisconnect(c);
    }
    SUBCASE("remote") {
        mk();
        openads::network::Server srv;
        REQUIRE(srv.start("127.0.0.1", 0).has_value());
        ADSHANDLE c = connect_to("tcp://127.0.0.1:" + std::to_string(srv.port()) +
                                 "/" + dir.generic_string(), ADS_REMOTE_SERVER);
        index_on_new_bag(c);
        AdsDisconnect(c);
        srv.stop();
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// rddads USECURRENT exactly: non-compound .idx (options 0, empty tag),
// built from the scoped order; rddads forwards only the bottom scope as
// the WHILE. DBFCDX builds the 4 keys inside the scope (N110..N113 here).
static void usecurrent_noncompound(ADSHANDLE conn, const fs::path& temp_dir) {
    // (temp_dir is shared with the bags opened below)
    UNSIGNED8 name[] = "scoped.dbf";
    ADSHANDLE t = 0;
    REQUIRE(AdsOpenTable(conn, name, nullptr, ADS_CDX, ADS_ANSI,
                         ADS_COMPATIBLE_LOCKING, ADS_IGNORERIGHTS,
                         ADS_SHARED, &t) == 0);
    // dballcmp: two other non-compound bags are already open, one with
    // the same key expression as the new one.
    for (const char* other : {"pl.idx", "ad.idx"}) {
        const std::string ob = (temp_dir / other).string();
        std::vector<UNSIGNED8> obb(ob.begin(), ob.end());
        obb.push_back(0);
        UNSIGNED8 etag[] = "", oexpr[] = "STR(AMT,8)";
        ADSHANDLE ho = 0;
        REQUIRE(AdsCreateIndex61(t, obb.data(), etag, oexpr, nullptr, nullptr,
                                 0, ADS_DEFAULT, &ho) == 0);
    }
    ADSHANDLE ord = 0;
    UNSIGNED8 tname[] = "TNAME";
    REQUIRE(AdsGetIndexHandle(t, tname, &ord) == 0);
    UNSIGNED8 top[] = "N110", bottom[] = "N113";
    REQUIRE(AdsSetScope(ord, ADS_TOP, top, 4, ADS_STRINGKEY) == 0);
    REQUIRE(AdsSetScope(ord, ADS_BOTTOM, bottom, 4, ADS_STRINGKEY) == 0);
    UNSIGNED8 whilex[] = "NAME<=\"N113\"";
    const std::string bag = (temp_dir / "uc.idx").string();
    std::vector<UNSIGNED8> bagb(bag.begin(), bag.end());
    bagb.push_back(0);
    UNSIGNED8 tag[] = "", expr[] = "STR(AMT,8)";
    ADSHANDLE hnew = 0;
    REQUIRE(AdsCreateIndex61(ord, bagb.data(), tag, expr, nullptr, whilex,
                             0, ADS_DEFAULT, &hnew) == 0);
    UNSIGNED32 keys = 0;
    CHECK(AdsGetKeyCount(hnew, ADS_IGNOREFILTERS, &keys) == 0);
    CHECK(keys == 4);
    // rddads then reopens the bag and counts through ahIndex[0].
    ADSHANDLE ah[16];
    UNSIGNED16 alen = 16;
    const UNSIGNED32 orc = AdsOpenIndex(t, bagb.data(), ah, &alen);
    CHECK((orc == 0 || orc == AE_INDEX_ALREADY_OPEN));
    REQUIRE(alen >= 1);
    keys = 0;
    CHECK(AdsGetKeyCount(ah[0], ADS_IGNOREFILTERS, &keys) == 0);
    CHECK(keys == 4);
    keys = 0;
    CHECK(AdsGetKeyCount(ah[0], ADS_RESPECTSCOPES, &keys) == 0);
    CHECK(keys == 4);
    // rddads OrdKeyCount() (DBOI_KEYCOUNT, no filter) -- while the source
    // order's scope is still set.
    keys = 0;
    CHECK(AdsGetRecordCount(ah[0], ADS_RESPECTSCOPES, &keys) == 0);
    CHECK(keys == 4);
    UNSIGNED8 nb[32] = {0};
    UNSIGNED16 nl = sizeof nb;
    CHECK(AdsGetIndexName(hnew, nb, &nl) == 0);
    CHECK(std::string(reinterpret_cast<char*>(nb), nl) == "UC");
    AdsCloseTable(t);
}

TEST_CASE("USECURRENT into a non-compound IDX honours both scopes") {
    const auto dir = fs::temp_directory_path() / "openads_usecurrent_idx";
    SUBCASE("local") {
        make_table(dir);
        ADSHANDLE c = connect_to((dir / "data").string(), ADS_LOCAL_SERVER);
        usecurrent_noncompound(c, dir / "temp");
        AdsDisconnect(c);
    }
    SUBCASE("remote") {
        make_table(dir);
        openads::network::Server srv;
        REQUIRE(srv.start("127.0.0.1", 0).has_value());
        ADSHANDLE c = connect_to("tcp://127.0.0.1:" + std::to_string(srv.port()) +
                                 "/" + (dir / "data").generic_string(),
                                 ADS_REMOTE_SERVER);
        usecurrent_noncompound(c, dir / "temp");
        AdsDisconnect(c);
        srv.stop();
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}
