// Server-side Harbour UDF bridge tests (needs OPENADS_WITH_HARBOUR_UDF
// and the compiled tests/hrb/udf_test.hrb — see tests/CMakeLists.txt).
// Proves: module load + UDF_Init, scalar calls through the real VM,
// error containment (TESTBOOM must not take the server down), and the
// end-to-end index path — tag build, ordered seek, and maintenance on
// later appends all evaluate the HRB function.

#include "doctest.h"
#include "openads/ace.h"
#include "engine/hrb_udf.h"
#include "engine/hrb_harbour.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using openads::engine::hrb_udf::Scalar;

namespace {

std::string upper_of(const std::string& s) {
    std::string o = s;
    for (char& c : o)
        c = static_cast<char>(
            std::toupper(static_cast<unsigned char>(c)));
    return o;
}

bool has_name(const std::vector<std::string>& v, const char* want) {
    const std::string w = want;
    for (const auto& n : v)
        if (upper_of(n) == w) return true;
    return false;
}

Scalar str_arg(const char* s) {
    Scalar a;
    a.kind = Scalar::Kind::String;
    a.s = s;
    return a;
}

Scalar num_arg(double n) {
    Scalar a;
    a.kind = Scalar::Kind::Number;
    a.n = n;
    return a;
}

struct Hrb {
    std::string err;
    Hrb() {
        openads::engine::hrb_udf::set_backend(
            openads::engine::hrb_udf::make_harbour_backend());
        bool ok = openads::engine::hrb_udf::load(
            OPENADS_TEST_HRB_PATH, err);
        REQUIRE_MESSAGE(ok, err);
        REQUIRE(openads::engine::hrb_udf::available());
    }
    ~Hrb() { openads::engine::hrb_udf::unload(); }
};

}  // namespace

TEST_CASE("hrb: load, function list, UDF_Init ran") {
    Hrb h;
    (void)h;
    auto fns = openads::engine::hrb_udf::functions();
    CHECK(has_name(fns, "TESTREV"));
    CHECK(has_name(fns, "TESTADD"));
    CHECK(has_name(fns, "UDF_INIT"));
    CHECK(openads::engine::hrb_udf::has("TESTREV"));
    CHECK(!openads::engine::hrb_udf::has("NO_SUCH_FN"));

    // Lifecycle proof: UDF_Init returns .T. (bridge maps logical to
    // "T", the xBase key convention).
    Scalar out;
    std::string err;
    REQUIRE(openads::engine::hrb_udf::call("UDF_INIT", nullptr, 0,
                                           out, err));
    CHECK(out.kind == Scalar::Kind::String);
    CHECK(out.s == "T");
}

TEST_CASE("hrb: scalar calls, blanks preserved, numbers round-trip") {
    Hrb h;
    (void)h;
    Scalar out;
    std::string err;
    // Byte reverse incl. trailing blanks: "ALPHA     " -> "     AHPLA".
    Scalar a1[1] = {str_arg("ALPHA     ")};
    REQUIRE(openads::engine::hrb_udf::call("TESTREV", a1, 1, out,
                                           err));
    CHECK(out.kind == Scalar::Kind::String);
    CHECK(out.s == "     AHPLA");
    CHECK(out.s.size() == 10);

    Scalar a2[2] = {num_arg(2.0), num_arg(3.0)};
    REQUIRE(openads::engine::hrb_udf::call("TESTADD", a2, 2, out,
                                           err));
    CHECK(out.kind == Scalar::Kind::Number);
    CHECK(out.n == doctest::Approx(5.0));

    Scalar a3[1] = {str_arg("AB  ")};
    REQUIRE(openads::engine::hrb_udf::call("TESTECHO", a3, 1, out,
                                           err));
    CHECK(out.s == "AB  ");

    // Date result arrives as YYYYMMDD text (bridge formats it).
    REQUIRE(openads::engine::hrb_udf::call("TESTDTOR", nullptr, 0,
                                           out, err));
    CHECK(out.kind == Scalar::Kind::String);
    CHECK(out.s.size() == 8);
    bool all_digits = out.s.size() == 8;
    for (char c : out.s) all_digits = all_digits && c >= '0' && c <= '9';
    CHECK(all_digits);

    // Unknown function: clean failure, exact message not pinned.
    Scalar none[1] = {str_arg("x")};
    CHECK(!openads::engine::hrb_udf::call("NO_SUCH_FN", none, 1, out,
                                          err));
}

