// Visual FoxPro compatibility of the CDX files OpenADS writes. Each case
// pins a defect VFP 9 reported against a production data set (Nov 2026):
//
//   1. A tag with a FOR clause must carry FORFILTER (0x08) in its indexOpt
//      byte (VFP writes 0x68; plain tags 0x60). Without it VFP ignored the
//      stored FOR, treated the tag as unconditional and reported every row
//      the FOR excluded as missing from the index.
//   2. indexOpt must keep COMPOUND (0x40) on every path. set_options and
//      the create paths wrote a bare 0x20, and VFP then saw one tag per bag.
//   3. A tag over an empty table must be rooted at an empty ROOT|LEAF page,
//      never at offset 0 -- VFP failed the whole bag with error 114
//      ("Index does not match the table").
//   4. Key expressions using LEFT / RIGHT / SPACE / LEN / IIF evaluated to
//      "" (unknown function), and composite tags were sized from record 1.
//      VFP sizes a tag from the expression's static maximum, so
//      TYPE+RIGHT(TRIM(CCN),4) is 7 wide even when record 1's CCN is blank.
#include "doctest.h"
#include "openads/ace.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

UNSIGNED8* u8(const char* s) {
    return reinterpret_cast<UNSIGNED8*>(const_cast<char*>(s));
}

fs::path fresh_dir(const char* tag) {
    auto p = fs::temp_directory_path() / (std::string("openads_vfpcdx_") + tag);
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p);
    return p;
}

ADSHANDLE connect_local(const fs::path& dir) {
    ADSHANDLE h = 0;
    const std::string s = dir.string();
    REQUIRE(AdsConnect60(u8(s.c_str()), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &h) == 0);
    return h;
}

ADSHANDLE create_table(ADSHANDLE conn, const fs::path& file) {
    ADSHANDLE t = 0;
    const std::string s = file.string();
    REQUIRE(AdsCreateTable(conn, u8(s.c_str()), nullptr, ADS_CDX, ADS_ANSI,
                           ADS_COMPATIBLE_LOCKING, ADS_IGNORERIGHTS, 0,
                           u8("TYPE,C,3,0;CCN,C,20,0;REF,C,10,0;PREV,C,1,0;"
                              "CODE,C,8,0;SLCT,L,1,0;VEND,C,6,0;CHKN,N,6,0;"
                              "IDATE,D,8,0"),
                           &t) == 0);
    return t;
}

void set_str(ADSHANDLE t, const char* fld, const char* v) {
    REQUIRE(AdsSetString(t, u8(fld), u8(v), static_cast<UNSIGNED32>(std::strlen(v))) == 0);
}

// Row 1 is deliberately blank in CCN / CODE / SLCT so a record-1 probe
// would size the variable-width tags too short.
void add_rows(ADSHANDLE t) {
    REQUIRE(AdsAppendRecord(t) == 0);
    set_str(t, "TYPE", "CHK");
    set_str(t, "VEND", "ACME");
    REQUIRE(AdsSetLogical(t, u8("SLCT"), 0) == 0);
    REQUIRE(AdsWriteRecord(t) == 0);

    REQUIRE(AdsAppendRecord(t) == 0);
    set_str(t, "TYPE", "CCA");
    set_str(t, "CCN", "4111111111111234");
    set_str(t, "REF", "REF01");
    set_str(t, "PREV", "R");
    set_str(t, "CODE", "NET 85");
    REQUIRE(AdsSetLogical(t, u8("SLCT"), 1) == 0);
    set_str(t, "VEND", "KLEIN");
    REQUIRE(AdsSetDouble(t, u8("CHKN"), 125) == 0);
    REQUIRE(AdsSetDate(t, u8("IDATE"), u8("20080919"), 8) == 0);
    REQUIRE(AdsWriteRecord(t) == 0);
}

ADSHANDLE make_tag(ADSHANDLE t, const fs::path& cdx, const char* tag,
                   const char* expr, const char* cond = "") {
    ADSHANDLE ix = 0;
    const std::string s = cdx.string();
    REQUIRE(AdsCreateIndex61(t, u8(s.c_str()), u8(tag), u8(expr), u8(cond),
                             u8(""), ADS_COMPOUND, 512, &ix) == 0);
    return ix;
}

