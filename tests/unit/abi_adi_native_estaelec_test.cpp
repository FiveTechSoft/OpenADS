// NATIVE-ADI ORACLE for the ESTAELEC index patterns (the ERP's real index set),
// WITHOUT the CDX reroute (OPENADS_ADT_CDX_INDEX is NOT set). This pins what the
// genuine OpenADS AdiIndex driver must do once reworked:
//
//   ORD1  cCodigoCon+cDocumeTra            (compound concat)
//   ORD2  cCodigoCli                        (single field)
//   ORD3  cPreFijTra+cDocumeTra            (compound concat — distinct from ORD1)
//   ORD4  DTOS(dFecTraTra)                  (computed)
//   ORD5  DTOS(dFecTraTra) FOR cCorEnvEle != 'S'  (computed + conditional)
//
// STATUS WHEN WRITTEN (2026-06-27): EXPECTED TO FAIL — the native AdiIndex indexes
// by field only (no expression / FOR / tag-name identity, recno too narrow). This
// is the target oracle for the AdiIndex rework. As each gap lands the matching
// CHECK goes green; when ALL pass, drop the should_fail decorator. The CDX mirror
// (abi_cdx_estaelec_compound_test.cpp) proves the ERP pattern is correct on CDX.
#include <cstdlib>
#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

namespace {
// The v2 tag layout is opt-in (OPENADS_ADI_V2). These cases exercise it, so
// they turn it on for their own duration and put it back afterwards — the rest
// of the suite must keep running against the legacy layout.
struct AdiV2Scope {
    AdiV2Scope() {
#ifdef _WIN32
        _putenv_s("OPENADS_ADI_V2", "1");
#else
        setenv("OPENADS_ADI_V2", "1", 1);
#endif
    }
    ~AdiV2Scope() {
#ifdef _WIN32
        _putenv_s("OPENADS_ADI_V2", "");
#else
        unsetenv("OPENADS_ADI_V2");
#endif
    }
};
} // namespace

namespace fs = std::filesystem;

namespace {
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

TEST_CASE("native AdiIndex handles ESTAELEC compound/computed/conditional tags") {
    AdiV2Scope _v2;
    // NO EnvGuard: exercise the genuine AdiIndex path (not the CDX reroute).
    fs::path dir = fs::temp_directory_path() / "openads_adi_native_estaelec";
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir);

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

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

    UNSIGNED16 nidx = 0;
    REQUIRE(AdsGetNumIndexes(hT, &nidx) == AE_SUCCESS);
    CHECK(nidx == 5);

    REQUIRE(AdsSetIndexOrderByHandle(hT, o1) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(recno(hT) == 1u);
    REQUIRE(AdsSetIndexOrderByHandle(hT, o3) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(recno(hT) == 4u);

    REQUIRE(AdsSetIndexOrderByHandle(hT, o4) == AE_SUCCESS);
    REQUIRE(AdsGotoTop(hT) == AE_SUCCESS);
    CHECK(recno(hT) == 2u);

    UNSIGNED32 kc = 0;
    REQUIRE(AdsGetKeyCount(o5, 0, &kc) == AE_SUCCESS);
    CHECK(kc == 2u);

    REQUIRE(AdsSetIndexOrderByHandle(hT, o1) == AE_SUCCESS);
    UNSIGNED8 key[] = "00100000030";
    UNSIGNED16 found = 0;
    REQUIRE(AdsSeek(o1, key, 11, ADS_STRINGKEY, 0, &found) == AE_SUCCESS);
    CHECK(found != 0);
    CHECK(recno(hT) == 3u);

    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);

    // Reopen: auto-open must rebind the 5 distinct tags BY NAME.
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

// Tag ORDINALS must follow creation order (ordinal 1 = first created), matching
// CDX/DBFCDX, because the ERP's xxBrowse column-click ordering (ordenaColumna)
// does DBSETORDER(n)/ORDNAME(n) by tag NUMBER. SAP's .adi prepends new tags
// (reversing ordinals) — add_tag must APPEND so DBSETORDER(1) picks the FIRST
// created tag, not the last.
TEST_CASE("native AdiIndex tag ordinals follow creation order (xxBrowse DBSETORDER)") {
    AdiV2Scope _v2;
    fs::path dir = fs::temp_directory_path() / "openads_adi_ordinal";
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir);
    UNSIGNED8 srv[260]{};
    std::memcpy(srv, dir.string().c_str(), dir.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);
    UNSIGNED8 tbl[] = "ord.adt";
    UNSIGNED8 def[] = "CCODE,Character,4;CNAME,Character,10;CCITY,Character,8";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0, def, &hT)
            == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    fs::rename(dir / "ord.adt", dir / "ord.DAT", ec); REQUIRE(!ec);
    UNSIGNED8 dat[] = "ord.DAT";
    hT = 0;
    REQUIRE(AdsOpenTable(hConn, dat, nullptr, ADS_ADT, 0, 0, 0, ADS_DEFAULT, &hT)
            == AE_SUCCESS);
    for (int i = 1; i <= 3; ++i) {
        REQUIRE(AdsAppendRecord(hT) == AE_SUCCESS);
        set_c(hT, "CCODE", ("C" + std::to_string(i)).c_str());
        set_c(hT, "CNAME", ("N" + std::to_string(i)).c_str());
        set_c(hT, "CCITY", ("Y" + std::to_string(i)).c_str());
        REQUIRE(AdsWriteRecord(hT) == AE_SUCCESS);
    }
    std::string bag = (dir / "ord.adi").string();
    make_tag(hT, bag.c_str(), "TAGCODE", "CCODE", nullptr);   // ordinal 1
    make_tag(hT, bag.c_str(), "TAGNAME", "CNAME", nullptr);   // ordinal 2
    make_tag(hT, bag.c_str(), "TAGCITY", "CCITY", nullptr);   // ordinal 3
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);

    hT = 0;
    REQUIRE(AdsOpenTable(hConn, dat, nullptr, ADS_ADT, 0, 0, 0, ADS_DEFAULT, &hT)
            == AE_SUCCESS);
    UNSIGNED8 b[260]{}; std::memcpy(b, bag.c_str(), bag.size());
    ADSHANDLE arr[16] = {0}; UNSIGNED16 alen = 16;
    REQUIRE(AdsOpenIndex(hT, b, arr, &alen) == AE_SUCCESS);
    REQUIRE(alen == 3);
    auto iname = [](ADSHANDLE h) {
        UNSIGNED8 nm[64]{}; UNSIGNED16 nl = 64;
        AdsGetIndexName(h, nm, &nl);
        return std::string(reinterpret_cast<const char*>(nm));
    };
    CHECK(iname(arr[0]) == "TAGCODE");  // DBSETORDER(1) -> first created
    CHECK(iname(arr[1]) == "TAGNAME");  // DBSETORDER(2)
    CHECK(iname(arr[2]) == "TAGCITY");  // DBSETORDER(3)
    REQUIRE(AdsCloseTable(hT) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);
    fs::remove_all(dir, ec);
}
