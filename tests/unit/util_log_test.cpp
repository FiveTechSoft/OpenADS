#include "doctest.h"
#include "util/log.h"

#include <sstream>
#include <string>
#include <string_view>

using openads::util::Log;
using openads::util::LogLevel;

TEST_CASE("Log respects the configured level threshold") {
    std::ostringstream out;
    Log log{LogLevel::Info, &out};

    log.write(LogLevel::Debug, "debug-line");
    log.write(LogLevel::Info,  "info-line");
    log.write(LogLevel::Error, "err-line");

    const std::string buf = out.str();
    CHECK(buf.find("debug-line") == std::string::npos);
    CHECK(buf.find("info-line")  != std::string::npos);
    CHECK(buf.find("err-line")   != std::string::npos);
}

TEST_CASE("Log emits the level prefix") {
    std::ostringstream out;
    Log log{LogLevel::Trace, &out};
    log.write(LogLevel::Trace, "tag");
    CHECK(out.str().find("TRACE") != std::string::npos);
    CHECK(out.str().find("tag")   != std::string::npos);
}

TEST_CASE("Log discards output when sink is null") {
    Log log{LogLevel::Trace, nullptr};
    log.write(LogLevel::Error, "ignored");
    // No crash, no UB. Nothing else to assert.
    CHECK(true);
}

TEST_CASE("Log parses level from environment-style string") {
    CHECK(openads::util::log_level_from_string("trace") == LogLevel::Trace);
    CHECK(openads::util::log_level_from_string("DEBUG") == LogLevel::Debug);
    CHECK(openads::util::log_level_from_string("info")  == LogLevel::Info);
    CHECK(openads::util::log_level_from_string("warn")  == LogLevel::Warn);
    CHECK(openads::util::log_level_from_string("error") == LogLevel::Error);
    CHECK(openads::util::log_level_from_string("nonsense") == LogLevel::Info);
}

// ---------------------------------------------------------------------------
// Audit prefix: CONN(6) ENTRY(8) SEQ(8) TIMESTAMP
// HYT673 00000001 00000014 2026-08-16 22:12:13.826 RESOLVED="..." JAILED
// ---------------------------------------------------------------------------

using openads::util::AuditKind;

namespace {

struct AuditGuard {
    std::ostringstream console;
    std::ostringstream file;
    AuditGuard() {
        openads::util::reset_audit_config();
        openads::util::set_audit_console(&console);
        openads::util::set_audit_file(&file);
        openads::util::set_audit_details_enabled(false);
    }
    ~AuditGuard() { openads::util::reset_audit_config(); }
};

bool looks_like_timestamp(std::string_view ts) {
    // YYYY-MM-DD HH:MM:SS.mmm
    return ts.size() == 23 &&
           ts[4] == '-' && ts[7] == '-' &&
           ts[10] == ' ' &&
           ts[13] == ':' && ts[16] == ':' &&
           ts[19] == '.';
}

}  // namespace

TEST_CASE("format_connection_serial is 6 uppercase base36 digits") {
    CHECK(openads::util::format_connection_serial(0) == "000000");
    CHECK(openads::util::format_connection_serial(1) == "000001");
    CHECK(openads::util::format_connection_serial(35) == "00000Z");
    CHECK(openads::util::format_connection_serial(36) == "000010");
    // HYT673 = 17*36^5 + 34*36^4 + 29*36^3 + 6*36^2 + 7*36 + 3
    CHECK(openads::util::format_connection_serial(1086392991u) == "HYT673");
}

TEST_CASE("format_entry_serial is 8 zero-padded decimal digits") {
    CHECK(openads::util::format_entry_serial(1) == "00000001");
    CHECK(openads::util::format_entry_serial(42) == "00000042");
    CHECK(openads::util::format_entry_serial(99999999) == "99999999");
}

TEST_CASE("format_log_prefix is connection, entry, seq, timestamp") {
    const std::string p = openads::util::format_log_prefix(
        "HYT673", 1, 14, "2026-08-16 22:12:13.826");
    CHECK(p == "HYT673 00000001 00000014 2026-08-16 22:12:13.826");
}

TEST_CASE("next_audit_seq is process-wide and reset by reset_audit_config") {
    openads::util::reset_audit_config();
    CHECK(openads::util::next_audit_seq() == 1);
    CHECK(openads::util::next_audit_seq() == 2);
    CHECK(openads::util::next_audit_seq() == 3);
    openads::util::reset_audit_config();
    CHECK(openads::util::next_audit_seq() == 1);
}

