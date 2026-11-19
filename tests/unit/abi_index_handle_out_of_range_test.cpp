// IndexKey(n)/OrdName(n) past the last tag must fail, not return tag 1.
// Harbour rddads answers "" only when AdsGetIndexHandleByOrder fails, and
// FiveWin's error handler runs  while !Empty(IndexKey(j))  (the reporting user).
#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"
#include <filesystem>
#include <string>
#include <cstdlib>
namespace {
struct OorEnv {
    const char* n_;
    OorEnv(const char* n, const char* v) : n_(n) {
#ifdef _WIN32
        _putenv_s(n, v);
#else
        setenv(n, v, 1);
#endif
    }
    ~OorEnv() {
#ifdef _WIN32
        _putenv_s(n_, "");
#else
        unsetenv(n_);
#endif
    }
};
UNSIGNED8* oor_b(std::string& s) { return reinterpret_cast<UNSIGNED8*>(s.data()); }
void oor_make(const std::filesystem::path& dir, UNSIGNED16 type) {
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::string path = dir.string(), nm = type == ADS_ADT ? "oor.adt" : "oor.dbf";
    std::string bag = type == ADS_ADT ? "oor.adi" : "oor.cdx";
    std::string defs = "NAME,Character,10;CITY,Character,10";
    ADSHANDLE c = 0, t = 0;
    REQUIRE(AdsConnect60(oor_b(path), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &c) == 0);
    REQUIRE(AdsCreateTable(c, oor_b(nm), nullptr, type, ADS_ANSI, ADS_PROPRIETARY_LOCKING, 0, 0, oor_b(defs), &t) == 0);
    REQUIRE(AdsAppendRecord(t) == 0);
    std::string v = "A", f1 = "NAME", f2 = "CITY";
    REQUIRE(AdsSetString(t, oor_b(f1), oor_b(v), 1) == 0);
    REQUIRE(AdsSetString(t, oor_b(f2), oor_b(v), 1) == 0);
    REQUIRE(AdsWriteRecord(t) == 0);
    const char* ex[3] = {"NAME", "CITY", "NAME+CITY"};
    for (int k = 0; k < 3; ++k) {
        std::string tag = "TAG" + std::to_string(k + 1), e = ex[k]; ADSHANDLE ix = 0;
        REQUIRE(AdsCreateIndex61(t, oor_b(bag), oor_b(tag), oor_b(e), nullptr, nullptr, 0, 0, &ix) == 0);
    }
    AdsCloseTable(t); AdsDisconnect(c);
}
void oor_check(const std::string& conn, UNSIGNED16 ctype, UNSIGNED16 ftype, const std::string& name) {
    std::string cs = conn, nm = name; ADSHANDLE c = 0, t = 0;
    REQUIRE(AdsConnect60(oor_b(cs), ctype, nullptr, nullptr, 0, &c) == 0);
    REQUIRE(AdsOpenTable(c, oor_b(nm), nullptr, ftype, ADS_ANSI, ADS_PROPRIETARY_LOCKING, 0, 0, &t) == 0);
    UNSIGNED16 n = 0; REQUIRE(AdsGetNumIndexes(t, &n) == 0); REQUIRE(n == 3);
    for (UNSIGNED16 o = 1; o <= 3; ++o) {
        ADSHANDLE h = 0; REQUIRE(AdsGetIndexHandleByOrder(t, o, &h) == 0);
        UNSIGNED8 b[64] = {0}; UNSIGNED16 l = 64; REQUIRE(AdsGetIndexName(h, b, &l) == 0);
        CHECK(std::string(reinterpret_cast<char*>(b)) == "TAG" + std::to_string(o));
    }
    for (UNSIGNED16 o : {UNSIGNED16(4), UNSIGNED16(5), UNSIGNED16(100)}) {
        ADSHANDLE h = 0xDEAD;
        CHECK(AdsGetIndexHandleByOrder(t, o, &h) != 0);
    }
    ADSHANDLE h0 = 0; CHECK(AdsGetIndexHandleByOrder(t, 0, &h0) == 0);  // order 0 unchanged
    AdsCloseTable(t); AdsDisconnect(c);
}
}  // namespace
TEST_CASE("AdsGetIndexHandleByOrder fails past the last tag (CDX, local and remote)") {
    auto dir = std::filesystem::temp_directory_path() / "openads_oor_cdx";
    oor_make(dir, ADS_CDX);
    oor_check(dir.string(), ADS_LOCAL_SERVER, ADS_CDX, "oor.dbf");
    openads::network::Server srv; REQUIRE(srv.start("127.0.0.1", 0).has_value());
    oor_check("tcp://127.0.0.1:" + std::to_string(srv.port()) + "/" + dir.string(), ADS_REMOTE_SERVER, ADS_CDX, "oor.dbf");
    srv.stop();
}
TEST_CASE("AdsGetIndexHandleByOrder fails past the last tag (ADT, local and remote)") {
    OorEnv cdx_adt("OPENADS_ADT_CDX_INDEX", "1");  // CDX-format .adi bag, as deployed with CDX-format bags
    auto dir = std::filesystem::temp_directory_path() / "openads_oor_adt";
    oor_make(dir, ADS_ADT);
    oor_check(dir.string(), ADS_LOCAL_SERVER, ADS_ADT, "oor.adt");
    openads::network::Server srv; REQUIRE(srv.start("127.0.0.1", 0).has_value());
    oor_check("tcp://127.0.0.1:" + std::to_string(srv.port()) + "/" + dir.string(), ADS_REMOTE_SERVER, ADS_ADT, "oor.adt");
    srv.stop();
}
