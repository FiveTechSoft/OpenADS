// M12.23 — rddads passes hOrdCurrent (a RemoteIndex handle) to
// AdsGotoTop / AdsSkip after AdsCreateIndex61 over TCP. Without
// routing those calls through the parent table cursor the post-create
// DBGoTop hangs or returns AE_INTERNAL_ERROR.
#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void make_colonias_dbf(const fs::path& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512];
    const auto sp = dir.string();
    std::memcpy(srv, sp.c_str(), sp.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[] =
        "COLONIA,C,20,0;NOMBRE,C,30,0;CP,C,5,0";
    UNSIGNED8 tname[] = "CCOLONIA.DBF";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 0, def, &hT) == 0);

    struct Row { const char* col; const char* nom; const char* cp; };
    const Row rows[] = {
        {"Centro",   "Av. Hidalgo", "06000"},
        {"Roma",     "Calle Orizaba", "06700"},
        {"Condesa",  "Av. Amsterdam", "06140"},
    };
    UNSIGNED8 f_col[] = "COLONIA";
    UNSIGNED8 f_nom[] = "NOMBRE";
    UNSIGNED8 f_cp[]  = "CP";
    for (const auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        AdsSetString(hT, f_col, (UNSIGNED8*)r.col,
                     static_cast<UNSIGNED32>(std::strlen(r.col)));
        AdsSetString(hT, f_nom, (UNSIGNED8*)r.nom,
                     static_cast<UNSIGNED32>(std::strlen(r.nom)));
        AdsSetString(hT, f_cp, (UNSIGNED8*)r.cp,
                     static_cast<UNSIGNED32>(std::strlen(r.cp)));
        REQUIRE(AdsWriteRecord(hT) == 0);
    }

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

// Like make_colonias_dbf but also builds a *production* CDX
// (CCOLONIA.CDX, same base name as the table) with a NOMBRE tag, then
// closes everything. A later remote open must auto-bind this bag.
// NOMBRE ascending = Av. Amsterdam(rec3), Av. Hidalgo(rec1),
// Calle Orizaba(rec2) — so an ordered GotoTop lands on rec 3, distinct
// from natural order's rec 1.
void make_colonias_dbf_with_prod_index(const fs::path& dir) {
    make_colonias_dbf(dir);

    UNSIGNED8 srv[512];
    const auto sp = dir.string();
    std::memcpy(srv, sp.c_str(), sp.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    ADSHANDLE hT = 0;
    UNSIGNED8 tname[] = "CCOLONIA.DBF";
    UNSIGNED8 alias[] = "CCOLONIA";
    REQUIRE(AdsOpenTable(hConn, tname, alias, ADS_CDX, 0, 0, 0, 0, &hT) == 0);
    ADSHANDLE hIndex = 0;
    UNSIGNED8 bag[]  = "CCOLONIA.CDX";
    UNSIGNED8 tag[]  = "NOMBRE";
    UNSIGNED8 expr[] = "NOMBRE";
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, ADS_COMPOUND, 512,
                             &hIndex) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}

ADSHANDLE remote_connect(const fs::path& dir, std::uint16_t port) {
    std::string uri = "tcp://127.0.0.1:" + std::to_string(port) + "/" +
                      dir.generic_string();
    std::vector<UNSIGNED8> buf(uri.begin(), uri.end());
    buf.push_back(0);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(buf.data(), ADS_REMOTE_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    return hConn;
}

} // namespace

TEST_CASE("remote AdsGotoTop/AdsSkip accept RemoteIndex handles") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_remote_idx_nav";
    make_colonias_dbf(dir);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = srv.port();

    ADSHANDLE hConn = remote_connect(dir, port);
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[] = "CCOLONIA.DBF";
    UNSIGNED8 alias[] = "CCOLONIA";
    REQUIRE(AdsOpenTable(hConn, tname, alias, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    ADSHANDLE hIndex = 0;
    UNSIGNED8 bag[]  = "CCOLONIA.CDX";
    UNSIGNED8 tag[]  = "COLONIA";
    UNSIGNED8 expr[] = "COLONIA";
    REQUIRE(AdsCreateIndex61(hTable, bag, tag, expr,
                             nullptr, nullptr, ADS_COMPOUND, 512,
                             &hIndex) == 0);
    REQUIRE(hIndex != 0);

    UNSIGNED8 nm[32] = {0};
    UNSIGNED16 nl = sizeof(nm);
    REQUIRE(AdsGetIndexName(hIndex, nm, &nl) == 0);
    CHECK(std::string(reinterpret_cast<char*>(nm), nl) == "COLONIA");

    // Simulate rddads: navigation via the index handle, not the table.
    REQUIRE(AdsGotoTop(hIndex) == 0);

    UNSIGNED32 r1 = 0, r2 = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &r1) == 0);
    CHECK(r1 > 0u);

    REQUIRE(AdsSkip(hIndex, 1) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &r2) == 0);
    CHECK(r2 != r1);

    REQUIRE(AdsCloseIndex(hIndex) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

// rddads' DbSetOrder(<number>) over the wire: open a table whose
// production CDX already exists, then resolve the order by ordinal
// (AdsGetIndexHandleByOrder) and by name (AdsGetIndexHandle) and
// navigate by the resulting RemoteIndex handle. Before the fix the
// remote open never bound the production bag, so AdsGetNumIndexes was 0
// and by-ordinal resolution failed — CDX silently fell back to natural
// order (the fivedbu "remote shows no index" report).
TEST_CASE("remote DbSetOrder by number/name uses production index") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_remote_prod_idx";
    make_colonias_dbf_with_prod_index(dir);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = srv.port();

    ADSHANDLE hConn = remote_connect(dir, port);
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[] = "CCOLONIA.DBF";
    UNSIGNED8 alias[] = "CCOLONIA";
    REQUIRE(AdsOpenTable(hConn, tname, alias, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    // Production CDX auto-bound on open → one order visible remotely.
    UNSIGNED16 nidx = 0;
    REQUIRE(AdsGetNumIndexes(hTable, &nidx) == 0);
    CHECK(nidx >= 1u);

    // DbSetOrder(1): resolve ordinal -> RemoteIndex handle.
    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hTable, 1, &hOrd) == 0);
    REQUIRE(hOrd != 0);

    UNSIGNED8  nm[32] = {0};
    UNSIGNED16 nl = sizeof(nm);
    REQUIRE(AdsGetIndexName(hOrd, nm, &nl) == 0);
    CHECK(std::string(reinterpret_cast<char*>(nm), nl) == "NOMBRE");

    // Navigate by the order handle (rddads passes hOrdCurrent). Ordered
    // GotoTop must land on rec 3 (Av. Amsterdam), NOT natural rec 1.
    REQUIRE(AdsGotoTop(hOrd) == 0);
    UNSIGNED32 rec = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &rec) == 0);
    CHECK(rec == 3u);

    // DbSetOrder("NOMBRE"): resolve by name to the same handle.
    ADSHANDLE hByName = 0;
    UNSIGNED8 want[] = "NOMBRE";
    REQUIRE(AdsGetIndexHandle(hTable, want, &hByName) == 0);
    CHECK(hByName == hOrd);

    // FWH xBrowse: ADSKEYCOUNT(,,1) passes filter option 1 (Harbour
    // ADS_RESPECTFILTERS). AdsGetScope must report empty scope on the
    // RemoteIndex handle so rddads takes the direct AdsGetRecordCount
    // path instead of a broken key-walk that returns 0.
    UNSIGNED32 key_cnt = 0, key_no = 0;
    REQUIRE(AdsGetRecordCount(hOrd, 1, &key_cnt) == 0);
    CHECK(key_cnt == 3u);
    REQUIRE(AdsGetKeyNum(hOrd, 0, &key_no) == 0);
    CHECK(key_no == 1u);

    UNSIGNED16 scope_len = 99;
    REQUIRE(AdsGetScope(hOrd, ADS_BOTTOM, nullptr, &scope_len) == 0);
    CHECK(scope_len == 0u);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