TEST_CASE("format_log_timestamp matches YYYY-MM-DD HH:MM:SS.mmm") {
    CHECK(looks_like_timestamp(openads::util::format_log_timestamp()));
}

TEST_CASE("make_connection_serial is unique 6-char [0-9A-Z]") {
    const std::string a = openads::util::make_connection_serial();
    const std::string b = openads::util::make_connection_serial();
    CHECK(a.size() == 6);
    CHECK(b.size() == 6);
    CHECK(a != b);
    for (char c : a) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
        CHECK(ok);
    }
}

TEST_CASE("write_audit RESOLVED goes to console and file") {
    AuditGuard g;
    openads::util::write_audit(
        AuditKind::Resolved, "HYT673", 1, 14,
        "RESOLVED=\"C:/temp/t.dbf\" JAILED",
        "2026-08-16 22:12:13.826");

    const std::string want =
        "HYT673 00000001 00000014 2026-08-16 22:12:13.826 "
        "RESOLVED=\"C:/temp/t.dbf\" JAILED\n";
    CHECK(g.console.str() == want);
    CHECK(g.file.str() == want);
}

TEST_CASE("write_audit Detail is omitted from console by default") {
    AuditGuard g;
    openads::util::write_audit(
        AuditKind::Detail, "HYT673", 1, 1,
        "input=\"t\" effective=\"t\" data_dir=\".\"",
        "2026-08-16 22:12:13.826");
    openads::util::write_audit(
        AuditKind::Resolved, "HYT673", 1, 1,
        "RESOLVED=\"t.dbf\" JAILED",
        "2026-08-16 22:12:13.826");

    CHECK(g.console.str().find("input=") == std::string::npos);
    CHECK(g.console.str().find("RESOLVED=") != std::string::npos);
    CHECK(g.file.str().find("input=") == std::string::npos);
    CHECK(g.file.str().find("RESOLVED=") != std::string::npos);
}

TEST_CASE("write_remote_open_audit logs each asked name only once") {
    AuditGuard g;
    openads::util::write_remote_open_audit("c:\\creative.ram\\cac00001\\sy900877.dbf");
    openads::util::write_remote_open_audit("C:/Creative.RAM/CAC00001/sy900877.dbf");
    std::size_t n = 0;
    const std::string& file = g.file.str();
    for (std::size_t p = 0;
         (p = file.find("RESOLVED=", p)) != std::string::npos;
         p += 9) {
        ++n;
    }
    CHECK(n == 1);
}

TEST_CASE("write_remote_open_audit is a RESOLVED line in the file") {
    AuditGuard g;
    openads::util::write_remote_open_audit("C:/Creative.RAM/USERS.dbf");
    CHECK(g.console.str().find("RESOLVED=\"(remote)\"") != std::string::npos);
    CHECK(g.console.str().find("ASKED=\"C:/Creative.RAM/USERS.dbf\"") !=
          std::string::npos);
    CHECK(g.console.str().find("VIA=REMOTE") != std::string::npos);
    CHECK(g.file.str() == g.console.str());
}

TEST_CASE("write_audit Detail appears on console when enabled, never in file") {
    AuditGuard g;
    openads::util::set_audit_details_enabled(true);
    openads::util::write_audit(
        AuditKind::Detail, "HYT673", 1, 1,
        "input=\"t\" effective=\"t\" data_dir=\".\"",
        "2026-08-16 22:12:13.826");
    openads::util::write_audit(
        AuditKind::Detail, "HYT673", 1, 1,
        "LEGACY remap: \"a\" -> \"b\"",
        "2026-08-16 22:12:13.826");
    openads::util::write_audit(
        AuditKind::Resolved, "HYT673", 1, 1,
        "RESOLVED=\"b\" JAILED",
        "2026-08-16 22:12:13.826");

    CHECK(g.console.str().find("input=") != std::string::npos);
    CHECK(g.console.str().find("LEGACY remap") != std::string::npos);
    CHECK(g.console.str().find("RESOLVED=") != std::string::npos);
    CHECK(g.file.str().find("input=") == std::string::npos);
    CHECK(g.file.str().find("LEGACY remap") == std::string::npos);
    CHECK(g.file.str() ==
          "HYT673 00000001 00000001 2026-08-16 22:12:13.826 "
          "RESOLVED=\"b\" JAILED\n");
}
