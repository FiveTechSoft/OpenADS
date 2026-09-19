// Server-side backup archiving (OAds_Zip/OAds_UnZip) through the ACE
// surface, local and over the wire: dated backup/ placement, explicit
// subdir archives with verbatim application-chosen filenames, stats,
// jail rejection, overwrite rules, flush-and-go on open tables, and
// fail-if-open on unzip targets.

#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

struct LocalDb {
    fs::path    dir;
    ADSHANDLE   hConn = 0;

    explicit LocalDb(const char* leaf) {
        dir = fs::temp_directory_path() / leaf;
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);

        std::string srv = dir.string();
        std::vector<UNSIGNED8> sb(srv.begin(), srv.end());
        sb.push_back(0);
        REQUIRE(AdsConnect60(sb.data(), ADS_LOCAL_SERVER, nullptr,
                             nullptr, 0, &hConn) == 0);
    }

    void make_table(const char* name, int rows) {
        UNSIGNED8 def[] = "NAME,C,10,0";
        std::string tn = name;
        std::vector<UNSIGNED8> tb(tn.begin(), tn.end());
        tb.push_back(0);
        ADSHANDLE hT = 0;
        REQUIRE(AdsCreateTable(hConn, tb.data(), nullptr, ADS_CDX,
                               0, 0, 0, 0, def, &hT) == 0);
        UNSIGNED8 f[] = "NAME";
        for (int i = 0; i < rows; ++i) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "r%02d", i);
            REQUIRE(AdsAppendRecord(hT) == 0);
            REQUIRE(AdsSetString(hT, f,
                                 reinterpret_cast<UNSIGNED8*>(buf),
                                 static_cast<UNSIGNED16>(
                                     std::strlen(buf))) == 0);
            REQUIRE(AdsWriteRecord(hT) == 0);
        }
        REQUIRE(AdsCloseTable(hT) == 0);
    }

    std::string read_file(const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    }

    ~LocalDb() {
        if (hConn) AdsDisconnect(hConn);
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

struct ZipOut {
    UNSIGNED32 files = 0;
    UNSIGNED64 bytes = 0, arc_bytes = 0;
    std::string archive;
    UNSIGNED32 rc = 0;
};

ZipOut do_zip(ADSHANDLE hConn, const char* dir, const char* files,
              const char* zipname, int level = 6, bool ow = false,
              const char* pwd = "", const char* excl = "",
              bool with_path = false) {
    ZipOut o;
    char arc[512];
    UNSIGNED16 arc_len = sizeof(arc);
    std::string fl = files ? files : "";
    std::string ex = excl ? excl : "";
    std::string pw = pwd ? pwd : "";
    // 0x1F-joined lists from the Harrbour side arrive pre-joined in
    // these tests via explicit separators.
    o.rc = AdsZipFiles(
        hConn, (UNSIGNED8*)dir, (UNSIGNED8*)fl.data(),
        (UNSIGNED8*)zipname, (UNSIGNED16)level, ow ? 1 : 0,
        (UNSIGNED8*)pw.data(), (UNSIGNED8*)ex.data(),
        with_path ? 1 : 0, (UNSIGNED8*)arc, &arc_len, &o.files,
        &o.bytes, &o.arc_bytes);
    if (o.rc == 0) o.archive.assign(arc, arc_len);
    return o;
}

}  // namespace

