#include "doctest.h"
#include "mgmt/error_log.h"
#include "openads/ace.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using openads::mgmt::ErrorLog;

namespace {

// Point the singleton at a fresh directory for one test, restoring the
// suite-wide default (OPENADS_ERROR_LOG_PATH, set in doctest_main) after.
struct LogDirGuard {
    fs::path dir;
    explicit LogDirGuard(const char* name) {
        std::error_code ec;
        dir = fs::temp_directory_path() / name;
        fs::remove_all(dir, ec);
        ErrorLog::instance().set_directory(dir.string());
        ErrorLog::instance().set_max_kbytes(1000);
    }
    ~LogDirGuard() {
        ErrorLog::instance().set_directory("");
        ErrorLog::instance().set_max_kbytes(1000);
    }
};

}  // namespace

TEST_CASE("error log: entries round-trip through ads_err.dbf") {
    LogDirGuard g("openads_errlog_rt");

    ErrorLog::instance().log(7200, "SQL", 0, "select broken from nowhere");
    ErrorLog::instance().log(5035, "NET", 42, "lock denied");
    ErrorLog::instance().log(0,    "SERVER", 0, "informational");

    auto last2 = ErrorLog::instance().read_last(2);
    REQUIRE(last2.size() == 2);
    CHECK(last2[0].code == 5035);
    CHECK(last2[0].source == "NET");
    CHECK(last2[0].src_line == 42);
    CHECK(last2[0].detail == "lock denied");
    CHECK(last2[1].code == 0);
    CHECK(last2[1].source == "SERVER");
    // DateTime is "YYYY-MM-DD HH:MM:SS".
    CHECK(last2[0].datetime.size() == 19);
    CHECK(last2[0].datetime[4] == '-');
    CHECK(last2[0].datetime[10] == ' ');

    auto all = ErrorLog::instance().read_last(100);
    CHECK(all.size() == 3);
    CHECK(all[0].code == 7200);
}

TEST_CASE("error log: size cap drops the oldest third and packs") {
    LogDirGuard g("openads_errlog_cap");
    // 1 KB cap: header (225) + 3 records (267 each) already exceeds it,
    // so the 4th append must trigger the drop-oldest-third rotation.
    ErrorLog::instance().set_max_kbytes(1);
    for (int i = 0; i < 12; ++i) {
        ErrorLog::instance().log(7000 + i, "SQL", i, "entry");
    }
    auto all = ErrorLog::instance().read_last(100);
    REQUIRE(!all.empty());
    CHECK(all.size() < 12);                    // rotation happened
    CHECK(all.back().code == 7011);            // newest survived
    // Entries remain in chronological order after packing.
    for (std::size_t i = 1; i < all.size(); ++i) {
        CHECK(all[i].code > all[i - 1].code);
    }
}

TEST_CASE("error log: ads_err.dbf is a valid DBF the engine can open") {
    LogDirGuard g("openads_errlog_dbf");
    ErrorLog::instance().log(1234, "TEST", 7, "readable via AdsOpenTable");

    const std::string dir = g.dir.string();
    UNSIGNED8 srv[512];
    std::memcpy(srv, dir.c_str(), dir.size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER,
                         nullptr, nullptr, 0, &hConn) == 0);
    UNSIGNED8 leaf[16] = "ads_err";
    ADSHANDLE hTable = 0;
    REQUIRE(AdsOpenTable(hConn, leaf, leaf, ADS_CDX,
                         1, 1, 0, 1, &hTable) == 0);
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hTable, ADS_IGNOREFILTERS, &cnt) == 0);
    CHECK(cnt == 1);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
}