std::string key_of(ADSHANDLE t, ADSHANDLE ix, UNSIGNED32 recno) {
    REQUIRE(AdsGotoRecord(t, recno) == 0);
    UNSIGNED8 buf[256] = {};
    UNSIGNED16 n = sizeof(buf) - 1;
    REQUIRE(AdsExtractKey(ix, buf, &n) == 0);
    return std::string(reinterpret_cast<char*>(buf), n);
}

UNSIGNED16 key_len(ADSHANDLE ix) {
    UNSIGNED16 n = 0;
    REQUIRE(AdsGetKeyLength(ix, &n) == 0);
    return n;
}

std::vector<std::uint8_t> slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

std::uint16_t le16(const std::vector<std::uint8_t>& b, std::size_t o) {
    return static_cast<std::uint16_t>(b[o] | (b[o + 1] << 8));
}
std::uint32_t le32(const std::vector<std::uint8_t>& b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) | (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) | (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

// Offset of the sub-tag header whose key expression is `expr`: tag headers
// are page aligned and keep the key expression at pool offset keyExpPos
// (header +0x1FC) with its length incl. NUL at +0x1FE.
std::size_t find_tag_header(const std::vector<std::uint8_t>& b, const std::string& expr) {
    for (std::size_t off = 0x600; off + 1024 <= b.size(); off += 512) {
        const std::uint16_t kpos = le16(b, off + 0x1FC);
        const std::uint16_t klen = le16(b, off + 0x1FE);
        if (klen != expr.size() + 1 || off + 512 + kpos + klen > b.size()) continue;
        if (std::memcmp(b.data() + off + 512 + kpos, expr.data(), expr.size()) == 0)
            return off;
    }
    return 0;
}

} // namespace

TEST_CASE("CDX for VFP: indexOpt is 0x60, plus FORFILTER 0x08 for a FOR tag") {
    auto dir = fresh_dir("opt");
    ADSHANDLE conn = connect_local(dir);
    ADSHANDLE t = create_table(conn, dir / "opt.dbf");
    add_rows(t);
    make_tag(t, dir / "opt.cdx", "PLAIN", "TYPE");
    make_tag(t, dir / "opt.cdx", "FORTAG", "VEND", "!DELETED()");
    REQUIRE(AdsCloseTable(t) == 0);
    REQUIRE(AdsDisconnect(conn) == 0);

    const auto b = slurp(dir / "opt.cdx");
    const std::size_t plain = find_tag_header(b, "TYPE");
    const std::size_t fortag = find_tag_header(b, "VEND");
    REQUIRE(plain != 0);
    REQUIRE(fortag != 0);
    CHECK(b[plain + 14] == 0x60);
    CHECK(b[fortag + 14] == 0x68);
    CHECK(b[14] == 0xE0);   // structure tag
}

TEST_CASE("CDX for VFP: tags of an empty table are rooted at an empty leaf") {
    auto dir = fresh_dir("empty");
    ADSHANDLE conn = connect_local(dir);
    ADSHANDLE t = create_table(conn, dir / "e.dbf");
    ADSHANDLE ix1 = make_tag(t, dir / "e.cdx", "T1", "TYPE");
    make_tag(t, dir / "e.cdx", "T2", "VEND+TYPE", "!DELETED()");
    REQUIRE(AdsCloseTable(t) == 0);

    const auto b = slurp(dir / "e.cdx");
    for (const char* expr : {"TYPE", "VEND+TYPE"}) {
        const std::size_t h = find_tag_header(b, expr);
        REQUIRE(h != 0);
        const std::uint32_t root = le32(b, h);
        CAPTURE(expr);
        REQUIRE(root != 0);
        REQUIRE(root + 512 <= b.size());
        CHECK(le16(b, root) == 3);       // ROOT | LEAF
        CHECK(le16(b, root + 2) == 0);   // no keys
    }
    (void)ix1;

    // The empty root is a working tree: reopen, append, read back in order.
    ADSHANDLE t2 = 0;
    const std::string tbl = (dir / "e.dbf").string();
    REQUIRE(AdsOpenTable(conn, u8(tbl.c_str()), nullptr, ADS_CDX, ADS_ANSI,
                         ADS_COMPATIBLE_LOCKING, ADS_IGNORERIGHTS, ADS_DEFAULT, &t2) == 0);
    add_rows(t2);
    ADSHANDLE ix = 0;
    REQUIRE(AdsGetIndexHandle(t2, u8("T1"), &ix) == 0);
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetKeyCount(ix, ADS_IGNOREFILTERS, &cnt) == 0);
    CHECK(cnt == 2);
    REQUIRE(AdsGotoTop(ix) == 0);
    UNSIGNED32 rec = 0;
    REQUIRE(AdsGetRecordNum(t2, ADS_IGNOREFILTERS, &rec) == 0);
    CHECK(rec == 2);   // "CCA" sorts before "CHK"
    REQUIRE(AdsCloseTable(t2) == 0);
    REQUIRE(AdsDisconnect(conn) == 0);
}

