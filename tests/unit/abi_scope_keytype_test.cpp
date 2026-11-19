// Regression: AdsSetScope must detect the key encoding from the ACTIVE
// INDEX's real key type plus the buffer length, not from the caller's
// usDataType constant. The ADS SDK numbers the buffer types
// ADS_RAWKEY=1 / ADS_STRINGKEY=2 / ADS_DOUBLEKEY=4; this header numbers
// them 0/1/2. A caller compiled against the SDK numbering therefore
// passes 2 for a character scope and 4 for a numeric one. Trusting the
// constant misread 8 ASCII scope bytes as an IEEE-754 double (the
// character scope then matched nothing) and dropped real numeric
// scopes onto the raw-string path. ads_seek_local already detects the
// encoding from the active index; AdsSetScope now mirrors that.
#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ADS SDK (ads.h / ads.ch) buffer-type numbering, as sent by a
// SAP-SDK-built caller -- deliberately NOT this header's values.
constexpr UNSIGNED16 SDK_ADS_STRINGKEY = 2;
constexpr UNSIGNED16 SDK_ADS_DOUBLEKEY = 4;

std::vector<UNSIGNED8> cbuf(const std::string& s) {
    std::vector<UNSIGNED8> v(s.begin(), s.end());
    v.push_back(0);
    return v;
}

fs::path fresh_dir(const char* leaf) {
    auto d = fs::temp_directory_path() / leaf;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return d;
}

ADSHANDLE connect_local(const fs::path& dir) {
    auto u = cbuf(dir.string());
    ADSHANDLE h = 0;
    REQUIRE(AdsConnect60(u.data(), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &h) == 0);
    REQUIRE(h != 0);
    return h;
}

void set_scope_raw(ADSHANDLE hN, UNSIGNED16 which, const void* bytes,
                   UNSIGNED16 len, UNSIGNED16 sdk_type) {
    REQUIRE(AdsSetScope(hN, which, (UNSIGNED8*)bytes, len, sdk_type) == 0);
}

UNSIGNED32 key_count(ADSHANDLE hN) {
    UNSIGNED32 n = 0xFFFFFFFFu;
    REQUIRE(AdsGetKeyCount(hN, ADS_RESPECTSCOPES, &n) == 0);
    return n;
}

} // namespace

TEST_CASE("scope keytype: SDK-numbered character scope on a C(8) key bounds by text") {
    auto dir = fresh_dir("openads_scope_keytype_c8");
    ADSHANDLE h = connect_local(dir);
    ADSHANDLE hT = 0, hN = 0;
    auto name   = cbuf("skc");
    auto fields = cbuf("NAME,C,8,0");
    REQUIRE(AdsCreateTable(h, name.data(), nullptr, ADS_CDX, ADS_ANSI,
                           0, 0, 0, fields.data(), &hT) == 0);
    auto bag  = cbuf((dir / "skc.cdx").string());
    auto tag  = cbuf("BYNAME");
    auto expr = cbuf("NAME");
    REQUIRE(AdsCreateIndex61(hT, bag.data(), tag.data(), expr.data(),
                             nullptr, nullptr, 0, 512, &hN) == 0);
    for (int i = 1; i <= 10; ++i) {
        char nm[9];
        std::snprintf(nm, sizeof nm, "KEY%05d", i);   // KEY00001..KEY00010
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetString(hT, (UNSIGNED8*)"NAME", (UNSIGNED8*)nm, 8) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    // An SDK-built caller passes ADS_STRINGKEY (its value: 2) with the
    // full 8-byte bound. The old constant check read 2 as ADS_DOUBLEKEY
    // and memcpy'd the 8 ASCII bytes as an IEEE-754 double.
    set_scope_raw(hN, ADS_TOP, "KEY00004", 8, SDK_ADS_STRINGKEY);
    set_scope_raw(hN, ADS_BOTTOM, "KEY00006", 8, SDK_ADS_STRINGKEY);
    CHECK(key_count(hN) == 3u);
    REQUIRE(AdsClearScope(hN, ADS_TOP) == 0);
    REQUIRE(AdsClearScope(hN, ADS_BOTTOM) == 0);
    CHECK(key_count(hN) == 10u);
    REQUIRE(AdsCloseTable(hT) == 0);
    AdsDisconnect(h);
}

TEST_CASE("scope keytype: SDK-numbered double scope on a numeric key converts") {
    auto dir = fresh_dir("openads_scope_keytype_num");
    ADSHANDLE h = connect_local(dir);
    ADSHANDLE hT = 0, hN = 0;
    auto name   = cbuf("skn");
    auto fields = cbuf("NUM,N,8,0");
    REQUIRE(AdsCreateTable(h, name.data(), nullptr, ADS_CDX, ADS_ANSI,
                           0, 0, 0, fields.data(), &hT) == 0);
    auto bag  = cbuf((dir / "skn.cdx").string());
    auto tag  = cbuf("BYNUM");
    auto expr = cbuf("NUM");
    REQUIRE(AdsCreateIndex61(hT, bag.data(), tag.data(), expr.data(),
                             nullptr, nullptr, 0, 512, &hN) == 0);
    for (int i = 1; i <= 10; ++i) {
        REQUIRE(AdsAppendRecord(hT) == 0);
        REQUIRE(AdsSetLong(hT, (UNSIGNED8*)"NUM", i) == 0);
        REQUIRE(AdsWriteRecord(hT) == 0);
    }
    // An SDK-built caller passes ADS_DOUBLEKEY (its value: 4) with an
    // 8-byte IEEE-754 bound. The old constant check only converted when
    // it saw this header's 2, so the raw double bytes became the bound.
    double top = 5.0, bottom = 7.0;
    set_scope_raw(hN, ADS_TOP, &top, sizeof(double), SDK_ADS_DOUBLEKEY);
    set_scope_raw(hN, ADS_BOTTOM, &bottom, sizeof(double), SDK_ADS_DOUBLEKEY);
    CHECK(key_count(hN) == 3u);
    REQUIRE(AdsClearScope(hN, ADS_TOP) == 0);
    REQUIRE(AdsClearScope(hN, ADS_BOTTOM) == 0);
    CHECK(key_count(hN) == 10u);
    REQUIRE(AdsCloseTable(hT) == 0);
    AdsDisconnect(h);
}
