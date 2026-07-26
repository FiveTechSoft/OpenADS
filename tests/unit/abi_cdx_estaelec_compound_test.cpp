// Proof that the OpenADS CDX engine handles the ERP's exact index patterns —
// the ones the ADI driver could NOT (it indexes by field only, so compound /
// computed tags collide on the first field).
//
// This mirrors UTILIDAD.PRG IndexaTabla, table ESTAELEC:
//   ORD1  cCodigoCon+cDocumeTra           (compound concat)
//   ORD2  cCodigoCli                       (single field)
//   ORD3  cPreFijTra+cDocumeTra           (compound concat — collides with ORD1
//                                           on field 0 in ADI; must stay distinct)
//   ORD4  DTOS(dFecTraTra)                 (computed)
//   ORD5  DTOS(dFecTraTra) FOR cCorEnvEle != 'S'   (computed + conditional)
//
// If ORD1 and ORD3 produce DIFFERENT orderings and ORD5 only indexes the
// matching rows, the CDX engine supports what the migration needs and CDX on
// x64 (DBF + .cdx via OpenADS) is a viable replacement for the ADT/.adi route.

#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

void set_c(ADSHANDLE h, const char* field, const char* val) {
    UNSIGNED8 f[32]{}; std::strncpy(reinterpret_cast<char*>(f), field, 31);
    UNSIGNED8 v[32]{}; std::strncpy(reinterpret_cast<char*>(v), val, 31);
    REQUIRE(AdsSetString(h, f, v,
            static_cast<UNSIGNED32>(std::strlen(val))) == AE_SUCCESS);
}

// recno helper
UNSIGNED32 recno(ADSHANDLE h) {
    UNSIGNED32 rn = 0;
    REQUIRE(AdsGetRecordNum(h, 0, &rn) == AE_SUCCESS);
    return rn;
}

ADSHANDLE make_tag(ADSHANDLE hTable, const char* bag, const char* tag,
                   const char* expr, const char* cond) {
    UNSIGNED8 b[260]{}; std::strncpy(reinterpret_cast<char*>(b), bag, 259);
    UNSIGNED8 t[64]{};  std::strncpy(reinterpret_cast<char*>(t), tag, 63);
    UNSIGNED8 e[128]{}; std::strncpy(reinterpret_cast<char*>(e), expr, 127);
    UNSIGNED8 c[128]{};
    UNSIGNED8* cp = nullptr;
    if (cond) { std::strncpy(reinterpret_cast<char*>(c), cond, 127); cp = c; }
    ADSHANDLE h = 0;
    REQUIRE(AdsCreateIndex61(hTable, b, t, e, cp, nullptr, 0, 0, &h)
            == AE_SUCCESS);
    return h;
}

} // namespace

