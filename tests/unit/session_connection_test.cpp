#include "doctest.h"
#include "openads/error.h"
#include "session/connection.h"
#include "util/log.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using openads::engine::TableType;
using openads::session::Connection;

namespace {

fs::path tmp_dir(const char* tag) {
    auto p = fs::temp_directory_path() / (std::string("openads_m1_conn_") + tag);
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p);
    return p;
}

void write_minimal_dbf(const fs::path& p) {
    std::vector<std::uint8_t> file;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    hdr[4] = 1;
    hdr[8] = 32 + 32 + 1; hdr[9] = 0;
    hdr[10] = 1 + 3; hdr[11] = 0;
    file.insert(file.end(), hdr.begin(), hdr.end());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "TAG", 11);
    fd[11] = 'C';
    fd[16] = 3;
    file.insert(file.end(), fd.begin(), fd.end());
    file.push_back(0x0D);
    file.push_back(' '); file.push_back('a'); file.push_back('b'); file.push_back('c');
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
}

} // namespace

TEST_CASE("Connection opens against a directory") {
    auto dir = tmp_dir("open");
    {
        auto opened = Connection::open(dir.string());
        REQUIRE(opened.has_value());
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("Connection opens a CDX-typed table by relative name") {
    auto dir = tmp_dir("table");
    write_minimal_dbf(dir / "data.dbf");
    {
        auto opened = Connection::open(dir.string());
        REQUIRE(opened.has_value());
        Connection c = std::move(opened).value();

        auto th = c.open_table("data.dbf", TableType::Cdx);
        REQUIRE(th.has_value());
        auto* table = c.lookup_table(th.value());
        REQUIRE(table != nullptr);
        CHECK(table->field_count() == 1);
        CHECK(table->record_count() == 1);

        c.close_table(th.value());
        CHECK(c.lookup_table(th.value()) == nullptr);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// openads_serverd --legacy-paths: a legacy ERP opens tables by absolute
// local path (USE "E:\CLIENT\FILE.DBF"). With legacy mode on, the
// connection strips the drive letter and matches its own data directory
// case-insensitively, so the same USE line resolves to the real file.
TEST_CASE("Connection legacy_paths resolves a foreign-drive absolute path") {
    auto dir = tmp_dir("legacy");
    fs::create_directories(dir / "Sub");
    write_minimal_dbf(dir / "Sub" / "data.dbf");
    {
        auto opened = Connection::open(dir.string());
        REQUIRE(opened.has_value());
        Connection c = std::move(opened).value();
        c.set_legacy_paths(true);

        // Spell the very same file through a drive letter that does not
        // exist on this machine — the verbatim-open probe must miss and
        // the legacy prefix-strip (drive-insensitive) must land it.
        const std::string abs = (dir / "Sub" / "data.dbf").generic_string();
        std::string foreign = "E:";
        foreign += (abs.size() >= 2 && abs[1] == ':') ? abs.substr(2) : abs;
        const std::string want =
            fs::weakly_canonical(dir / "Sub" / "data.dbf").generic_string();

        auto type = TableType::Cdx;
        CHECK(fs::path(c.resolve_table_file(foreign, type))
                  .generic_string() == want);

        // Same path with legacy mode OFF folds the whole remainder under
        // the data directory and does NOT find the file.
        auto opened2 = Connection::open(dir.string());
        REQUIRE(opened2.has_value());
        Connection c2 = std::move(opened2).value();
        auto type2 = TableType::Cdx;
        const std::string off = c2.resolve_table_file(foreign, type2);
        CHECK(off != want);
        CHECK_FALSE(fs::exists(off));

        // End to end: the foreign path opens the table.
        auto th = c.open_table(foreign, TableType::Cdx);
        REQUIRE(th.has_value());
        auto* table = c.lookup_table(th.value());
        REQUIRE(table != nullptr);
        CHECK(table->record_count() == 1);
        c.close_table(th.value());
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("Connection legacy_paths folds an unknown absolute path under root") {
    auto dir = tmp_dir("legacyfold");
    fs::create_directories(dir / "CLIENTFOLD");
    write_minimal_dbf(dir / "CLIENTFOLD" / "fold.dbf");
    {
        auto opened = Connection::open(dir.string());
        REQUIRE(opened.has_value());
        Connection c = std::move(opened).value();
        c.set_legacy_paths(true);

        // No root prefix in common: drive dropped, remainder joined —
        // this is what serves several ERP client folders from one root.
        auto type = TableType::Cdx;
        const std::string want =
            fs::weakly_canonical(dir / "CLIENTFOLD" / "fold.dbf")
                .generic_string();
        CHECK(fs::path(c.resolve_table_file("E:\\CLIENTFOLD\\fold.dbf", type))
                  .generic_string() == want);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

// Regression (Pritpal Bedi, Aug 2026): on a Windows server with
// --legacy-paths, an absolute client path that happens to exist on the
// host filesystem OUTSIDE --data must still remount under the data root.
// Strict mode keeps the SAP free-table OPEN-exists exception; legacy
// mode must not.
TEST_CASE("Connection legacy_paths ignores host-absolute file that exists outside data root") {
    auto base = tmp_dir("legacy_host_shadow");
    auto jail = base / "Temp";
    auto shadow = base / "Creative.RAM";
    fs::create_directories(jail / "Creative.RAM");
    fs::create_directories(shadow);
    write_minimal_dbf(jail / "Creative.RAM" / "t.dbf");
    write_minimal_dbf(shadow / "t.dbf");
    {
        auto opened = Connection::open(jail.string());
        REQUIRE(opened.has_value());
        Connection c = std::move(opened).value();

        const std::string host_abs =
            fs::absolute(shadow / "t.dbf").generic_string();
        const std::string host_can =
            fs::weakly_canonical(shadow / "t.dbf").generic_string();
        const std::string jail_can =
            fs::weakly_canonical(jail).generic_string();
        const std::string under_jail =
            fs::weakly_canonical(jail / "Creative.RAM" / "t.dbf")
                .generic_string();

        // Strict mode (legacy OFF): existing absolute path is honored
        // verbatim — that is the SAP free-table OPEN exception.
        {
            auto type = TableType::Cdx;
            CHECK(fs::path(c.resolve_table_file(host_abs, type))
                      .generic_string() == host_can);
        }

        c.set_legacy_paths(true);

        // Legacy ON: same absolute path must NOT stay on the shadow
        // tree. It is remounted under --data (fold/prefix-strip).
        {
            auto type = TableType::Cdx;
            const std::string got =
                fs::path(c.resolve_table_file(host_abs, type))
                    .generic_string();
            CHECK(got != host_can);
            // Remounted path lives under the jail (possibly a folded
            // remainder of the full host path).
            CHECK(got.rfind(jail_can, 0) == 0);
        }

        // ERP-style short path (the real client spelling): drive letter
        // dropped, remainder joined under --data → jail/Creative.RAM/t.dbf
        // even though the host also has a Creative.RAM tree outside.
        {
            auto type = TableType::Cdx;
            CHECK(fs::path(c.resolve_table_file(
                              "C:/Creative.RAM/t.dbf", type))
                      .generic_string() == under_jail);
            CHECK(fs::path(c.resolve_table_file(
                              "C:\\Creative.RAM\\t.dbf", type))
                      .generic_string() == under_jail);
        }

        // Create: parent of the host absolute path exists, but legacy
        // must still remount under the jail (not write next to shadow).
        {
            auto type = TableType::Cdx;
            const std::string create_host =
                fs::absolute(shadow / "new.dbf").generic_string();
            const std::string got = fs::path(c.resolve_table_file(
                                                 create_host, type,
                                                 /*for_create=*/true))
                                        .generic_string();
            CHECK(got != fs::path(create_host).generic_string());
            CHECK(got.rfind(jail_can, 0) == 0);

            // Short ERP create spelling lands exactly where open will.
            const std::string short_create =
                fs::path(c.resolve_table_file("C:/Creative.RAM/new.dbf",
                                              type, /*for_create=*/true))
                    .generic_string();
            CHECK(short_create ==
                  fs::weakly_canonical(jail / "Creative.RAM" / "new.dbf")
                      .generic_string());
        }
    }
    std::error_code ec;
    fs::remove_all(base, ec);
}

// Regression for the live-verified Linux-server bug: a remote
// AdsCreateTable routes through the session's lazy ABI connection, which
// never received the legacy flag — on a POSIX server the client-absolute
// path was not recognized as absolute and a literal "E:\..." file was
// created under the data root. With legacy on, a create spelled through
// a foreign drive letter must resolve to the same file a later open of
// the same name finds.
TEST_CASE("Connection legacy_paths create resolves a foreign-drive path") {
    auto dir = tmp_dir("legacycreate");
    {
        auto opened = Connection::open(dir.string());
        REQUIRE(opened.has_value());
        Connection c = std::move(opened).value();
        c.set_legacy_paths(true);

        const std::string abs = (dir / "newtable.dbf").generic_string();
        std::string foreign = "E:";
        foreign += (abs.size() >= 2 && abs[1] == ':') ? abs.substr(2) : abs;
        const std::string want =
            fs::weakly_canonical(dir / "newtable.dbf").generic_string();

        auto type = TableType::Cdx;
        CHECK(fs::path(c.resolve_table_file(foreign, type,
                                            /*for_create=*/true))
                  .generic_string() == want);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

namespace {

struct ResolveAuditGuard {
    std::ostringstream console;
    std::ostringstream file;
    ResolveAuditGuard() {
        openads::util::reset_audit_config();
        openads::util::set_audit_console(&console);
        openads::util::set_audit_file(&file);
        openads::util::set_audit_details_enabled(false);
    }
    ~ResolveAuditGuard() { openads::util::reset_audit_config(); }
};

bool has_audit_prefix(const std::string& line, const std::string& conn) {
    // CONN(6) SPACE ENTRY(8) SPACE THR(3) SPACE A/U+SEQ(8) SPACE YYYY-MM-DD HH:MM:SS.mmm
    if (line.size() < 6 + 1 + 8 + 1 + 3 + 1 + 1 + 8 + 1 + 23 + 1) return false;
    if (line.compare(0, 6, conn) != 0) return false;
    if (line[6] != ' ') return false;
    for (int i = 0; i < 8; ++i) {
        if (line[7 + i] < '0' || line[7 + i] > '9') return false;
    }
    // THR at 16-18 (digits), space at 19, A/U at 20, SEQ digits at 21-28
    if (line[15] != ' ') return false;
    for (int i = 0; i < 3; ++i) {
        if (line[16 + i] < '0' || line[16 + i] > '9') return false;
    }
    if (line[19] != ' ') return false;
    if (line[20] != 'A' && line[20] != 'U') return false;
    for (int i = 0; i < 8; ++i) {
        if (line[21 + i] < '0' || line[21 + i] > '9') return false;
    }
    // after SEQ(29); date/time space at 40; millisecond dot at 49
    return line[29] == ' ' && line[40] == ' ' && line[49] == '.';
}

}  // namespace

TEST_CASE("resolve logs RESOLVED with connection, entry, seq, timestamp") {
    ResolveAuditGuard g;
    auto dir = tmp_dir("audit_resolved");
    write_minimal_dbf(dir / "data.dbf");
    {
        auto opened = Connection::open(dir.string());
        REQUIRE(opened.has_value());
        Connection c = std::move(opened).value();
        REQUIRE(c.connection_serial().size() == 6);

        auto type = TableType::Cdx;
        const std::string resolved = c.resolve_table_file("data.dbf", type);

        CHECK(g.console.str().find("effective=") == std::string::npos);
        CHECK(g.file.str().find("effective=") == std::string::npos);

        const std::string& out = g.console.str();
        CHECK(out.find("RESOLVED=\"") != std::string::npos);
        CHECK(out.find(resolved) != std::string::npos);
        CHECK(out.find("(sandboxed)") != std::string::npos);
        CHECK(out.find("asked=\"data.dbf\"") != std::string::npos);
        CHECK(out.find("via=local") != std::string::npos);
        CHECK(has_audit_prefix(out, c.connection_serial()));
        CHECK(g.file.str() == out);
        CHECK(g.file.str().find("RESOLVED=") != std::string::npos);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("resolve detail lines share the entry serial and stay off the file") {
    ResolveAuditGuard g;
    openads::util::set_audit_details_enabled(true);
    auto dir = tmp_dir("audit_detail");
    write_minimal_dbf(dir / "data.dbf");
    {
        auto opened = Connection::open(dir.string());
        REQUIRE(opened.has_value());
        Connection c = std::move(opened).value();

        auto type = TableType::Cdx;
        (void)c.resolve_table_file("data.dbf", type);

        CHECK(g.console.str().find("input=") != std::string::npos);
        CHECK(g.console.str().find("RESOLVED=") != std::string::npos);
        CHECK(g.file.str().find("effective=") == std::string::npos);
        CHECK(g.file.str().find("RESOLVED=") != std::string::npos);
        CHECK(g.file.str().find("asked=\"data.dbf\"") != std::string::npos);

        // Same connection + entry serial on every line of this resolve.
        const std::string prefix6 = c.connection_serial() + " 00000001 ";
        CHECK(g.console.str().find(prefix6) != std::string::npos);
        CHECK(g.file.str().find(prefix6) != std::string::npos);
        CHECK(g.file.str().find("input=") == std::string::npos);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("remote server refuses host-absolute leftover and stays in the jail") {
    ResolveAuditGuard g;
    auto base = tmp_dir("remote_refuse_host");
    auto jail = base / "Temp";
    auto shadow = base / "Creative.RAM";
    fs::create_directories(jail);
    fs::create_directories(shadow);
    write_minimal_dbf(shadow / "t.dbf");
    {
        auto opened = Connection::open(jail.string());
        REQUIRE(opened.has_value());
        Connection c = std::move(opened).value();
        c.set_remote_server(true);

        const std::string host_abs =
            fs::absolute(shadow / "t.dbf").generic_string();
        const std::string host_can =
            fs::weakly_canonical(shadow / "t.dbf").generic_string();
        const std::string jail_can =
            fs::weakly_canonical(jail).generic_string();

        auto type = TableType::Cdx;
        const std::string got =
            fs::path(c.resolve_table_file(host_abs, type)).generic_string();
        // Must not open the leftover host tree — remote is safe storage.
        CHECK(got != host_can);
        CHECK(got.rfind(jail_can, 0) == 0);
        CHECK_FALSE(fs::exists(got));

        auto th = c.open_table(host_abs, TableType::Cdx);
        REQUIRE_FALSE(th.has_value());
        CHECK(th.error().code != 0);
    }
    std::error_code ec;
    fs::remove_all(base, ec);
}

TEST_CASE("resolve entry serial increments per call on the same connection") {
    ResolveAuditGuard g;
    auto dir = tmp_dir("audit_seq");
    write_minimal_dbf(dir / "a.dbf");
    write_minimal_dbf(dir / "b.dbf");
    {
        auto opened = Connection::open(dir.string());
        REQUIRE(opened.has_value());
        Connection c = std::move(opened).value();
        auto type = TableType::Cdx;
        (void)c.resolve_table_file("a.dbf", type);
        (void)c.resolve_table_file("b.dbf", type);
        CHECK(g.file.str().find(c.connection_serial() + " 00000001 ") !=
              std::string::npos);
        CHECK(g.file.str().find(c.connection_serial() + " 00000002 ") !=
              std::string::npos);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("resolve process seq is global; entry serial stays per connection") {
    ResolveAuditGuard g;
    auto dir = tmp_dir("audit_global_seq");
    write_minimal_dbf(dir / "a.dbf");
    write_minimal_dbf(dir / "b.dbf");
    {
        auto oa = Connection::open(dir.string());
        auto ob = Connection::open(dir.string());
        REQUIRE(oa.has_value());
        REQUIRE(ob.has_value());
        Connection ca = std::move(oa).value();
        Connection cb = std::move(ob).value();
        REQUIRE(ca.connection_serial() != cb.connection_serial());
        auto type = TableType::Cdx;
        (void)ca.resolve_table_file("a.dbf", type);
        type = TableType::Cdx;
        (void)cb.resolve_table_file("b.dbf", type);
        const std::string& file = g.file.str();
        // Each connection's first file is entry 00000001, seq A0000001/U0000001.
        CHECK(file.find(ca.connection_serial() + " 00000001 ") !=
              std::string::npos);
        CHECK(file.find(cb.connection_serial() + " 00000001 ") !=
              std::string::npos);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("resolve logs a given file only once per connection") {
    ResolveAuditGuard g;
    openads::util::set_audit_details_enabled(true);
    auto dir = tmp_dir("audit_once");
    write_minimal_dbf(dir / "sy900877.dbf");
    {
        auto opened = Connection::open(dir.string());
        REQUIRE(opened.has_value());
        Connection c = std::move(opened).value();
        auto type = TableType::Cdx;
        (void)c.resolve_table_file("sy900877.dbf", type);
        type = TableType::Cdx;
        (void)c.resolve_table_file("sy900877.dbf", type);

        const std::string& file = g.file.str();
        std::size_t n = 0;
        for (std::size_t p = 0;
             (p = file.find("RESOLVED=", p)) != std::string::npos;
             p += 9) {
            ++n;
        }
        CHECK(n == 1);
        std::size_t inputs = 0;
        for (std::size_t p = 0;
             (p = g.console.str().find("input=", p)) != std::string::npos;
             p += 6) {
            ++inputs;
        }
        CHECK(inputs == 1);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}
