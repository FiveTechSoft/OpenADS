// Native ACE mirror of the owner-supplied deleted-only scope repro.
// Synthetic rows only. No application or customer data is needed.
#include "doctest.h"
#include "openads/ace.h"
#include "network/server.h"
#include <filesystem>
#include <string>

namespace {
UNSIGNED8* bytes(std::string& value) {
    return reinterpret_cast<UNSIGNED8*>(value.data());
}
void success(UNSIGNED32 status) { REQUIRE(status == 0); }
}

TEST_CASE("deleted-only scope never substitutes a preceding live row") {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "openads_deleted_scope";
    fs::remove_all(dir);
    fs::create_directories(dir);
    ADSHANDLE connection = 0, table = 0, index = 0;
    std::string root = dir.string(), name = "T_REC.dbf";
    std::string definition = "WRKORD,C,6,0;LABCOD,C,15,0";
    success(AdsConnect60(bytes(root), ADS_LOCAL_SERVER, nullptr, nullptr, 0, &connection));
    success(AdsCreateTable(connection, bytes(name), nullptr, ADS_CDX, ADS_ANSI,
                           0, 0, 0, bytes(definition), &table));
    std::string field = "WRKORD", label = "LABCOD";
    unsigned row = 0;
    for (std::string key : {"105555", "105556", "105557", "105557", "105566"}) {
        ++row;
        success(AdsAppendRecord(table));
        success(AdsSetString(table, bytes(field), bytes(key), 6));
        std::string value = "C" + key + "_" + std::to_string(row);
        success(AdsSetString(table, bytes(label), bytes(value),
                             static_cast<UNSIGNED32>(value.size())));
        if (row == 3 || row == 4) success(AdsDeleteRecord(table));
        success(AdsWriteRecord(table));
    }
    std::string bag = (dir / "T_REC.cdx").string(), tag = "WRKORD", expression = "WRKORD";
    success(AdsCreateIndex61(table, bytes(bag), bytes(tag), bytes(expression),
                            nullptr, nullptr, ADS_COMPOUND, 512, &index));
    success(AdsCloseTable(table));
    success(AdsDisconnect(connection));

    for (bool remote : {false, true}) {
        INFO("remote=" << remote);
        openads::network::Server server;
        if (remote) REQUIRE(server.start("127.0.0.1", 0));
        std::string uri = remote ? "tcp://127.0.0.1:" + std::to_string(server.port()) +
                                  "/" + dir.generic_string() : root;
        success(AdsConnect60(bytes(uri), remote ? ADS_REMOTE_SERVER : ADS_LOCAL_SERVER,
                             nullptr, nullptr, 0, &connection));
        success(AdsShowDeleted(0));
        success(AdsOpenTable(connection, bytes(name), nullptr, ADS_CDX, ADS_ANSI,
                             ADS_COMPATIBLE_LOCKING, ADS_IGNORERIGHTS, ADS_SHARED, &table));
        success(AdsGetIndexHandleByOrder(table, 1, &index));
        for (std::string key : {"105557", "105560", "105556", "105557"}) {
            INFO("scope=" << key);
            success(AdsSetScope(index, ADS_TOP, bytes(key), 6, ADS_STRINGKEY));
            success(AdsSetScope(index, ADS_BOTTOM, bytes(key), 6, ADS_STRINGKEY));
            const bool visible = key == "105556";
            auto check_landing = [&] {
                UNSIGNED16 eof = 0;
                UNSIGNED32 recno = 0, length = 63;
                UNSIGNED8 value[64] = {};
                success(AdsAtEOF(table, &eof));
                success(AdsGetRecordNum(table, 0, &recno));
                const auto status = AdsGetString(table, bytes(field), value, &length, ADS_NONE);
                CHECK((status == 0 || (!visible && status == 5068)));
                CHECK(eof == (visible ? 0 : 1));
                CHECK(recno == (visible ? 2 : 6));
                CHECK(std::string(reinterpret_cast<char*>(value)) == (visible ? "105556" : ""));
            };
            success(AdsGotoTop(index));
            check_landing();
            // Repeated navigation and the opposite-boundary pair must retain truth.
            success(AdsGotoTop(index));
            check_landing();
            success(AdsGotoBottom(index));
            check_landing();
            success(AdsGotoTop(index));
            check_landing();
            success(AdsSkip(index, 1));
            UNSIGNED16 eof = 0;
            success(AdsAtEOF(table, &eof));
            CHECK(eof == 1);
        }
        success(AdsCloseTable(table));
        success(AdsDisconnect(connection));
        server.stop();
    }
    fs::remove_all(dir);
}

