// SPEC / ORACLE for ADS_ADT (.ADI) expression-index support — the ADT mirror of
// abi_cdx_estaelec_compound_test.cpp. This DEFINES "correct" for the ADI driver:
// the same ERP index patterns (UTILIDAD.PRG ESTAELEC) must work on an ADT table
// stored as .DAT with a .ADI bag, exactly as they do on CDX.
//
//   ORD1  cCodigoCon+cDocumeTra           (compound concat)
//   ORD2  cCodigoCli                       (single field)
//   ORD3  cPreFijTra+cDocumeTra           (compound concat — must stay distinct
//                                           from ORD1; today they collide on
//                                           field 0 in the ADI driver)
//   ORD4  DTOS(dFecTraTra)                 (computed)
//   ORD5  DTOS(dFecTraTra) FOR cCorEnvEle != 'S'   (computed + conditional)
//
// STATUS WHEN WRITTEN (2026-06-27): EXPECTED TO FAIL — the ADI driver indexes by
// field only (no expression / FOR / tag-name identity). This test is the target
// for the ADI expression-support work. As that lands, this should go green
// WITHOUT changing the CDX equivalent's expectations.

#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

// Sets an env var for the test's lifetime and clears it on exit (leak-proof
// across doctest's shared process, even if a REQUIRE throws).
struct EnvGuard {
    const char* name_;
    EnvGuard(const char* n, const char* v) : name_(n) {
#ifdef _WIN32
        _putenv_s(n, v);
#else
        setenv(n, v, 1);
#endif
    }
    ~EnvGuard() {
#ifdef _WIN32
        _putenv_s(name_, "");
#else
        unsetenv(name_);
#endif
    }
};

void set_c(ADSHANDLE h, const char* field, const char* val) {
    UNSIGNED8 f[32]{}; std::strncpy(reinterpret_cast<char*>(f), field, 31);
    UNSIGNED8 v[32]{}; std::strncpy(reinterpret_cast<char*>(v), val, 31);
    REQUIRE(AdsSetString(h, f, v,
            static_cast<UNSIGNED32>(std::strlen(val))) == AE_SUCCESS);
}

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
    UNSIGNED32 rc = AdsCreateIndex61(hTable, b, t, e, cp, nullptr, 0, 0, &h);
    REQUIRE(rc == AE_SUCCESS);
    return h;
}

} // namespace

// Validates the EXPERIMENTAL "CDX-over-ADT" reroute (env OPENADS_ADT_CDX_INDEX=1):
// an ADT table stored as .DAT with a .ADI bag, indexed via the CdxIndex engine,
// must handle the ERP's ESTAELEC index patterns (compound/computed/FOR) exactly
// as CDX does. The EnvGuard turns the reroute on only for this test.
TEST_CASE("ADI->CDX reroute handles ESTAELEC compound/computed/conditional tags") {
    EnvGuard _cdx_adt("OPENADS_ADT_CDX_INDEX", "1");
    fs::path dir = fs::temp_directory_path() / "openads_adi_estaelec";
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir);

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    // ADT table; rename to .DAT (Russoft convention) and reopen as ADS_ADT.
    UNSIGNED8 tbl[] = "estaelec.adt";
    UNSIGNED8 def[] = "CCODIGOCON,Character,3;CDOCUMETRA,Character,8;"
                      "CCODIGOCLI,Character,10;CPREFIJTRA,Character,4;"
                      "DFECTRATRA,Date,8;CCORENVELE,Character,1";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0, def, &hT)
            == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    fs::rename(dir / "estaelec.adt", dir / "estaelec.DAT", ec);
    REQUIRE(!ec);
    UNSIGNED8 dat[] = "estaelec.DAT";
    hT = 0;
    REQUIRE(AdsOpenTable(hConn, dat, nullptr, ADS_ADT, 0, 0, 0, ADS_DEFAULT, &hT)
            == AE_SUCCESS);

    struct Row { const char* con; const char* doc; const char* cli;
                 const char* pre; const char* fec; const char* cor; };
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

    std::string bags = (dir / "estaelec.adi").string();
    ADSHANDLE o1 = make_tag(hT, bags.c_str(), "ORD1", "CCODIGOCON+CDOCUMETRA", nullptr);
    ADSHANDLE o2 = make_tag(hT, bags.c_str(), "ORD2", "CCODIGOCLI", nullptr);
    ADSHANDLE o3 = make_tag(hT, bags.c_str(), "ORD3", "CPREFIJTRA+CDOCUMETRA", nullptr);
    ADSHANDLE o4 = make_tag(hT, bags.c_str(), "ORD4", "DTOS(DFECTRATRA)", nullptr);
    ADSHANDLE o5 = make_tag(hT, bags.c_str(), "ORD5", "DTOS(DFECTRATRA)", "CCORENVELE != 'S'");
    (void)o2;

    // 5 distinct tags coexist (today: collapse on field 0).
    UNSIGNED16 nidx = 0;
    REQUIRE(AdsGetNumIndexes(hT, &nidx) == AE_SUCCESS);
    CHECK(nidx == 5);

    // ORD1 (con+doc) top = rec1 ; ORD3 (pre+doc) top = rec4 — distinct compound orders.
    REQUIRE(AdsSetIndexOrderByHandle(hT, o1) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(recno(hT) == 1u);
    REQUIRE(AdsSetIndexOrderByHandle(hT, o3) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(recno(hT) == 4u);

    // ORD4 DTOS(date): earliest is rec2.
    REQUIRE(AdsSetIndexOrderByHandle(hT, o4) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(recno(hT) == 2u);

    // ORD5 conditional: only rec2 and rec4 (cor != 'S').
    UNSIGNED32 kc = 0;
    REQUIRE(AdsGetKeyCount(o5, 0, &kc) == AE_SUCCESS);
    CHECK(kc == 2u);

    // Exact seek on compound ORD1 -> rec3.
    REQUIRE(AdsSetIndexOrderByHandle(hT, o1) == AE_SUCCESS);
    UNSIGNED8 key[] = "00100000030";
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(o1, key, 11, ADS_STRINGKEY, 0, &found) == AE_SUCCESS);
    CHECK(found != 0);
    CHECK(recno(hT) == 3u);

    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);

    // Reopen: table auto-open binds the .adi bag through the CDX reroute
    // (AdsOpenIndex path). The 5 orders must persist and stay distinct, addressed
    // by tag NAME (handles are gone after close).
    hT = 0;
    REQUIRE(AdsOpenTable(hConn, dat, nullptr, ADS_ADT, 0, 0, 0, ADS_DEFAULT, &hT)
            == AE_SUCCESS);
    UNSIGNED16 nidx2 = 0;
    REQUIRE(AdsGetNumIndexes(hT, &nidx2) == AE_SUCCESS);
    CHECK(nidx2 == 5);
    REQUIRE(AdsSetIndexOrder(hT, (UNSIGNED8*)"ORD3") == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(recno(hT) == 4u);
    REQUIRE(AdsSetIndexOrder(hT, (UNSIGNED8*)"ORD1") == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(recno(hT) == 1u);

    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    fs::remove_all(dir, ec);
}