TEST_CASE("hrb: UDF runtime error is contained, VM stays usable") {
    Hrb h;
    (void)h;
    Scalar out;
    std::string err;
    CHECK(!openads::engine::hrb_udf::call("TESTBOOM", nullptr, 0, out,
                                          err));
    CHECK(!err.empty());
    // The VM must still serve afterwards.
    Scalar a1[1] = {str_arg("AB")};
    REQUIRE(openads::engine::hrb_udf::call("TESTREV", a1, 1, out,
                                           err));
    CHECK(out.s == "BA");
}

namespace {

// End-to-end: a TESTREV(NAME) tag built, seeked and maintained
// through the public ACE surface (local engine).
struct TagRepro {
    fs::path  dir;
    ADSHANDLE hConn = 0;
    ADSHANDLE hT    = 0;

    TagRepro() {
        dir = fs::temp_directory_path() / "openads_hrb_tag";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);

        UNSIGNED8 srv[256];
        std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
        REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &hConn) == 0);

        UNSIGNED8 def[] = "NAME,C,10,0";
        UNSIGNED8 tname[] = "hrbtag";
        REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                               0, 0, 0, 0, def, &hT) == 0);

        UNSIGNED8 fNAME[] = "NAME";
        auto append = [&](const char* name) {
            REQUIRE(AdsAppendRecord(hT) == 0);
            REQUIRE(AdsSetString(hT, fNAME,
                reinterpret_cast<UNSIGNED8*>(const_cast<char*>(name)),
                static_cast<UNSIGNED16>(std::strlen(name))) == 0);
            REQUIRE(AdsWriteRecord(hT) == 0);
        };
        append("ALPHA");
        append("BRAVO");
        append("CHARLIE");
    }

    ~TagRepro() {
        if (hT) AdsCloseTable(hT);
        if (hConn) AdsDisconnect(hConn);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

}  // namespace

TEST_CASE("hrb: TESTREV tag builds, seeks, and maintains on append") {
    Hrb h;
    (void)h;
    TagRepro r;

    // Build the tag through the HRB function (no native REVERSE
    // involved — TESTREV exists only in the module).
    UNSIGNED8 bag[] = "hrbtag.cdx", tag[] = "BYREV",
              exp[] = "TESTREV(NAME)";
    ADSHANDLE hI = 0;
    REQUIRE(AdsCreateIndex61(r.hT, bag, tag, exp,
                             nullptr, nullptr, 0, 0, &hI) == 0);

    // Reversed keys: "ALPHA     "->"     AHPLA", "BRAVO     "->"     OVARB",
    // "CHARLIE   "->"   EILRAHC". Seek CHARLIE by its reversed key.
    const char* key = "   EILRAHC";
    UNSIGNED16 found = 99;
    REQUIRE(AdsSeek(hI, reinterpret_cast<UNSIGNED8*>(
                        const_cast<char*>(key)),
                    10, ADS_STRINGKEY, ADS_HARDSEEK, &found) == 0);
    CHECK(found == 1);
    if (found == 1) {
        UNSIGNED32 recno = 0;
        REQUIRE(AdsGetRecordNum(r.hT, 0, &recno) == 0);
        CHECK(recno == 3u);
    }

    // Append AFTER the tag exists: maintenance must evaluate the
    // HRB function too, or the new row is unseekable.
    UNSIGNED8 fNAME[] = "NAME";
    REQUIRE(AdsAppendRecord(r.hT) == 0);
    REQUIRE(AdsSetString(r.hT, fNAME,
        reinterpret_cast<UNSIGNED8*>(const_cast<char*>("DELTA")),
        5) == 0);
    REQUIRE(AdsWriteRecord(r.hT) == 0);

    const char* key2 = "     ATLED";  // reverse("DELTA     ")
    found = 99;
    REQUIRE(AdsSeek(hI, reinterpret_cast<UNSIGNED8*>(
                        const_cast<char*>(key2)),
                    10, ADS_STRINGKEY, ADS_HARDSEEK, &found) == 0);
    CHECK(found == 1);
    if (found == 1) {
        UNSIGNED32 recno = 0;
        REQUIRE(AdsGetRecordNum(r.hT, 0, &recno) == 0);
        CHECK(recno == 4u);
    }
}
