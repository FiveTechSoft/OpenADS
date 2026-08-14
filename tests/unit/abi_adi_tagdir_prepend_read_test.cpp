// Reading a .adi whose tag directory is in PREPEND layout must still report
// tags in CREATION order.
//
// Found against a real 22-tag bag in a Harbour ERP (reported by russimicro,
// Zerus ERP): the application resolves orders by NUMBER -- DBSETORDER(n) /
// OrdSetFocus(n), as a browse doing click-to-sort on a column does -- and a
// build that read raw directory-slot order returned ordinal 1 as the LAST
// tag created. Nothing errored; the orders were simply the wrong ones, and
// column sorting silently stopped working.
//
// Both layouts exist in the wild and a bag cannot be assumed to have been
// written by this engine: append is add_tag()'s default, prepend is what it
// writes with CreateParams::prepend_tag_dir and what SAP's Advantage Data
// Architect produces. list_tags() must not care which -- it sorts by each
// tag's header PAGE NUMBER, allocated at end-of-file in creation order no
// matter which directory slot the entry lands in.
//
// The sibling test (abi_adi_tagdir_order_test) covers the append layout.
// This one covers prepend, by taking a bag this engine just wrote and
// reversing its tag directory in place -- byte for byte what a
// prepend-written bag looks like.

#include "doctest.h"
#include "drivers/adi/adi_index.h"
#include "openads/ace.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Reverse the tag-directory entries on page 2, turning an append-written bag
// into the prepend layout without touching any per-tag header or B+tree page.
bool reverse_tag_directory(const std::string& adi_path) {
    using namespace openads::drivers::adi;

    std::fstream f(adi_path, std::ios::in | std::ios::out | std::ios::binary);
    if (!f) return false;

    const std::streamoff pg2 = static_cast<std::streamoff>(ADI_PAGE_SIZE) * 2;
    std::vector<char> page(ADI_PAGE_SIZE);
    f.seekg(pg2);
    f.read(page.data(), ADI_PAGE_SIZE);
    if (!f) return false;

    // Entry count lives at page offset 2, u16 LE.
    const std::uint16_t count = static_cast<std::uint16_t>(
        (static_cast<unsigned char>(page[2])) |
        (static_cast<unsigned char>(page[3]) << 8));
    if (count < 2) return false;

    char* base = page.data() + ADI_TAGDIR_ENTRY_START;
    for (std::uint16_t i = 0; i < count / 2; ++i) {
        char* a = base + static_cast<std::size_t>(i) * ADI_TAGDIR_ENTRY_SIZE;
        char* b = base + static_cast<std::size_t>(count - 1 - i)
                       * ADI_TAGDIR_ENTRY_SIZE;
        char tmp[ADI_TAGDIR_ENTRY_SIZE];
        std::memcpy(tmp, a,   ADI_TAGDIR_ENTRY_SIZE);
        std::memcpy(a,   b,   ADI_TAGDIR_ENTRY_SIZE);
        std::memcpy(b,   tmp, ADI_TAGDIR_ENTRY_SIZE);
    }

    f.seekp(pg2);
    f.write(page.data(), ADI_PAGE_SIZE);
    f.flush();
    return static_cast<bool>(f);
}

} // namespace

TEST_CASE("ADI: list_tags reports creation order for a PREPEND tag directory") {
    fs::path tmp = fs::temp_directory_path() / "openads_adi_tagdir_prepend";
    { std::error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp, ec); }

    UNSIGNED8 srv[260]{};
    std::memcpy(srv, tmp.string().c_str(), tmp.string().size());
    ADSHANDLE hConn = 0;
    REQUIRE(AdsConnect60(srv, ADS_LOCAL_SERVER, nullptr, nullptr, 0, &hConn)
            == AE_SUCCESS);

    UNSIGNED8 tbl[]    = "pre.adt";
    UNSIGNED8 flddef[] = "CCODIGO,Character,10;CNOMBRE,Character,40;CGRUPO,Character,6";
    ADSHANDLE hTable   = 0;
    REQUIRE(AdsCreateTable(hConn, tbl, nullptr, ADS_ADT, ADS_ANSI, 0, 0, 0,
                           flddef, &hTable) == AE_SUCCESS);

    for (std::uint32_t i = 0; i < 50u; ++i) {
        REQUIRE(AdsAppendRecord(hTable) == AE_SUCCESS);
        char cod[16];
        std::snprintf(cod, sizeof(cod), "C%08u", i);
        AdsSetString(hTable, (UNSIGNED8*)"CCODIGO", (UNSIGNED8*)cod,
                     (UNSIGNED32)std::strlen(cod));
        AdsSetString(hTable, (UNSIGNED8*)"CNOMBRE", (UNSIGNED8*)"N", 1);
        AdsSetString(hTable, (UNSIGNED8*)"CGRUPO", (UNSIGNED8*)"G01", 3);
    }
    REQUIRE(AdsWriteRecord(hTable) == AE_SUCCESS);

    UNSIGNED8 idxfile[] = "pre.adi";
    ADSHANDLE hIdx = 0;
    REQUIRE(AdsCreateIndex61(hTable, idxfile, (UNSIGNED8*)"TCODIGO",
                             (UNSIGNED8*)"CCODIGO", nullptr, nullptr,
                             0, 0, &hIdx) == AE_SUCCESS);
    REQUIRE(AdsCreateIndex61(hTable, idxfile, (UNSIGNED8*)"TNOMBRE",
                             (UNSIGNED8*)"CNOMBRE", nullptr, nullptr,
                             0, 0, &hIdx) == AE_SUCCESS);
    REQUIRE(AdsCreateIndex61(hTable, idxfile, (UNSIGNED8*)"TGRUPO",
                             (UNSIGNED8*)"CGRUPO", nullptr, nullptr,
                             0, 0, &hIdx) == AE_SUCCESS);
    REQUIRE(AdsCloseTable(hTable) == AE_SUCCESS);
    REQUIRE(AdsDisconnect(hConn) == AE_SUCCESS);

    const std::string adi = (tmp / "pre.adi").string();
    const std::string adt = (tmp / "pre.adt").string();

    // Baseline: as written (append layout), creation order. Whether the
    // reported names are the TAG names (v2 tag header) or the indexed FIELD
    // names (legacy layout) depends on OPENADS_ADI_V2 -- irrelevant here, and
    // deliberately not asserted: what this test pins is the ORDER.
    std::vector<std::string> as_written;
    {
        auto tags = openads::drivers::adi::AdiIndex::list_tags(adi, adt);
        REQUIRE(tags);
        REQUIRE(tags.value().size() == 3u);
        as_written = tags.value();
        // First created first, last created last.
        CHECK(as_written[0] != as_written[2]);
    }

    // Now make it a prepend-layout bag and read it again.
    REQUIRE(reverse_tag_directory(adi));

    {
        auto tags = openads::drivers::adi::AdiIndex::list_tags(adi, adt);
        REQUIRE(tags);
        REQUIRE(tags.value().size() == 3u);
        // Identical to the append reading: the header-page sort recovers
        // creation order from a reversed directory. Without it this comes
        // back exactly backwards and every DBSETORDER(<literal>) in the ERP
        // lands on the wrong tag.
        CHECK(tags.value() == as_written);
        CHECK(tags.value()[0] != as_written[2]);
    }

    { std::error_code ec; fs::remove_all(tmp, ec); }
}