TEST_CASE("zip: local dated backup, stats, restore roundtrip") {
    LocalDb db("openads_zip_abi");
    db.make_table("acc.dbf", 3);
    {
        std::ofstream(db.dir / "note.txt") << "hello";
    }

    std::string files = std::string("acc.dbf") + '\x1F' + "note.txt";
    ZipOut z = do_zip(db.hConn, ".", files.c_str(), "ACC");
    REQUIRE_MESSAGE(z.rc == 0, z.rc);
    CHECK(z.files == 2u);
    CHECK(z.bytes > 0u);
    CHECK(z.arc_bytes > 0u);
    // Dated repository naming under backup/.
    CHECK(z.archive.rfind("backup/ACC_", 0) == 0);
    CHECK(z.archive.size() > 18);
    CHECK(z.archive.substr(z.archive.size() - 4) == ".zip");
    std::string stamp = z.archive.substr(z.archive.size() - 12, 8);
    bool digits = stamp.size() == 8;
    for (char c : stamp) digits = digits && c >= '0' && c <= '9';
    CHECK(digits);
    CHECK(fs::is_regular_file(db.dir / z.archive));

    // Restore elsewhere and byte-compare.
    fs::create_directories(db.dir / "restored");
    UNSIGNED32 n = 0;
    UNSIGNED64 nb = 0, ab = 0;
    REQUIRE(AdsUnzipFiles(db.hConn, (UNSIGNED8*)"restored",
                          (UNSIGNED8*)z.archive.c_str(),
                          (UNSIGNED8*)"", 0, 0, &n, &nb, &ab) == 0);
    CHECK(n == 2u);
    CHECK(db.read_file(db.dir / "restored" / "acc.dbf") ==
          db.read_file(db.dir / "acc.dbf"));
    CHECK(db.read_file(db.dir / "restored" / "note.txt") == "hello");
}

TEST_CASE("zip: explicit subdir takes the filename verbatim") {
    LocalDb db("openads_zip_subdir");
    db.make_table("s.dbf", 2);
    {
        std::ofstream(db.dir / "s.txt") << "hi";
    }

    std::string files = std::string("s.dbf") + '\x1F' + "s.txt";
    // No date stamp, no forced extension: exactly what was asked.
    ZipOut z = do_zip(db.hConn, ".", files.c_str(), "myback/nightly");
    REQUIRE_MESSAGE(z.rc == 0, z.rc);
    CHECK(z.files == 2u);
    CHECK(z.archive == "myback/nightly");
    CHECK(fs::is_regular_file(db.dir / "myback" / "nightly"));
    // The explicit form never touches the legacy backup/ repository.
    CHECK(!fs::exists(db.dir / "backup"));

    // Nested subdirs are created on demand.
    ZipOut zn = do_zip(db.hConn, ".", "s.txt", "deep/n1/n2arc.zip");
    REQUIRE_MESSAGE(zn.rc == 0, zn.rc);
    CHECK(zn.archive == "deep/n1/n2arc.zip");
    CHECK(fs::is_regular_file(db.dir / zn.archive));

    // Fully-qualified client spellings fold under the owning root
    // (same rule as source dirs; host-independent).
    ZipOut za = do_zip(db.hConn, ".", "s.txt", "C:/absbkp/nightly2");
    REQUIRE_MESSAGE(za.rc == 0, za.rc);
    CHECK(za.archive == "absbkp/nightly2");
    CHECK(fs::is_regular_file(db.dir / za.archive));

    // Re-zip without overwrite refuses; with overwrite succeeds.
    ZipOut z2 = do_zip(db.hConn, ".", files.c_str(), "myback/nightly");
    CHECK(z2.rc != 0);
    ZipOut z3 =
        do_zip(db.hConn, ".", files.c_str(), "myback/nightly", 6, true);
    CHECK(z3.rc == 0);
    CHECK(z3.archive == "myback/nightly");

    // Roundtrip via the reported spelling.
    fs::create_directories(db.dir / "out");
    UNSIGNED32 n = 0;
    UNSIGNED64 nb = 0, ab = 0;
    REQUIRE(AdsUnzipFiles(db.hConn, (UNSIGNED8*)"out",
                          (UNSIGNED8*)z.archive.c_str(),
                          (UNSIGNED8*)"", 1, 0, &n, &nb, &ab) == 0);
    CHECK(n == 2u);
    CHECK(db.read_file(db.dir / "out" / "s.txt") == "hi");

    // Traversal in the archive spelling is loud, not jailed-silent.
    ZipOut bad = do_zip(db.hConn, ".", "s.dbf", "../evil");
    CHECK(bad.rc != 0);
    ZipOut bad2 = do_zip(db.hConn, ".", "s.dbf", "myback/");
    CHECK(bad2.rc != 0);
}