// Production CDX beside a table in a subdirectory: ensure_abi_handle must
// reopen with the same relative path the client used, not basename-only.
TEST_CASE("remote production CDX auto-open in subdirectory") {
    using openads::network::Server;
    auto base = fs::temp_directory_path() / "openads_remote_prod_subdir";
    auto data = base / "data";
    make_colonias_dbf_with_prod_index(data);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = srv.port();

    ADSHANDLE hConn = remote_connect(base, port);
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[] = "data/CCOLONIA.DBF";
    UNSIGNED8 alias[] = "CCOLONIA";
    REQUIRE(AdsOpenTable(hConn, tname, alias, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    UNSIGNED16 nidx = 0;
    REQUIRE(AdsGetNumIndexes(hTable, &nidx) == 0);
    CHECK(nidx >= 1u);

    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hTable, 1, &hOrd) == 0);
    REQUIRE(hOrd != 0);
    REQUIRE(AdsGotoTop(hOrd) == 0);
    UNSIGNED32 rec = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &rec) == 0);
    CHECK(rec == 3u);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    std::error_code ec;
    fs::remove_all(base, ec);
    srv.stop();
}

// xBrowse / TDataBase navigate via the TABLE handle after
// AdsSetIndexOrderByHandle — not via hOrdCurrent. The server must
// honour ordered_tables_ on GotoTop/Skip for the table id.
TEST_CASE("remote SetIndexOrderByHandle + table-handle GotoTop/Skip") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_remote_setorder_tbl";
    make_colonias_dbf_with_prod_index(dir);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = srv.port();

    ADSHANDLE hConn = remote_connect(dir, port);
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[] = "CCOLONIA.DBF";
    UNSIGNED8 alias[] = "CCOLONIA";
    REQUIRE(AdsOpenTable(hConn, tname, alias, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    UNSIGNED16 nidx = 0;
    REQUIRE(AdsGetNumIndexes(hTable, &nidx) == 0);
    REQUIRE(nidx >= 1u);

    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hTable, 1, &hOrd) == 0);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd) == 0);

    // Navigate by TABLE handle (xBrowse path), not index handle.
    REQUIRE(AdsGotoTop(hTable) == 0);
    UNSIGNED32 rec = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &rec) == 0);
    CHECK(rec == 3u);

    UNSIGNED32 rec2 = 0;
    REQUIRE(AdsSkip(hTable, 1) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &rec2) == 0);
    CHECK(rec2 == 1u);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}

TEST_CASE("local customer.dbf CUSTNAME order via table handle") {
    fs::path data = "C:/OpenADS/testdata/invoices";
    if (const char* root = std::getenv("OPENADS_ROOT"))
        data = fs::path(root) / "testdata" / "invoices";
    if (!fs::exists(data / "customer.dbf")) return;

    std::string sp = data.string();
    std::vector<UNSIGNED8> srv(sp.begin(), sp.end());
    srv.push_back(0);
    ADSHANDLE hConn = 0, hTable = 0;
    REQUIRE(AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 tname[] = "customer.dbf";
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hTable, 2, &hOrd) == 0);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);

    std::vector<std::string> names;
    for (int i = 0; i < 8; ++i) {
        UNSIGNED16 eof = 0;
        AdsAtEOF(hTable, &eof);
        if (eof) break;
        UNSIGNED8 buf[64] = {0};
        UNSIGNED32 cap = sizeof(buf) - 1;
        AdsGetField(hTable, (UNSIGNED8*)"NAME", buf, &cap, 0);
        names.emplace_back(reinterpret_cast<char*>(buf), cap);
        AdsSkip(hTable, 1);
    }
    REQUIRE(names.size() >= 2u);
    for (std::size_t i = 1; i < names.size(); ++i) {
        CHECK(names[i] >= names[i - 1]);
    }

    AdsCloseTable(hTable);
    AdsDisconnect(hConn);
}

TEST_CASE("remote customer.dbf CUSTNAME order via table handle") {
    using openads::network::Server;
    fs::path data = "C:/OpenADS/testdata/invoices";
    if (const char* root = std::getenv("OPENADS_ROOT"))
        data = fs::path(root) / "testdata" / "invoices";
    if (!fs::exists(data / "customer.dbf")) return;

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = srv.port();

    ADSHANDLE hConn = remote_connect(data, port);
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[] = "customer.dbf";
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hTable, 2, &hOrd) == 0);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);

    std::vector<std::string> names;
    for (int i = 0; i < 8; ++i) {
        UNSIGNED16 eof = 0;
        AdsAtEOF(hTable, &eof);
        if (eof) break;
        UNSIGNED8 buf[64] = {0};
        UNSIGNED32 cap = sizeof(buf) - 1;
        AdsGetField(hTable, (UNSIGNED8*)"NAME", buf, &cap, 0);
        names.emplace_back(reinterpret_cast<char*>(buf), cap);
        AdsSkip(hTable, 1);
    }
    REQUIRE(names.size() >= 2u);
    for (std::size_t i = 1; i < names.size(); ++i) {
        CHECK(names[i] >= names[i - 1]);
    }

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    srv.stop();
}

