#include "doctest.h"

#include "mgmt/mg_collector.h"
#include "mgmt/mg_snapshot.h"
#include "mgmt/mg_stats.h"

#include <cstring>
#include <string>

using openads::mgmt::MgCollector;
using openads::mgmt::MgSnapshot;
using openads::mgmt::MgStats;

namespace {
std::string c_str_of(const UNSIGNED8* p, std::size_t cap) {
    std::size_t n = 0;
    while (n < cap && p[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(p), n);
}
}  // namespace

TEST_CASE("MgCollector::install_info reports the product string") {
    MgSnapshot snap;
    MgCollector c(snap);

    ADS_MGMT_INSTALL_INFO info = c.install_info();
    // Since 1.8.14 this carries the real build version. Release builds
    // report "OpenADS X.Y.Z"; a dev build may drop the brand so the longer
    // git-describe string fits the 16-byte field.
    std::string ver = c_str_of(info.aucVersionStr, sizeof(info.aucVersionStr));
    CHECK_FALSE(ver.empty());
    CHECK(ver != "OpenADS 1.0");    // the pre-1.8.14 hardcoded string
    // OpenADS is not serial-licensed: serial reports empty.
    CHECK(c_str_of(info.aucSerialNumber,
                   sizeof(info.aucSerialNumber)).empty());
}

TEST_CASE("MgCollector::activity_info maps counts and uptime") {
    MgSnapshot snap;
    snap.connections     = 3;
    snap.workareas       = 7;
    snap.tables          = 5;
    snap.indexes         = 2;
    snap.locks           = 1;
    snap.worker_threads  = 4;
    snap.user_list.resize(3);   // 3 users
    snap.max_connections = 9;
    snap.operations      = 120;
    snap.logged_errors   = 2;

    MgCollector c(snap);
    ADS_MGMT_ACTIVITY_INFO a = c.activity_info();

    CHECK(a.ulOperations              == 120);
    CHECK(a.ulLoggedErrors            == 2);
    CHECK(a.stConnections.ulInUse     == 3);
    CHECK(a.stConnections.ulMaxUsed   == 9);
    CHECK(a.stWorkAreas.ulInUse       == 7);
    CHECK(a.stTables.ulInUse          == 5);
    CHECK(a.stIndexes.ulInUse         == 2);
    CHECK(a.stLocks.ulInUse           == 1);
    CHECK(a.stWorkerThreads.ulInUse   == 4);
    CHECK(a.stUsers.ulInUse           == 3);
}

TEST_CASE("MgCollector::activity_info splits uptime seconds") {
    MgSnapshot snap;
    snap.uptime_seconds = 90061;   // 1d 1h 1m 1s
    MgCollector c(snap);
    ADS_MGMT_ACTIVITY_INFO a = c.activity_info();
    CHECK(a.stUpTime.usDays    == 1);
    CHECK(a.stUpTime.usHours   == 1);
    CHECK(a.stUpTime.usMinutes == 1);
    CHECK(a.stUpTime.usSeconds == 1);
}

TEST_CASE("MgCollector::comm_stats reports real packet totals only") {
    MgSnapshot snap;
    snap.packets_in       = 40;
    snap.packets_out      = 60;
    snap.disconnects      = 2;
    snap.partial_connects = 1;

    MgCollector c(snap);
    ADS_MGMT_COMM_STATS s = c.comm_stats();

    CHECK(s.ulTotalPackets      == 100);   // in + out
    CHECK(s.ulDisconnectedUsers == 2);
    CHECK(s.ulPartialConnects   == 1);
    // No checksum / sequencing in our framing — honest zeros.
    CHECK(s.dPercentCheckSums   == doctest::Approx(0.0));
    CHECK(s.ulCheckSumFailures  == 0);
    CHECK(s.ulRcvPktOutOfSeq    == 0);
}

TEST_CASE("MgCollector::config_params echoes live counts") {
    MgSnapshot snap;
    snap.connections = 3;
    snap.tables      = 5;
    snap.worker_threads = 4;

    MgCollector c(snap);
    ADS_MGMT_CONFIG_PARAMS p = c.config_params();
    CHECK(p.ulNumConnections  == 3);
    CHECK(p.ulNumTables       == 5);
    CHECK(p.usNumWorkerThreads == 4);
    // NetWare-era ECB fields are honest zeros.
    CHECK(p.usNumReceiveECBs  == 0);
    CHECK(p.usNumSendECBs     == 0);
}

TEST_CASE("MgCollector list accessors map snapshot vectors") {
    MgSnapshot snap;

    openads::mgmt::MgUser u;
    u.name = "alice"; u.address = "10.0.0.2:5000";
    u.os_login = "alice"; u.conn_no = 1;
    snap.user_list.push_back(u);

    openads::mgmt::MgTable t;
    t.name = "orders.adt"; t.user = "alice";
    t.conn_no = 1; t.open_mode = 0; t.lock_type = ADS_MGMT_NO_LOCK;
    snap.table_list.push_back(t);

    openads::mgmt::MgIndex ix;
    ix.name = "orders.adi"; ix.tag = "CUSTNO";
    ix.expression = "CUSTNO";
    snap.index_list.push_back(ix);

    openads::mgmt::MgLock lk;
    lk.user = "alice"; lk.conn_no = 1; lk.recno = 42;
    snap.lock_list.push_back(lk);

    MgCollector c(snap);

    auto users = c.user_names();
    REQUIRE(users.size() == 1);
    CHECK(c_str_of(users[0].aucUserName,
                   sizeof(users[0].aucUserName)) == "alice");
    CHECK(users[0].usConnNumber == 1);

    auto tables = c.open_tables();
    REQUIRE(tables.size() == 1);
    CHECK(c_str_of(tables[0].aucTableName,
                   sizeof(tables[0].aucTableName)) == "orders.adt");

    auto idxs = c.open_indexes();
    REQUIRE(idxs.size() == 1);
    CHECK(c_str_of(idxs[0].aucTagName,
                   sizeof(idxs[0].aucTagName)) == "CUSTNO");

    auto lks = c.locks();
    REQUIRE(lks.size() == 1);
    CHECK(lks[0].ulRecordNumber == 42);

    ADS_MGMT_LOCK_INFO owner = c.lock_owner(42);
    CHECK(owner.ulRecordNumber == 42);
    CHECK(c_str_of(owner.aucUserName,
                   sizeof(owner.aucUserName)) == "alice");

    ADS_MGMT_LOCK_INFO none = c.lock_owner(999);
    CHECK(none.ulRecordNumber == 0);
}

TEST_CASE("MgCollector::config_memory reports the snapshot RSS total") {
    MgSnapshot snap;
    snap.rss_bytes = 1048576;
    MgCollector c(snap);
    CHECK(c.config_memory().ulTotalConfigMem
              == doctest::Approx(1048576.0));
}

TEST_CASE("MgCollector::config_params reports the server port") {
    MgSnapshot snap;
    snap.server_port = 16262;
    MgCollector c(snap);
    auto p = c.config_params();
    CHECK(p.usSendIPPort    == 16262);
    CHECK(p.usReceiveIPPort == 16262);
}

TEST_CASE("capture_mg_stats folds MgStats counters into the snapshot") {
    MgStats stats;
    stats.operations.store(7);
    stats.packets_in.store(11);
    stats.packets_out.store(13);
    stats.disconnects.store(3);
    stats.max_connections.store(5);

    MgSnapshot snap;
    openads::mgmt::capture_mg_stats(snap, stats);

    CHECK(snap.operations      == 7);
    CHECK(snap.packets_in      == 11);
    CHECK(snap.packets_out     == 13);
    CHECK(snap.disconnects     == 3);
    CHECK(snap.max_connections == 5);

    // A MgCollector built from that snapshot surfaces the values.
    MgCollector c(snap);
    CHECK(c.comm_stats().ulTotalPackets == 24);
    CHECK(c.activity_info().ulOperations == 7);
}