TEST_CASE("zip: jail, overwrite and level rules are loud") {
    LocalDb db("openads_zip_rules");
    db.make_table("a.dbf", 1);

    // Escape attempts rejected (no silent remap).
    ZipOut e1 = do_zip(db.hConn, "../..", "a.dbf", "X");
    CHECK(e1.rc != 0);
    ZipOut e2 = do_zip(db.hConn, ".", "C:/win/x.dbf", "X");
    CHECK(e2.rc != 0);

    // Archive exists + overwrite off -> loud refusal.
    ZipOut z1 = do_zip(db.hConn, ".", "a.dbf", "ONE");
    REQUIRE(z1.rc == 0);
    ZipOut z2 = do_zip(db.hConn, ".", "a.dbf", "ONE");
    CHECK(z2.rc != 0);
    // Overwrite on -> fine.
    ZipOut z3 = do_zip(db.hConn, ".", "a.dbf", "ONE", 6, true);
    CHECK(z3.rc == 0);
    CHECK(z3.archive == z1.archive);

    // Bad level rejected before touching anything.
    ZipOut z4 = do_zip(db.hConn, ".", "a.dbf", "LVL", 99);
    CHECK(z4.rc != 0);

    // Missing source names the failure.
    std::string ghost = std::string("ghost.dbf");
    ZipOut z5 = do_zip(db.hConn, ".", ghost.c_str(), "GHOST");
    CHECK(z5.rc != 0);

    // Unzip of nothing fails loud.
    UNSIGNED32 n = 0;
    UNSIGNED64 nb = 0, ab = 0;
    CHECK(AdsUnzipFiles(db.hConn, (UNSIGNED8*)".",
                        (UNSIGNED8*)"backup/nope_20000101.zip",
                        (UNSIGNED8*)"", 0, 0, &n, &nb, &ab) != 0);
}

TEST_CASE("zip: flush-and-go archives uncommitted-closed writes") {
    LocalDb db("openads_zip_flush");
    db.make_table("f.dbf", 2);

    // Open, append, write — then zip WITHOUT closing. The archive
    // must still capture the new row (server flushes first).
    std::string tn = "f.dbf";
    std::vector<UNSIGNED8> tb(tn.begin(), tn.end());
    tb.push_back(0);
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(db.hConn, tb.data(), nullptr, ADS_CDX, 0, 0,
                         0, 0, &hT) == 0);
    UNSIGNED8 f[] = "NAME";
    REQUIRE(AdsAppendRecord(hT) == 0);
    REQUIRE(AdsSetString(hT, f, (UNSIGNED8*)"newrow", 6) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);

    ZipOut z = do_zip(db.hConn, ".", "f.dbf", "FL");
    REQUIRE_MESSAGE(z.rc == 0, z.rc);
    REQUIRE(AdsCloseTable(hT) == 0);

    fs::create_directories(db.dir / "r2");
    UNSIGNED32 n = 0;
    UNSIGNED64 nb = 0, ab = 0;
    REQUIRE(AdsUnzipFiles(db.hConn, (UNSIGNED8*)"r2",
                          (UNSIGNED8*)z.archive.c_str(),
                          (UNSIGNED8*)"", 0, 0, &n, &nb, &ab) == 0);
    // Reopen the restored copy: all three rows present.
    std::string rt = (db.dir / "r2" / "f.dbf").string();
    std::vector<UNSIGNED8> rb(rt.begin(), rt.end());
    rb.push_back(0);
    ADSHANDLE hT2 = 0;
    REQUIRE(AdsOpenTable(db.hConn, rb.data(), nullptr, ADS_CDX, 0, 0,
                         0, 0, &hT2) == 0);
    UNSIGNED32 cnt = 0;
    REQUIRE(AdsGetRecordCount(hT2, 0, &cnt) == 0);
    CHECK(cnt == 3u);
    REQUIRE(AdsCloseTable(hT2) == 0);
}