TEST_CASE("remote skip roundtrip restores same record") {
    using openads::network::Server;
    fs::path data = "C:/OpenADS/testdata/invoices";
    if (const char* root = std::getenv("OPENADS_ROOT"))
        data = fs::path(root) / "testdata" / "invoices";
    if (!fs::exists(data / "customer.dbf")) return;

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = srv.port();

    ADSHANDLE hConn = remote_connect(data, port);
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[] = "customer.dbf";
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    auto snap = [&](const char* label) {
        UNSIGNED32 rec = 0;
        REQUIRE(AdsGetRecordNum(hTable, 0, &rec) == 0);
        UNSIGNED8 buf[64] = {0};
        UNSIGNED32 cap = sizeof(buf) - 1;
        REQUIRE(AdsGetField(hTable, (UNSIGNED8*)"NAME", buf, &cap, 0) == 0);
        std::string name(reinterpret_cast<char*>(buf), cap);
        INFO(label << " rec=" << rec << " name=" << name);
        return std::pair<UNSIGNED32, std::string>{rec, name};
    };

    auto roundtrip = [&](int steps) {
        auto before = snap("before");
        for (int i = 0; i < steps; ++i) REQUIRE(AdsSkip(hTable, 1) == 0);
        for (int i = 0; i < steps; ++i) REQUIRE(AdsSkip(hTable, -1) == 0);
        auto after = snap("after");
        CHECK(before.first == after.first);
        CHECK(before.second == after.second);
    };

    REQUIRE(AdsGotoTop(hTable) == 0);
    roundtrip(1);
    roundtrip(3);

    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hTable, 2, &hOrd) == 0);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);
    roundtrip(1);
    roundtrip(5);

    REQUIRE(AdsGotoTop(hOrd) == 0);
    auto before = snap("ix-before");
    REQUIRE(AdsSkip(hOrd, 1) == 0);
    REQUIRE(AdsSkip(hOrd, -1) == 0);
    auto after = snap("ix-after");
    CHECK(before.first == after.first);
    CHECK(before.second == after.second);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    srv.stop();
}

// FWH xBrowse Paint() bookmark pattern: save RecNo, skip visible rows,
// DbGoto(bookmark). KeyNo must still describe the saved row, not the
// paint walk.
TEST_CASE("remote bookmark restore keeps AdsGetKeyNum coherent") {
    using openads::network::Server;
    fs::path data = "C:/OpenADS/testdata/invoices";
    if (const char* root = std::getenv("OPENADS_ROOT"))
        data = fs::path(root) / "testdata" / "invoices";
    if (!fs::exists(data / "customer.dbf")) return;

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = srv.port();

    ADSHANDLE hConn = remote_connect(data, port);
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[] = "customer.dbf";
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hTable, 2, &hOrd) == 0);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd) == 0);
    REQUIRE(AdsGotoTop(hTable) == 0);

    UNSIGNED32 rec0 = 0, key0 = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &rec0) == 0);
    REQUIRE(AdsGetKeyNum(hTable, 0, &key0) == 0);
    CHECK(key0 == 1u);

    const int paint_rows = 5;
    for (int i = 0; i < paint_rows; ++i) REQUIRE(AdsSkip(hTable, 1) == 0);

    UNSIGNED32 key_mid = 0;
    REQUIRE(AdsGetKeyNum(hTable, 0, &key_mid) == 0);
    CHECK(key_mid == static_cast<UNSIGNED32>(paint_rows + 1));

    REQUIRE(AdsGotoRecord(hTable, rec0) == 0);

    UNSIGNED32 rec1 = 0, key1 = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &rec1) == 0);
    REQUIRE(AdsGetKeyNum(hTable, 0, &key1) == 0);
    CHECK(rec1 == rec0);
    CHECK(key1 == key0);

    // CalcRowSelPos-style skip after bookmark restore.
    REQUIRE(AdsSkip(hTable, 3) == 0);
    UNSIGNED32 key_after = 0;
    REQUIRE(AdsGetKeyNum(hTable, 0, &key_after) == 0);
    CHECK(key_after == 4u);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    srv.stop();
}

// xBrowse Paint() + OrdSetFocus: skips use hOrdCurrent (RemoteIndex),
// bookmark restore uses AdsGotoRecord on the table handle. The server
// must re-anchor the ABI index cursor on GOTO or the next index Skip
// walks from the paint-walk position instead of the bookmark.
TEST_CASE("remote index skip after GotoRecord bookmark restore") {
    using openads::network::Server;
    fs::path data = "C:/OpenADS/testdata/invoices";
    if (const char* root = std::getenv("OPENADS_ROOT"))
        data = fs::path(root) / "testdata" / "invoices";
    if (!fs::exists(data / "customer.dbf")) return;

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = srv.port();

    ADSHANDLE hConn = remote_connect(data, port);
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[] = "customer.dbf";
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hTable, 2, &hOrd) == 0);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd) == 0);
    REQUIRE(AdsGotoTop(hOrd) == 0);

    auto read_name = [&](ADSHANDLE h) -> std::string {
        UNSIGNED8 buf[64] = {0};
        UNSIGNED32 cap = sizeof(buf) - 1;
        REQUIRE(AdsGetField(h, (UNSIGNED8*)"NAME", buf, &cap, 0) == 0);
        return std::string(reinterpret_cast<char*>(buf), cap);
    };

    const std::string top_name = read_name(hTable);
    UNSIGNED32 top_rec = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &top_rec) == 0);
    REQUIRE(!top_name.empty());

    const int paint_rows = 5;
    for (int i = 0; i < paint_rows; ++i) REQUIRE(AdsSkip(hOrd, 1) == 0);
    const std::string walk_name = read_name(hTable);
    CHECK(walk_name != top_name);

    REQUIRE(AdsGotoRecord(hTable, top_rec) == 0);
    CHECK(read_name(hTable) == top_name);

    REQUIRE(AdsSkip(hOrd, 1) == 0);
    const std::string after_name = read_name(hTable);
    CHECK(after_name != top_name);
    CHECK(after_name != walk_name);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    srv.stop();
}