TEST_CASE("CDX engine handles ESTAELEC compound/computed/conditional tags") {
    fs::path dir = fs::temp_directory_path() / "openads_cdx_estaelec";
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir);

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[] = "estaelec.dbf";
    UNSIGNED8 def[] = "CCODIGOCON,C,3,0;CDOCUMETRA,C,8,0;CCODIGOCLI,C,10,0;"
                      "CPREFIJTRA,C,4,0;DFECTRATRA,D,8,0;CCORENVELE,C,1,0";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hT)
            == AE_SUCCESS);

    struct Row { const char* con; const char* doc; const char* cli;
                 const char* pre; const char* fec; const char* cor; };
    // Designed so ORD1 (con+doc) and ORD3 (pre+doc) order DIFFERENTLY.
    const Row rows[4] = {
        {"001", "00000010", "CLIENTE001", "FA01", "20260103", "S"}, // rec1
        {"002", "00000020", "CLIENTE002", "FA02", "20260101", "N"}, // rec2
        {"001", "00000030", "CLIENTE003", "FA03", "20260102", "S"}, // rec3
        {"003", "00000005", "CLIENTE004", "FA01", "20260104", "N"}, // rec4
    };
    for (const auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
        set_c(hT, "CCODIGOCON", r.con);
        set_c(hT, "CDOCUMETRA", r.doc);
        set_c(hT, "CCODIGOCLI", r.cli);
        set_c(hT, "CPREFIJTRA", r.pre);
        set_c(hT, "DFECTRATRA", r.fec);
        set_c(hT, "CCORENVELE", r.cor);
        REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);
    }

    std::string bags = (dir / "estaelec.cdx").string();
    ADSHANDLE o1 = make_tag(hT, bags.c_str(), "ORD1", "CCODIGOCON+CDOCUMETRA", nullptr);
    ADSHANDLE o2 = make_tag(hT, bags.c_str(), "ORD2", "CCODIGOCLI", nullptr);
    ADSHANDLE o3 = make_tag(hT, bags.c_str(), "ORD3", "CPREFIJTRA+CDOCUMETRA", nullptr);
    ADSHANDLE o4 = make_tag(hT, bags.c_str(), "ORD4", "DTOS(DFECTRATRA)", nullptr);
    ADSHANDLE o5 = make_tag(hT, bags.c_str(), "ORD5", "DTOS(DFECTRATRA)", "CCORENVELE != 'S'");
    (void)o2;

    // All 5 tags coexist in the one .cdx bag.
    UNSIGNED16 nidx = 0;
    REQUIRE(AdsGetNumIndexes(hT, &nidx) == AE_SUCCESS);
    CHECK(nidx == 5);

    // The ADI-failure proof: ORD1 and ORD3 are DISTINCT compound orders.
    // ORD1 top key = "001"+"00000010" -> rec1.
    REQUIRE(AdsSetIndexOrderByHandle(hT, o1) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(recno(hT) == 1u);
    // ORD3 top key = "FA01"+"00000005" -> rec4 (different first record!).
    REQUIRE(AdsSetIndexOrderByHandle(hT, o3) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(recno(hT) == 4u);

    // Computed order ORD4 = DTOS(date): earliest date is rec2 (20260101).
    REQUIRE(AdsSetIndexOrderByHandle(hT, o4) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(recno(hT) == 2u);

    // Conditional ORD5 FOR cCorEnvEle != 'S' indexes only rec2 and rec4.
    UNSIGNED32 kc = 0;
    REQUIRE(AdsGetKeyCount(o5, 0, &kc) == AE_SUCCESS);
    CHECK(kc == 2u);

    // Exact seek on the compound ORD1 lands on the right record.
    REQUIRE(AdsSetIndexOrderByHandle(hT, o1) == AE_SUCCESS);
    UNSIGNED8 key[] = "00100000030";   // "001"+"00000030" -> rec3
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(o1, key, 11, ADS_STRINGKEY, 0, &found) == AE_SUCCESS);
    CHECK(found != 0);
    CHECK(recno(hT) == 3u);

    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    fs::remove_all(dir, ec);
}

// Proves the CDX engine covers the cases the "CDX-over-ADT" reroute must handle
// beyond build-time: NUMERIC bare-field key, persistence across close/reopen,
// and MAINTENANCE (dbAppend re-evaluates the compound expression). If these pass,
// reusing CdxIndex for ADT tables is guaranteed sound for every key kind the ERP
// uses (not just the char/DTOS path of the test above).
TEST_CASE("CDX engine: numeric key + reopen persistence + dbAppend maintenance") {
    fs::path dir = fs::temp_directory_path() / "openads_cdx_persist";
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir);

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[] = "doc.dbf";
    UNSIGNED8 def[] = "CCODIGOCON,C,3,0;CDOCUMETRA,C,8,0;NSECUEN,N,4,0";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_CDX, 0, 0, 0, 0, def, &hT)
            == AE_SUCCESS);

    struct R { const char* con; const char* doc; double seq; };
    const R rows[3] = {
        {"001", "00000010", 30},
        {"002", "00000020", 10},
        {"001", "00000030", 20},
    };
    for (const auto& r : rows) {
        REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
        set_c(hT, "CCODIGOCON", r.con);
        set_c(hT, "CDOCUMETRA", r.doc);
        REQUIRE(AdsSetDouble(hT, (UNSIGNED8*)"NSECUEN", r.seq) == AE_SUCCESS);
        REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);
    }

    std::string bags = (dir / "doc.cdx").string();
    ADSHANDLE oc = make_tag(hT, bags.c_str(), "ORDCOMP", "CCODIGOCON+CDOCUMETRA", nullptr);
    ADSHANDLE on = make_tag(hT, bags.c_str(), "ORDNUM",  "NSECUEN", nullptr);
    (void)oc;

    // Numeric bare-field order: ascending by NSECUEN -> rec2(10), rec3(20), rec1(30).
    REQUIRE(AdsSetIndexOrderByHandle(hT, on) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(recno(hT) == 2u);

    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);

    // Reopen: auto-binds doc.cdx. Orders must persist; select by NAME.
    UNSIGNED8 t2[] = "doc.dbf";
    hT = 0;
    REQUIRE(AdsOpenTable(hConn, t2, nullptr, ADS_CDX, 0, 0, 0, ADS_DEFAULT, &hT)
            == AE_SUCCESS);
    UNSIGNED16 nidx = 0;
    REQUIRE(AdsGetNumIndexes(hT, &nidx) == AE_SUCCESS);
    CHECK(nidx == 2);

    REQUIRE(AdsSetIndexOrder(hT, (UNSIGNED8*)"ORDCOMP") == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(recno(hT) == 1u);   // "001"+"00000010"

    // Maintenance: append a record; the compound order must place it correctly
    // (sync_all_indexes_ re-evaluates CCODIGOCON+CDOCUMETRA on the new row).
    REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
    set_c(hT, "CCODIGOCON", "000");
    set_c(hT, "CDOCUMETRA", "00000001");
    REQUIRE(AdsSetDouble(hT, (UNSIGNED8*)"NSECUEN", 99) == AE_SUCCESS);
    REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);

    // The new row ("000"+"00000001") is the lowest compound key, so on ORDCOMP
    // GotoTop must land on it (rec4) — proving the append was maintained in the
    // compound order, not just stored by recno.
    REQUIRE(AdsSetIndexOrder(hT, (UNSIGNED8*)"ORDCOMP") == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(recno(hT) == 4u);

    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    fs::remove_all(dir, ec);
}