TEST_CASE("zip: unzip refuses targets open on the connection") {
    LocalDb db("openads_zip_open");
    db.make_table("o.dbf", 1);

    ZipOut z = do_zip(db.hConn, ".", "o.dbf", "OP");
    REQUIRE(z.rc == 0);

    // Hold the destination table open, then unzip over its own dir.
    std::string tn = "o.dbf";
    std::vector<UNSIGNED8> tb(tn.begin(), tn.end());
    tb.push_back(0);
    ADSHANDLE hT = 0;
    REQUIRE(AdsOpenTable(db.hConn, tb.data(), nullptr, ADS_CDX, 0, 0,
                         0, 0, &hT) == 0);
    UNSIGNED32 n = 0;
    UNSIGNED64 nb = 0, ab = 0;
    UNSIGNED32 rc = AdsUnzipFiles(db.hConn, (UNSIGNED8*)".",
                                  (UNSIGNED8*)z.archive.c_str(),
                                  (UNSIGNED8*)"", 1, 0, &n, &nb, &ab);
    CHECK(rc == 7040);
    REQUIRE(AdsCloseTable(hT) == 0);
    // Closed: same call now succeeds.
    REQUIRE(AdsUnzipFiles(db.hConn, (UNSIGNED8*)".",
                          (UNSIGNED8*)z.archive.c_str(),
                          (UNSIGNED8*)"", 1, 0, &n, &nb, &ab) == 0);
    CHECK(n == 1u);
}

TEST_CASE("zip: remote roundtrip over the wire") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "openads_zip_wire";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    openads::network::Server srv;
    REQUIRE(srv.start("127.0.0.1", 0).has_value());

    char uri[512];
    std::snprintf(uri, sizeof(uri), "tcp://127.0.0.1:%u/%s",
                  static_cast<unsigned>(srv.port()),
                  dir.string().c_str());
    std::vector<UNSIGNED8> ub(uri, uri + std::strlen(uri) + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(ub.data(), ADS_REMOTE_SERVER, nullptr,
                         nullptr, 0, &hConn) == 0);

    UNSIGNED8 def[] = "NAME,C,10,0";
    UNSIGNED8 tname[] = "w.dbf";
    ADSHANDLE hT = 0;
    REQUIRE(AdsCreateTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0,
                           def, &hT) == 0);
    UNSIGNED8 f[] = "NAME";
    REQUIRE(AdsAppendRecord(hT) == 0);
    REQUIRE(AdsSetString(hT, f, (UNSIGNED8*)"wire", 4) == 0);
    REQUIRE(AdsWriteRecord(hT) == 0);
    REQUIRE(AdsCloseTable(hT) == 0);

    ZipOut z = do_zip(hConn, ".", "w.dbf", "WIRE");
    REQUIRE_MESSAGE(z.rc == 0, z.rc);
    CHECK(z.files == 1u);
    CHECK(z.archive.rfind("backup/WIRE_", 0) == 0);

    fs::create_directories(dir / "back");
    UNSIGNED32 n = 0;
    UNSIGNED64 nb = 0, ab = 0;
    REQUIRE(AdsUnzipFiles(hConn, (UNSIGNED8*)"back",
                          (UNSIGNED8*)z.archive.c_str(),
                          (UNSIGNED8*)"", 0, 0, &n, &nb, &ab) == 0);
    CHECK(n == 1u);
    CHECK(fs::is_regular_file(dir / "back" / "w.dbf"));

    REQUIRE(AdsDisconnect(hConn) == 0);
    srv.stop();
    fs::remove_all(dir, ec);
}