TEST_CASE("remote index skip(-1) at top sets BOF for xBrowse GoUp") {
    using openads::network::Server;
    fs::path data = "C:/OpenADS/testdata/invoices";
    if (const char* root = std::getenv("OPENADS_ROOT"))
        data = fs::path(root) / "testdata" / "invoices";
    if (!fs::exists(data / "customer.dbf")) return;

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = srv.port();

    ADSHANDLE hConn = remote_connect(data, port);
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[] = "customer.dbf";
    REQUIRE(AdsOpenTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hTable, 2, &hOrd) == 0);
    REQUIRE(AdsSetIndexOrderByHandle(hTable, hOrd) == 0);
    REQUIRE(AdsGotoTop(hOrd) == 0);

    UNSIGNED32 rec0 = 0;
    REQUIRE(AdsGetRecordNum(hTable, 0, &rec0) == 0);
    UNSIGNED16 bof = 0;
    REQUIRE(AdsAtBOF(hTable, &bof) == 0);
    CHECK(bof == 0u);

    REQUIRE(AdsSkip(hOrd, -1) == 0);
    REQUIRE(AdsGetRecordNum(hTable, 0, &rec0) == 0);
    REQUIRE(AdsAtBOF(hTable, &bof) == 0);
    CHECK(bof == 1u);

    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    srv.stop();
}

TEST_CASE("remote legacy AdsCreateIndex routes to AdsCreateIndex61") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_remote_idx_legacy";
    make_colonias_dbf(dir);

    Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = srv.port();

    ADSHANDLE hConn = remote_connect(dir, port);
    ADSHANDLE hTable = 0;
    UNSIGNED8 tname[] = "CCOLONIA.DBF";
    UNSIGNED8 alias[] = "CCOLONIA";
    REQUIRE(AdsOpenTable(hConn, tname, alias, ADS_CDX, 0, 0, 0, 0, &hTable) == 0);

    ADSHANDLE hIndex = 0;
    UNSIGNED8 bag[]  = "CCOLONIA.CDX";
    UNSIGNED8 tag[]  = "NOMBRE";
    UNSIGNED8 expr[] = "NOMBRE";
    REQUIRE(AdsCreateIndex(hTable, bag, tag, expr,
                           nullptr, ADS_COMPOUND, 512, &hIndex) == 0);
    REQUIRE(hIndex != 0);
    REQUIRE(AdsGotoTop(hIndex) == 0);

    REQUIRE(AdsCloseIndex(hIndex) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    std::error_code ec;
    fs::remove_all(dir, ec);
    srv.stop();
}
// M12.28 — AdsGetKeyCount over the wire for a remote alias with an active
// conditional FOR index. Before the fix, AdsGetKeyCount returned 0 for
// remote index handles (no get_remote_index guard) and the server never
// had a GetKeyCount opcode — so OrdKeyCount() in xBrowse returned 0 and
// the grid showed no rows.
TEST_CASE("remote AdsGetKeyCount with conditional FOR index returns filtered count") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_remote_keycount";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // Stage: create table with 200 rows, CODIGO = 1..200, then a
    // conditional tag TF with FOR CODIGO>100 → index has exactly 100 entries.
    UNSIGNED8 srv[512];
    const auto sp = dir.string();
    std::memcpy(srv, sp.c_str(), sp.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 fields[] = "CODIGO,Numeric,6,0;NOME,Character,20";
    UNSIGNED8 tname[]  = "kctest.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 64, fields, &hT) == 0);

    for (int i = 1; i <= 200; ++i) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        UNSIGNED8 fCod[] = "CODIGO";
        REQUIRE(AdsSetDouble(hT, fCod, static_cast<double>(i)) == 0);
        UNSIGNED8 fNome[] = "NOME";
        std::string v = "n" + std::to_string(i);
        REQUIRE(AdsSetString(hT, fNome,
                             reinterpret_cast<UNSIGNED8*>(v.data()),
                             static_cast<UNSIGNED32>(v.size())) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }

    // Create conditional tag: key CODIGO, FOR CODIGO>100.
    UNSIGNED8 bag[]  = "kctest.cdx";
    UNSIGNED8 tag[]  = "TF";
    UNSIGNED8 expr[] = "CODIGO";
    UNSIGNED8 cond[] = "CODIGO>100";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             cond, nullptr, 0, 512, &hIdx) == 0);

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    // --- Now connect remotely and verify key count ---
    Server server;
    REQUIRE(server.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = server.port();

    ADSHANDLE hRC = remote_connect(dir, port);
    ADSHANDLE hRT = 0;
    REQUIRE(AdsOpenTable(hRC, tname, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hRT) == 0);

    // Activate the conditional order via the RemoteIndex handle.
    ADSHANDLE hOrd = 0;
    UNSIGNED8 want_tag[] = "TF";
    REQUIRE(AdsGetIndexHandle(hRT, want_tag, &hOrd) == 0);
    REQUIRE(hOrd != 0);
    REQUIRE(AdsSetIndexOrderByHandle(hRT, hOrd) == 0);

    // AdsGetKeyCount via the ORDER handle — the rddads OrdKeyCount path.
    UNSIGNED32 key_cnt = 0;
    REQUIRE(AdsGetKeyCount(hOrd, 0, &key_cnt) == 0);
    CHECK(key_cnt == 100u);

    // AdsGetKeyCount via the TABLE handle — with an active order, returns
    // filtered key count (same as local AdsGetKeyCount behaviour).
    UNSIGNED32 tbl_cnt = 0;
    REQUIRE(AdsGetKeyCount(hRT, 0, &tbl_cnt) == 0);
    CHECK(tbl_cnt == 100u);

    // AdsGetRecordCount via the ORDER handle — remote route passes through
    // to the parent table's physical record_count() (200). The filtered
    // count is served by AdsGetKeyCount, which is the correct path for
    // OrdKeyCount / DBOI_KEYCOUNT.
    UNSIGNED32 rec_cnt = 0;
    REQUIRE(AdsGetRecordCount(hOrd, 1, &rec_cnt) == 0);
    CHECK(rec_cnt == 200u);

    // Verify the index walk: GotoTop + Skip should only visit CODIGO > 100.
    REQUIRE(AdsGotoTop(hOrd) == 0);
    double first_cod = 0.0;
    UNSIGNED8 fCod[] = "CODIGO";
    REQUIRE(AdsGetDouble(hRT, fCod, &first_cod) == 0);
    CHECK(first_cod == 101.0);

    REQUIRE(AdsCloseTable(hRT) == 0);
    REQUIRE(AdsDisconnect(hRC) == 0);
    fs::remove_all(dir, ec);
    server.stop();
}