TEST_CASE("CDX FOR clause: parenthesised arithmetic inside a function") {
    // VTRANS-style "open item" tag: rows whose balance formats to 0.00
    // must be excluded. STR((...)) used to parse as STR("") and every
    // row passed the FOR.
    auto dir = fresh_dir("forparen");
    ADSHANDLE conn = connect_local(dir);
    ADSHANDLE t = create_table(conn, dir / "f.dbf");
    add_rows(t);   // CHKN: row 1 = 0, row 2 = 125
    ADSHANDLE ix = make_tag(t, dir / "f.cdx", "OPEN", "TYPE",
        "LTRIM(STR((CHKN-125),15,2))#\"0.00\".AND..NOT.DELETED()");
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetKeyCount(ix, ADS_IGNOREFILTERS, &cnt) == 0);
    CHECK(cnt == 1);
    // xBase blank-vs-space: row 1's CCN is blank, so CCN#" " is false
    // there; only row 2 qualifies. (ITEMS.CHG_MARKED: FOR MARKED#" ".)
    ADSHANDLE ix2 = make_tag(t, dir / "f.cdx", "MARKED", "TYPE",
                             "CCN#\" \".AND..NOT.DELETED()");
    REQUIRE(AdsGetKeyCount(ix2, ADS_IGNOREFILTERS, &cnt) == 0);
    CHECK(cnt == 1);
    REQUIRE(AdsCloseTable(t) == 0);
    REQUIRE(AdsDisconnect(conn) == 0);
}

TEST_CASE("CDX for VFP: LEFT/RIGHT/SPACE/LEN/IIF keys, sized like VFP") {
    struct Case { const char* expr; UNSIGNED16 width; const char* key_rec2; };
    const Case cases[] = {
        {"TYPE+right(Trim(CCN),4)",                    7,  "CCA1234"},
        {"TYPE+padL( Trim(REF) , len( REF) )",         13, "CCA     REF01"},
        {"PREV+LEFT( LTrim( CODE ) +SPACE(10) ,10)",   11, "RNET 85    "},
        {"IIF(SLCT,\"0\",\"1\")+VEND+DTOS(IDATE)",     15, "0KLEIN 20080919"},
        {"IIF(LTRIM(STR(CHKN,15,2))#\"0.00\",\"1\",\"2\")+TYPE", 4, "1CCA"},
        {"IIF(.NOT.EMPTY(CCN),\"A\",\"B\")+REPLICATE(\"*\",2)",  3, "A**"},
        // UPPER(field) fast path must not swallow the rest of the expression.
        {"UPPER(VEND)+TYPE",                           9,  "KLEIN CCA"},
    };
    for (bool empty_first : {false, true}) {
        auto dir = fresh_dir(empty_first ? "w_empty" : "w_rows");
        ADSHANDLE conn = connect_local(dir);
        ADSHANDLE t = create_table(conn, dir / "w.dbf");
        if (!empty_first) add_rows(t);
        int n = 0;
        std::vector<ADSHANDLE> tags;
        for (const auto& c : cases) {
            const std::string name = "W" + std::to_string(n++);
            tags.push_back(make_tag(t, dir / "w.cdx", name.c_str(), c.expr));
        }
        if (empty_first) add_rows(t);   // maintained through the open tags
        for (std::size_t i = 0; i < tags.size(); ++i) {
            CAPTURE(cases[i].expr);
            CAPTURE(empty_first);
            CHECK(key_len(tags[i]) == cases[i].width);
            CHECK(key_of(t, tags[i], 2) == cases[i].key_rec2);
        }
        REQUIRE(AdsCloseTable(t) == 0);
        REQUIRE(AdsDisconnect(conn) == 0);
    }
}