// Explicit refresh must read the same ordered twin that supplies its row ack.
// Synthetic peer write, checked flush/unlock, plus independent fresh control.
TEST_CASE("indexed explicit refresh sees peer writes without losing position") {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "openads_indexed_refresh_peer";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    openads::network::Server server;
    REQUIRE(server.start("127.0.0.1", 0));
    std::string uri = "tcp://127.0.0.1:" + std::to_string(server.port()) + "/" + dir.generic_string();
    ADSHANDLE conn = 0, peer = 0, table = 0, writer = 0, index = 0, wi = 0;
    success(AdsConnect60(bytes(uri), ADS_REMOTE_SERVER, nullptr, nullptr, 0, &conn));
    std::string name = "fresh.dbf", bag = "fresh.cdx", tag = "CODE", field = "CITY";
    std::string def = "CODE,C,10,0;CITY,C,20,0";
    success(AdsCreateTable(conn, bytes(name), nullptr, ADS_CDX, ADS_ANSI, 0, 0, 0, bytes(def), &table));
    for (std::string key : {"AAA", "BBB", "CCC", "DDD"}) {
        success(AdsAppendRecord(table));
        success(AdsSetString(table, bytes(tag), bytes(key), 3));
        std::string value = "OLD";
        success(AdsSetString(table, bytes(field), bytes(value), 3));
        success(AdsWriteRecord(table));
    }
    success(AdsCreateIndex61(table, bytes(bag), bytes(tag), bytes(tag), nullptr, nullptr, ADS_COMPOUND, 0, &index));
    success(AdsCloseTable(table));
    success(AdsOpenTable(conn, bytes(name), nullptr, ADS_CDX, ADS_ANSI, 0, ADS_IGNORERIGHTS, ADS_SHARED, &table));
    success(AdsGetIndexHandle(table, bytes(tag), &index));
    success(AdsSetIndexOrderByHandle(table, index));
    success(AdsConnect60(bytes(uri), ADS_REMOTE_SERVER, nullptr, nullptr, 0, &peer));
    success(AdsOpenTable(peer, bytes(name), nullptr, ADS_CDX, ADS_ANSI, 0, ADS_IGNORERIGHTS, ADS_SHARED, &writer));
    success(AdsGetIndexHandle(writer, bytes(tag), &wi));
    success(AdsSetIndexOrderByHandle(writer, wi));
    auto position = [&](UNSIGNED32 expected) {
        UNSIGNED32 rn = 0; UNSIGNED16 bof = 0, eof = 0, ord = 0;
        success(AdsGetRecordNum(table, 0, &rn));
        success(AdsAtBOF(table, &bof)); success(AdsAtEOF(table, &eof));
        success(AdsGetIndexOrderByHandle(index, &ord));
        CHECK(rn == expected); CHECK(bof == 0); CHECK(eof == 0); CHECK(ord == 1);
    };
    auto value = [&](ADSHANDLE h, std::string& col) {
        UNSIGNED8 buf[64] = {}; UNSIGNED32 size = 63;
        success(AdsGetString(h, bytes(col), buf, &size, ADS_NONE));
        return std::string(reinterpret_cast<char*>(buf));
    };
    success(AdsGotoTop(index)); success(AdsSkip(index, 2)); position(3);
    success(AdsGotoRecord(writer, 3)); success(AdsLockRecord(writer, 3));
    std::string changed = "PEER";
    success(AdsSetString(writer, bytes(field), bytes(changed), 4));
    success(AdsWriteRecord(writer)); success(AdsFlushFileBuffers(writer)); success(AdsUnlockRecord(writer, 3));
    ADSHANDLE control = 0;
    success(AdsOpenTable(peer, bytes(name), nullptr, ADS_CDX, ADS_ANSI, 0, ADS_IGNORERIGHTS, ADS_SHARED, &control));
    success(AdsGotoRecord(control, 3)); CHECK(value(control, field) == "PEER");
    success(AdsCloseTable(control));
    success(AdsSkip(index, 0)); success(AdsRefreshRecord(table));
    CHECK(value(table, field) == "PEER"); position(3);
    success(AdsSkip(index, 1)); position(4);
    success(AdsGotoTop(index)); success(AdsSkip(index, 2));
    success(AdsGotoRecord(writer, 3)); success(AdsLockRecord(writer, 3));
    success(AdsDeleteRecord(writer)); success(AdsWriteRecord(writer));
    success(AdsFlushFileBuffers(writer)); success(AdsUnlockRecord(writer, 3));
    success(AdsRefreshRecord(table));
    UNSIGNED16 deleted = 0; success(AdsIsRecordDeleted(table, &deleted)); CHECK(deleted == 1);
    success(AdsLockRecord(writer, 3)); success(AdsRecallRecord(writer));
    std::string moved = "CCC5";
    success(AdsSetString(writer, bytes(tag), bytes(moved), 4));
    success(AdsWriteRecord(writer)); success(AdsFlushFileBuffers(writer)); success(AdsUnlockRecord(writer, 3));
    success(AdsRefreshRecord(table)); position(3);
    CHECK(value(table, tag) == "CCC5");
    success(AdsIsRecordDeleted(table, &deleted)); CHECK(deleted == 0);
    success(AdsSkip(index, 1)); position(4);
    success(AdsCloseTable(writer)); success(AdsCloseTable(table));
    success(AdsDisconnect(peer)); success(AdsDisconnect(conn));
    server.stop(); fs::remove_all(dir, ec);
}
