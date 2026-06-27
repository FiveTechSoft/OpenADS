// E2E repro: a CDX tag whose key is an EXPRESSION (UPPER(NOME)) must order the
// table correctly after a large CREATE INDEX. Community report: simple-field
// tags order fine, but an expression tag (UPPER(cNombre)) yields a disordered
// grid at scale. This walks the index and asserts the positioned records'
// UPPER(NOME) values come out in ascending order.
#include "doctest.h"
#include "openads/ace.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr std::uint16_t kNomeLen = 20;
constexpr std::uint32_t kRecs    = 3000;

// Build a DBF with one field NOME C(20). Record i (1-based) gets the lowercase
// name "prod{N-i}" — so recno order is the REVERSE of UPPER(NOME) sort order
// and the names share long prefixes (exercises the bulk leaf compression).
fs::path stage_dbf(const fs::path& dir) {
    fs::create_directories(dir);
    auto p = dir / "data.dbf";
    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    const std::uint16_t rec_len = 1 + kNomeLen;
    const std::uint16_t hdr_len = 32 + 32 + 1;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0]  = 0x03;
    hdr[4]  = static_cast<std::uint8_t>(kRecs & 0xFF);
    hdr[5]  = static_cast<std::uint8_t>((kRecs >> 8) & 0xFF);
    hdr[6]  = static_cast<std::uint8_t>((kRecs >> 16) & 0xFF);
    hdr[7]  = static_cast<std::uint8_t>((kRecs >> 24) & 0xFF);
    hdr[8]  = hdr_len & 0xFF; hdr[9] = (hdr_len >> 8) & 0xFF;
    hdr[10] = rec_len & 0xFF; hdr[11] = (rec_len >> 8) & 0xFF;
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> fd{};
    std::strncpy(reinterpret_cast<char*>(fd.data()), "NOME", 11);
    fd[11] = 'C'; fd[16] = static_cast<std::uint8_t>(kNomeLen);
    push(fd.data(), fd.size());
    file.push_back(0x0D);
    for (std::uint32_t i = 1; i <= kRecs; ++i) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "prod%05u", kRecs - i);
        std::string v(buf);
        v.resize(kNomeLen, ' ');
        file.push_back(' ');                 // not deleted
        push(v.data(), v.size());
    }
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

// Two-field DBF: NOME C(20) + CODIGO C(5), for a COMPOSITE expression key.
fs::path stage_dbf2(const fs::path& dir) {
    fs::create_directories(dir);
    auto p = dir / "data.dbf";
    std::vector<std::uint8_t> file;
    auto push = [&](const void* d, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(d);
        file.insert(file.end(), b, b + n);
    };
    const std::uint16_t cod_len = 5;
    const std::uint16_t rec_len = 1 + kNomeLen + cod_len;
    const std::uint16_t hdr_len = 32 + 32 + 32 + 1;
    std::array<std::uint8_t, 32> hdr{};
    hdr[0] = 0x03;
    hdr[4] = static_cast<std::uint8_t>(kRecs & 0xFF);
    hdr[5] = static_cast<std::uint8_t>((kRecs >> 8) & 0xFF);
    hdr[6] = static_cast<std::uint8_t>((kRecs >> 16) & 0xFF);
    hdr[7] = static_cast<std::uint8_t>((kRecs >> 24) & 0xFF);
    hdr[8] = hdr_len & 0xFF; hdr[9] = (hdr_len >> 8) & 0xFF;
    hdr[10] = rec_len & 0xFF; hdr[11] = (rec_len >> 8) & 0xFF;
    push(hdr.data(), hdr.size());
    std::array<std::uint8_t, 32> f1{};
    std::strncpy(reinterpret_cast<char*>(f1.data()), "NOME", 11);
    f1[11] = 'C'; f1[16] = static_cast<std::uint8_t>(kNomeLen);
    push(f1.data(), f1.size());
    std::array<std::uint8_t, 32> f2{};
    std::strncpy(reinterpret_cast<char*>(f2.data()), "CODIGO", 11);
    f2[11] = 'C'; f2[16] = static_cast<std::uint8_t>(cod_len);
    push(f2.data(), f2.size());
    file.push_back(0x0D);
    for (std::uint32_t i = 1; i <= kRecs; ++i) {
        char nb[24], cb[16];
        std::snprintf(nb, sizeof(nb), "prod%05u", (kRecs - i) / 3);  // dup NOMEs
        std::snprintf(cb, sizeof(cb), "%05u", i % 1000);
        std::string nv(nb); nv.resize(kNomeLen, ' ');
        std::string cv(cb); cv.resize(cod_len, ' ');
        file.push_back(' ');
        push(nv.data(), nv.size());
        push(cv.data(), cv.size());
    }
    file.push_back(0x1A);
    std::ofstream(p, std::ios::binary).write(
        reinterpret_cast<const char*>(file.data()),
        static_cast<std::streamsize>(file.size()));
    return p;
}

std::string upper(std::string s) {
    for (char& c : s) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    return s;
}

std::string read_field(ADSHANDLE hTable, const char* name) {
    UNSIGNED8 fld[16]; std::memcpy(fld, name, std::strlen(name) + 1);
    UNSIGNED8 buf[64] = {0};
    UNSIGNED32 cap = sizeof(buf);
    REQUIRE(AdsGetField(hTable, fld, buf, &cap, 0) == 0);
    return std::string(reinterpret_cast<const char*>(buf), cap);
}