// OrdScope / AdsSetScope: scope is stored on the server's ABI table
// order. Without marking ordered_tables_ after SetScope (or syncing
// SetOrder before index GotoTop), GotoTop/Skip used the engine Table
// and returned every record — the work-order labour-items report.
TEST_CASE("remote AdsSetScope constrains GotoTop/Skip walk") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_remote_scope";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512];
    const auto sp = dir.string();
    std::memcpy(srv, sp.c_str(), sp.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 fields[] = "GRP,Character,1;DATA,Character,8";
    UNSIGNED8 tname[]  = "item.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 64, fields, &hT) == 0);
    UNSIGNED8 bag[]  = "item.cdx";
    UNSIGNED8 tag[]  = "BYGRP";
    UNSIGNED8 expr[] = "GRP";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, 0, 512, &hIdx) == 0);
    struct Row { const char* grp; const char* data; };
    const Row rows[] = {
        {"A", "a1"}, {"B", "b1"}, {"A", "a2"}, {"A", "a3"},
    };
    for (const auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        UNSIGNED8 fg[] = "GRP";
        UNSIGNED8 fd[] = "DATA";
        REQUIRE(AdsSetString(hT, fg, (UNSIGNED8*)r.grp, 1) == 0);
        REQUIRE(AdsSetString(hT, fd, (UNSIGNED8*)r.data,
                             static_cast<UNSIGNED32>(std::strlen(r.data))) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    Server server;
    REQUIRE(server.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = server.port();

    ADSHANDLE hRC = remote_connect(dir, port);
    ADSHANDLE hRT = 0;
    REQUIRE(AdsOpenTable(hRC, tname, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hRT) == 0);

    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hRT, 1, &hOrd) == 0);
    REQUIRE(hOrd != 0);

    // Scope to group "A" without an explicit AdsSetIndexOrderByHandle —
    // mirrors OrdSetFocus + OrdScope where the client already tracks
    // active_index_id from auto-opened production CDX.
    UNSIGNED8 top[] = "A";
    UNSIGNED8 bot[] = "A";
    REQUIRE(AdsSetScope(hOrd, ADS_TOP, top, 1, ADS_STRINGKEY) == 0);
    REQUIRE(AdsSetScope(hOrd, ADS_BOTTOM, bot, 1, ADS_STRINGKEY) == 0);

    auto visible_count = [&](ADSHANDLE nav) -> int {
        REQUIRE(AdsGotoTop(nav) == 0);
        int n = 0;
        for (;;) {
            UNSIGNED16 eof = 0;
            REQUIRE(AdsAtEOF(hRT, &eof) == 0);
            if (eof) break;
            ++n;
            REQUIRE(AdsSkip(nav, 1) == 0);
        }
        return n;
    };

    CHECK(visible_count(hOrd) == 3);
    CHECK(visible_count(hRT) == 3);

    REQUIRE(AdsClearScope(hOrd, ADS_TOP) == 0);
    REQUIRE(AdsClearScope(hOrd, ADS_BOTTOM) == 0);
    CHECK(visible_count(hOrd) == 4);

    REQUIRE(AdsCloseTable(hRT) == 0);
    REQUIRE(AdsDisconnect(hRC) == 0);
    fs::remove_all(dir, ec);
    server.stop();
}

// M12.31 — SET DELETED ON must propagate to the server so a scoped
// index walk skips rows flagged deleted. Without ShowDeleted over the
// wire the server always walked with show_deleted=true.
TEST_CASE("remote AdsSetScope with SET DELETED ON skips deleted rows") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_remote_scope_del";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512];
    const auto sp = dir.string();
    std::memcpy(srv, sp.c_str(), sp.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 fields[] = "GRP,Character,1;DATA,Character,8";
    UNSIGNED8 tname[]  = "item.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 64, fields, &hT) == 0);
    UNSIGNED8 bag[]  = "item.cdx";
    UNSIGNED8 tag[]  = "BYGRP";
    UNSIGNED8 expr[] = "GRP";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, 0, 512, &hIdx) == 0);
    struct Row { const char* grp; const char* data; };
    const Row rows[] = {
        {"A", "live1"}, {"A", "gone"}, {"A", "live2"}, {"B", "other"},
    };
    for (const auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        UNSIGNED8 fg[] = "GRP";
        UNSIGNED8 fd[] = "DATA";
        REQUIRE(AdsSetString(hT, fg, (UNSIGNED8*)r.grp, 1) == 0);
        REQUIRE(AdsSetString(hT, fd, (UNSIGNED8*)r.data,
                             static_cast<UNSIGNED32>(std::strlen(r.data))) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    AdsShowDeleted(1);
    REQUIRE(AdsGotoRecord(hT, 2) == 0);
    REQUIRE(AdsDeleteRecord(hT) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    Server server;
    REQUIRE(server.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = server.port();

    ADSHANDLE hRC = remote_connect(dir, port);
    ADSHANDLE hRT = 0;
    REQUIRE(AdsOpenTable(hRC, tname, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hRT) == 0);

    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hRT, 1, &hOrd) == 0);
    REQUIRE(hOrd != 0);

    UNSIGNED8 top[] = "A";
    UNSIGNED8 bot[] = "A";
    REQUIRE(AdsSetScope(hOrd, ADS_TOP, top, 1, ADS_STRINGKEY) == 0);
    REQUIRE(AdsSetScope(hOrd, ADS_BOTTOM, bot, 1, ADS_STRINGKEY) == 0);
    REQUIRE(AdsShowDeleted(0) == 0);

    REQUIRE(AdsGotoTop(hOrd) == 0);
    int n = 0;
    for (;;) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hRT, &eof) == 0);
        if (eof) break;
        UNSIGNED16 del = 0;
        REQUIRE(AdsIsRecordDeleted(hRT, &del) == 0);
        CHECK(del == 0);
        ++n;
        REQUIRE(AdsSkip(hOrd, 1) == 0);
    }
    CHECK(n == 2);

    REQUIRE(AdsCloseTable(hRT) == 0);
    REQUIRE(AdsDisconnect(hRC) == 0);
    fs::remove_all(dir, ec);
    server.stop();
}

// M12.32 — SET DELETED ON issued BEFORE AdsConnect60 (the usual rddads
// startup order: SET DELETED ON in Main, then AdsConnect). The M12.31
// AdsShowDeleted broadcast only reached connections open at call time,
// and the server's lazy ABI connection (created at the first SetScope /
// SetOrder) defaulted to show_deleted=true — so scoped walks on remote
// aliases still returned deleted rows.
TEST_CASE("remote scoped walk honours SET DELETED ON issued before connect") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_remote_scope_del_pre";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512];
    const auto sp = dir.string();
    std::memcpy(srv, sp.c_str(), sp.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 fields[] = "GRP,Character,1;DATA,Character,8";
    UNSIGNED8 tname[]  = "item.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 64, fields, &hT) == 0);
    UNSIGNED8 bag[]  = "item.cdx";
    UNSIGNED8 tag[]  = "BYGRP";
    UNSIGNED8 expr[] = "GRP";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, 0, 512, &hIdx) == 0);
    struct Row { const char* grp; const char* data; };
    const Row rows[] = {
        {"A", "live1"}, {"A", "gone"}, {"A", "live2"}, {"B", "other"},
    };
    for (const auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        UNSIGNED8 fg[] = "GRP";
        UNSIGNED8 fd[] = "DATA";
        REQUIRE(AdsSetString(hT, fg, (UNSIGNED8*)r.grp, 1) == 0);
        REQUIRE(AdsSetString(hT, fd, (UNSIGNED8*)r.data,
                             static_cast<UNSIGNED32>(std::strlen(r.data))) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    AdsShowDeleted(1);
    REQUIRE(AdsGotoRecord(hT, 2) == 0);
    REQUIRE(AdsDeleteRecord(hT) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    Server server;
    REQUIRE(server.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = server.port();

    // SET DELETED ON before the remote connection exists.
    REQUIRE(AdsShowDeleted(0) == 0);

    ADSHANDLE hRC = remote_connect(dir, port);
    ADSHANDLE hRT = 0;
    REQUIRE(AdsOpenTable(hRC, tname, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hRT) == 0);

    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hRT, 1, &hOrd) == 0);
    REQUIRE(hOrd != 0);

    UNSIGNED8 top[] = "A";
    UNSIGNED8 bot[] = "A";
    REQUIRE(AdsSetScope(hOrd, ADS_TOP, top, 1, ADS_STRINGKEY) == 0);
    REQUIRE(AdsSetScope(hOrd, ADS_BOTTOM, bot, 1, ADS_STRINGKEY) == 0);

    REQUIRE(AdsGotoTop(hOrd) == 0);
    int n = 0;
    for (;;) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hRT, &eof) == 0);
        if (eof) break;
        UNSIGNED16 del = 0;
        REQUIRE(AdsIsRecordDeleted(hRT, &del) == 0);
        CHECK(del == 0);
        ++n;
        REQUIRE(AdsSkip(hOrd, 1) == 0);
    }
    CHECK(n == 2);

    AdsShowDeleted(1);
    REQUIRE(AdsCloseTable(hRT) == 0);
    REQUIRE(AdsDisconnect(hRC) == 0);
    fs::remove_all(dir, ec);
    server.stop();
}

// M12.32 — SET DELETED ON after connect but before the first ordered
// operation: the ShowDeleted opcode arrived while the server's lazy ABI
// connection didn't exist yet, so ensure_abi_conn later created it with
// the default show_deleted=true and the scoped walk leaked deleted rows.
TEST_CASE("remote scoped walk honours SET DELETED ON before first ordered op") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_remote_scope_del_lazy";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512];
    const auto sp = dir.string();
    std::memcpy(srv, sp.c_str(), sp.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 fields[] = "GRP,Character,1;DATA,Character,8";
    UNSIGNED8 tname[]  = "item.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 64, fields, &hT) == 0);
    UNSIGNED8 bag[]  = "item.cdx";
    UNSIGNED8 tag[]  = "BYGRP";
    UNSIGNED8 expr[] = "GRP";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, 0, 512, &hIdx) == 0);
    struct Row { const char* grp; const char* data; };
    const Row rows[] = {
        {"A", "live1"}, {"A", "gone"}, {"A", "live2"}, {"B", "other"},
    };
    for (const auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        UNSIGNED8 fg[] = "GRP";
        UNSIGNED8 fd[] = "DATA";
        REQUIRE(AdsSetString(hT, fg, (UNSIGNED8*)r.grp, 1) == 0);
        REQUIRE(AdsSetString(hT, fd, (UNSIGNED8*)r.data,
                             static_cast<UNSIGNED32>(std::strlen(r.data))) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    AdsShowDeleted(1);
    REQUIRE(AdsGotoRecord(hT, 2) == 0);
    REQUIRE(AdsDeleteRecord(hT) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    Server server;
    REQUIRE(server.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = server.port();

    ADSHANDLE hRC = remote_connect(dir, port);
    ADSHANDLE hRT = 0;
    REQUIRE(AdsOpenTable(hRC, tname, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hRT) == 0);

    // SET DELETED ON reaches the server before any SetScope/SetOrder has
    // created the lazy ABI connection.
    REQUIRE(AdsShowDeleted(0) == 0);

    ADSHANDLE hOrd = 0;
    REQUIRE(AdsGetIndexHandleByOrder(hRT, 1, &hOrd) == 0);
    REQUIRE(hOrd != 0);

    UNSIGNED8 top[] = "A";
    UNSIGNED8 bot[] = "A";
    REQUIRE(AdsSetScope(hOrd, ADS_TOP, top, 1, ADS_STRINGKEY) == 0);
    REQUIRE(AdsSetScope(hOrd, ADS_BOTTOM, bot, 1, ADS_STRINGKEY) == 0);

    REQUIRE(AdsGotoTop(hOrd) == 0);
    int n = 0;
    for (;;) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hRT, &eof) == 0);
        if (eof) break;
        UNSIGNED16 del = 0;
        REQUIRE(AdsIsRecordDeleted(hRT, &del) == 0);
        CHECK(del == 0);
        ++n;
        REQUIRE(AdsSkip(hOrd, 1) == 0);
    }
    CHECK(n == 2);

    AdsShowDeleted(1);
    REQUIRE(AdsCloseTable(hRT) == 0);
    REQUIRE(AdsDisconnect(hRC) == 0);
    fs::remove_all(dir, ec);
    server.stop();
}