std::string read_nome(ADSHANDLE hTable) {
    std::string s = read_field(hTable, "NOME");
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

} // namespace

TEST_CASE("CDX expression tag UPPER(NOME) orders a large table correctly (repro)") {
    auto dir = fs::temp_directory_path() / "openads_cdx_expr_reindex";
    std::error_code ec; fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 nm[16] = "data";
    REQUIRE(AdsOpenTable(hConn, nm, nm, ADS_CDX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED8 fn[16]  = "data";
    UNSIGNED8 tag[16] = "UPNOME";
    UNSIGNED8 expr[32] = "UPPER(NOME)";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, fn, tag, expr,
                            nullptr, nullptr, 0, 512, &hIdx) == 0);

    // Walk the table in the active (UPPER(NOME)) order; the positioned record's
    // UPPER(NOME) must be non-decreasing the whole way down.
    REQUIRE(AdsGotoTop(hTable) == 0);
    std::string prev;
    std::uint32_t seen = 0, disorder = 0;
    UNSIGNED16 eof = 0;
    REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    while (eof == 0) {
        std::string u = upper(read_nome(hTable));
        if (!prev.empty() && u < prev) ++disorder;
        prev = u;
        ++seen;
        REQUIRE(AdsSkip(hTable, 1) == 0);
        REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    }
    CHECK(seen == kRecs);
    CHECK(disorder == 0);

    REQUIRE(AdsCloseIndex(hIdx) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// Composite expression key: UPPER(NOME)+CODIGO (with many duplicate NOMEs so
// the second key component decides order). The reporter flagged "compuestos".
TEST_CASE("CDX composite expression UPPER(NOME)+CODIGO orders correctly (repro)") {
    auto dir = fs::temp_directory_path() / "openads_cdx_expr_comp";
    std::error_code ec; fs::remove_all(dir, ec);
    stage_dbf2(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 nm[16] = "data";
    REQUIRE(AdsOpenTable(hConn, nm, nm, ADS_CDX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED8 fn[16]  = "data";
    UNSIGNED8 tag[16] = "UPNCOD";
    UNSIGNED8 expr[40] = "UPPER(NOME)+CODIGO";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, fn, tag, expr,
                            nullptr, nullptr, 0, 512, &hIdx) == 0);

    REQUIRE(AdsGotoTop(hTable) == 0);
    std::string prev;
    std::uint32_t seen = 0, disorder = 0;
    UNSIGNED16 eof = 0;
    REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    while (eof == 0) {
        // Reconstruct the composite key exactly: upper(NOME[20]) + CODIGO[5].
        std::string key = upper(read_field(hTable, "NOME")) +
                          read_field(hTable, "CODIGO");
        if (!prev.empty() && key < prev) ++disorder;
        prev = key;
        ++seen;
        REQUIRE(AdsSkip(hTable, 1) == 0);
        REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    }
    CHECK(seen == kRecs);
    CHECK(disorder == 0);

    REQUIRE(AdsCloseIndex(hIdx) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}

// Same data, but force the per-record AdsReindex path (Table::reindex /
// sync_all_indexes_) instead of the bulk CREATE INDEX path.
TEST_CASE("CDX expression tag UPPER(NOME) stays ordered after AdsReindex (repro)") {
    auto dir = fs::temp_directory_path() / "openads_cdx_expr_reindex2";
    std::error_code ec; fs::remove_all(dir, ec);
    stage_dbf(dir);

    UNSIGNED8 srv[256];
    std::memcpy(srv, dir.string().c_str(), dir.string().size() + 1);
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn) == 0);

    ADSHANDLE hTable = 0;
    UNSIGNED8 nm[16] = "data";
    REQUIRE(AdsOpenTable(hConn, nm, nm, ADS_CDX, 1, 1, 0, 1, &hTable) == 0);

    UNSIGNED8 fn[16]  = "data";
    UNSIGNED8 tag[16] = "UPNOME";
    UNSIGNED8 expr[32] = "UPPER(NOME)";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, fn, tag, expr,
                            nullptr, nullptr, 0, 512, &hIdx) == 0);

    // Force the incremental/per-record rebuild path.
    REQUIRE(AdsReindex(hTable) == 0);

    REQUIRE(AdsGotoTop(hTable) == 0);
    std::string prev;
    std::uint32_t seen = 0, disorder = 0;
    UNSIGNED16 eof = 0;
    REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    while (eof == 0) {
        std::string u = upper(read_nome(hTable));
        if (!prev.empty() && u < prev) ++disorder;
        prev = u;
        ++seen;
        REQUIRE(AdsSkip(hTable, 1) == 0);
        REQUIRE(AdsAtEOF(hTable, &eof) == 0);
    }
    CHECK(seen == kRecs);
    CHECK(disorder == 0);

    REQUIRE(AdsCloseIndex(hIdx) == 0);
    REQUIRE(AdsCloseTable(hTable) == 0);
    REQUIRE(AdsDisconnect(hConn) == 0);
    fs::remove_all(dir, ec);
}