// M12.28 — AdsGetDate over the wire. Before the fix, rddads resolved
// Date-type field access to AdsGetDate(hOrdCurrent, ...) which passed
// the RemoteIndex handle to AdsGetField — but AdsGetField only checks
// get_remote_table(), so the remote path was skipped entirely and the
// function fell through to a null local table pointer -> crash.
TEST_CASE("remote AdsGetDate on Date field via index handle does not crash") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_remote_date";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // Stage: create table with a Date field + production index.
    UNSIGNED8 srv[512];
    const auto sp = dir.string();
    std::memcpy(srv, sp.c_str(), sp.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 fields[] = "NOME,Character,20;WHEN,Date,8";
    UNSIGNED8 tname[]  = "dttest.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 64, fields, &hT) == 0);

    // Insert 3 rows with dates.
    struct Row { const char* nome; const char* when; };
    const Row rows[] = {
        {"Alpha", "2024-01-15"},
        {"Beta",  "2025-06-20"},
        {"Gamma", "2026-12-31"},
    };
    for (const auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        UNSIGNED8 fNome[] = "NOME";
        AdsSetString(hT, fNome, (UNSIGNED8*)r.nome,
                     static_cast<UNSIGNED32>(std::strlen(r.nome)));
        UNSIGNED8 fWhen[] = "WHEN";
        AdsSetDate(hT, fWhen, (UNSIGNED8*)r.when,
                   static_cast<UNSIGNED16>(std::strlen(r.when)));
        REQUIRE(AdsWriteRecord(hT) == 0);
    }

    // Create an order tag on NOME for navigation.
    UNSIGNED8 bag[]  = "dttest.cdx";
    UNSIGNED8 tag[]  = "TNOME";
    UNSIGNED8 expr[] = "NOME";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hT, bag, tag, expr,
                             nullptr, nullptr, 0, 512, &hIdx) == 0);

    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    // --- Connect remotely ---
    Server server;
    REQUIRE(server.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = server.port();

    ADSHANDLE hRC = remote_connect(dir, port);
    ADSHANDLE hRT = 0;
    REQUIRE(AdsOpenTable(hRC, tname, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hRT) == 0);

    // Navigate to the first record via the index handle.
    ADSHANDLE hOrd = 0;
    UNSIGNED8 want_tag[] = "TNOME";
    REQUIRE(AdsGetIndexHandle(hRT, want_tag, &hOrd) == 0);
    REQUIRE(hOrd != 0);
    REQUIRE(AdsGotoTop(hOrd) == 0);

    // AdsGetField on the TABLE handle should work (baseline).
    UNSIGNED8 out[32] = {0};
    UNSIGNED32 cap = sizeof(out);
    UNSIGNED8 fNome[] = "NOME";
    REQUIRE(AdsGetField(hRT, fNome, out, &cap, 0) == 0);
    std::string nomeVal(reinterpret_cast<char*>(out), cap);
    CHECK(nomeVal.substr(0, 5) == "Alpha");

    // AdsGetDate via the INDEX handle — this was the crash path.
    // Before the fix, hOrd (RemoteIndex) was passed to AdsGetField
    // which didn't check get_remote_index() -> segfault.
    UNSIGNED8 dtBuf[32] = {0};
    UNSIGNED16 dtLen = sizeof(dtBuf);
    UNSIGNED8 fWhen[] = "WHEN";
    REQUIRE(AdsGetDate(hOrd, fWhen, dtBuf, &dtLen) == 0);
    std::string dateStr(reinterpret_cast<char*>(dtBuf), dtLen);
    CHECK(dateStr == "20240115");

    // Also verify AdsGetDate via the TABLE handle works.
    std::memset(dtBuf, 0, sizeof(dtBuf));
    dtLen = sizeof(dtBuf);
    REQUIRE(AdsGetDate(hRT, fWhen, dtBuf, &dtLen) == 0);
    dateStr = std::string(reinterpret_cast<char*>(dtBuf), dtLen);
    CHECK(dateStr == "20240115");

    // Walk all 3 records via the index, read Date from each.
    std::vector<std::string> dates;
    for (int i = 0; i < 3; ++i) {
        std::memset(dtBuf, 0, sizeof(dtBuf));
        dtLen = sizeof(dtBuf);
        REQUIRE(AdsGetDate(hOrd, fWhen, dtBuf, &dtLen) == 0);
        dates.emplace_back(reinterpret_cast<char*>(dtBuf), dtLen);
        if (i < 2) AdsSkip(hOrd, 1);
    }
    CHECK(dates.size() == 3u);
    CHECK(dates[0] == "20240115");
    CHECK(dates[1] == "20250620");
    CHECK(dates[2] == "20261231");

    REQUIRE(AdsCloseTable(hRT) == 0);
    REQUIRE(AdsDisconnect(hRC) == 0);
    fs::remove_all(dir, ec);
    server.stop();
}
// M12.33 — natural-order multi-record Skip with SET DELETED ON. The
// engine computed the landing as recno + delta (physical) and then only
// slid further while sitting ON a deleted row, so every deleted row
// strictly inside the range left the cursor one visible row short of
// where a Clipper SKIP N lands.
TEST_CASE("local natural-order Skip(N) counts visible rows under SET DELETED ON") {
    auto dir = fs::temp_directory_path() / "openads_local_skip_deleted";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512];
    const auto sp = dir.string();
    std::memcpy(srv, sp.c_str(), sp.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 fields[] = "DATA,Character,8";
    UNSIGNED8 tname[]  = "skipdel.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 64, fields, &hT) == 0);
    // recno: 1 L1, 2 gone, 3 L2, 4 L3, 5 gone, 6 L4, 7 L5
    const char* vals[] = {"L1", "gone", "L2", "L3", "gone", "L4", "L5"};
    for (const char* v : vals) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        UNSIGNED8 fd[] = "DATA";
        REQUIRE(AdsSetString(hT, fd, (UNSIGNED8*)v,
                             static_cast<UNSIGNED32>(std::strlen(v))) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    AdsShowDeleted(1);
    for (UNSIGNED32 rec : {2u, 5u}) {
        REQUIRE(AdsGotoRecord(hT, rec) == 0);
        REQUIRE(AdsDeleteRecord(hT) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }

    REQUIRE(AdsShowDeleted(0) == 0);
    // Visible rows: 1, 3, 4, 6, 7.
    UNSIGNED32 rn = 0;
    REQUIRE(AdsGotoTop(hT) == 0);
    REQUIRE(AdsGetRecordNum(hT, 0, &rn) == 0);
    REQUIRE(rn == 1);

    // SKIP 2 from rec 1: visible steps 3, 4 -> rec 4.
    REQUIRE(AdsSkip(hT, 2) == 0);
    REQUIRE(AdsGetRecordNum(hT, 0, &rn) == 0);
    CHECK(rn == 4);

    // SKIP -3 from rec 7: visible steps 6, 4, 3 -> rec 3.
    REQUIRE(AdsGotoRecord(hT, 7) == 0);
    REQUIRE(AdsSkip(hT, -3) == 0);
    REQUIRE(AdsGetRecordNum(hT, 0, &rn) == 0);
    CHECK(rn == 3);

    // SKIP 3 from rec 4: visible steps 6, 7, phantom -> EOF.
    REQUIRE(AdsGotoRecord(hT, 4) == 0);
    REQUIRE(AdsSkip(hT, 3) == 0);
    UNSIGNED16 eof = 0;
    REQUIRE(AdsAtEOF(hT, &eof) == 0);
    CHECK(eof == 1);

    AdsShowDeleted(1);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// M12.33 — remote natural-order walk with SET DELETED ON. The client's
// prefetch fold (Skip(step + prefetch_consumed)) sends multi-record
// skips over the wire; with the physical-arithmetic Skip bug above the
// server landed one visible row short per deleted row in range and
// re-served a row the client had already displayed — the "deleted
// record duplicates the previous item" browse symptom.
TEST_CASE("remote natural-order walk under SET DELETED ON has no duplicate rows") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_remote_skip_del_dup";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512];
    const auto sp = dir.string();
    std::memcpy(srv, sp.c_str(), sp.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 fields[] = "DATA,Character,8";
    UNSIGNED8 tname[]  = "item.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 64, fields, &hT) == 0);
    // recno: 1 live1, 2 live2, 3 gone (deleted), 4 live3, 5 live4
    const char* vals[] = {"live1", "live2", "gone", "live3", "live4"};
    for (const char* v : vals) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        UNSIGNED8 fd[] = "DATA";
        REQUIRE(AdsSetString(hT, fd, (UNSIGNED8*)v,
                             static_cast<UNSIGNED32>(std::strlen(v))) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    AdsShowDeleted(1);
    REQUIRE(AdsGotoRecord(hT, 3) == 0);
    REQUIRE(AdsDeleteRecord(hT) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    Server server;
    REQUIRE(server.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = server.port();

    // SET DELETED ON before connect — the rddads startup order.
    REQUIRE(AdsShowDeleted(0) == 0);

    ADSHANDLE hRC = remote_connect(dir, port);
    ADSHANDLE hRT = 0;
    REQUIRE(AdsOpenTable(hRC, tname, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hRT) == 0);

    // Browse-style walk: GoTop then Skip(1) until EOF, collecting the
    // recno of every row the client would paint.
    REQUIRE(AdsGotoTop(hRT) == 0);
    std::vector<UNSIGNED32> seen;
    for (int guard = 0; guard < 10; ++guard) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hRT, &eof) == 0);
        if (eof) break;
        UNSIGNED32 rn = 0;
        REQUIRE(AdsGetRecordNum(hRT, 0, &rn) == 0);
        seen.push_back(rn);
        REQUIRE(AdsSkip(hRT, 1) == 0);
    }
    REQUIRE(seen.size() == 4);
    CHECK(seen[0] == 1);
    CHECK(seen[1] == 2);
    CHECK(seen[2] == 4);
    CHECK(seen[3] == 5);

    AdsShowDeleted(1);
    REQUIRE(AdsCloseTable(hRT) == 0);
    REQUIRE(AdsDisconnect(hRC) == 0);
    fs::remove_all(dir, ec);
    server.stop();
}

// M12.33 — same walk over a table longer than the prefetch lookahead
// window (64 rows). The folded resync Skip(1 + 64) crosses the deleted
// row, so the physical-arithmetic bug landed the server one visible row
// short — on the exact row the client had just painted — which the
// same-recno nav heuristic then misread as EOF, truncating the walk.
TEST_CASE("remote natural-order walk longer than prefetch window under SET DELETED ON") {
    using openads::network::Server;
    auto dir = fs::temp_directory_path() / "openads_remote_skip_del_long";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    UNSIGNED8 srv[512];
    const auto sp = dir.string();
    std::memcpy(srv, sp.c_str(), sp.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);

    UNSIGNED8 fields[] = "DATA,Character,8";
    UNSIGNED8 tname[]  = "items.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                           0, 0, 0, 64, fields, &hT) == 0);
    for (int i = 1; i <= 70; ++i) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        UNSIGNED8 fd[] = "DATA";
        char buf[16];
        std::snprintf(buf, sizeof(buf), "it%05d", i);
        REQUIRE(AdsSetString(hT, fd, (UNSIGNED8*)buf,
                             static_cast<UNSIGNED32>(std::strlen(buf))) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    AdsShowDeleted(1);
    REQUIRE(AdsGotoRecord(hT, 3) == 0);
    REQUIRE(AdsDeleteRecord(hT) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);

    Server server;
    REQUIRE(server.start("127.0.0.1", 0).has_value());
    const std::uint16_t port = server.port();

    REQUIRE(AdsShowDeleted(0) == 0);

    ADSHANDLE hRC = remote_connect(dir, port);
    ADSHANDLE hRT = 0;
    REQUIRE(AdsOpenTable(hRC, tname, nullptr, ADS_CDX,
                         0, 0, 0, 0, &hRT) == 0);

    REQUIRE(AdsGotoTop(hRT) == 0);
    std::vector<UNSIGNED32> seen;
    for (int guard = 0; guard < 100; ++guard) {
        UNSIGNED16 eof = 0;
        REQUIRE(AdsAtEOF(hRT, &eof) == 0);
        if (eof) break;
        UNSIGNED32 rn = 0;
        REQUIRE(AdsGetRecordNum(hRT, 0, &rn) == 0);
        seen.push_back(rn);
        REQUIRE(AdsSkip(hRT, 1) == 0);
    }
    // Visible rows: 1, 2, 4..70 — 69 of them, each exactly once.
    REQUIRE(seen.size() == 69);
    UNSIGNED32 expect = 1;
    for (std::size_t i = 0; i < seen.size(); ++i) {
        if (expect == 3) expect = 4;
        CHECK(seen[i] == expect);
        ++expect;
    }

    AdsShowDeleted(1);
    REQUIRE(AdsCloseTable(hRT) == 0);
    REQUIRE(AdsDisconnect(hRC) == 0);
    fs::remove_all(dir, ec);
    server.stop();
}
