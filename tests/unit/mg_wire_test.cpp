#include "doctest.h"

#include "network/mg_wire.h"

using namespace openads;

TEST_CASE("mg_wire snapshot round-trips identically") {
    mgmt::MgSnapshot in;
    in.users = 2; in.connections = 2; in.workareas = 4;
    in.tables = 3; in.indexes = 1; in.locks = 1;
    in.worker_threads = 5; in.server_type = 0;

    mgmt::MgUser u;
    u.name = "bob"; u.address = "1.2.3.4:9"; u.os_login = "bob";
    u.conn_no = 7;
    in.user_list.push_back(u);

    mgmt::MgTable t;
    t.name = "t.adt"; t.user = "bob"; t.conn_no = 7;
    t.open_mode = 1; t.lock_type = 2;
    in.table_list.push_back(t);

    mgmt::MgLock l;
    l.user = "bob"; l.conn_no = 7; l.recno = 99;
    l.table = "c:/data/t.adt";
    in.lock_list.push_back(l);

    mgmt::MgIndex x;
    x.name = "c:/data/t.adi"; x.tag = "byname"; x.expression = "NAME";
    x.table = "c:/data/t.adt"; x.user = "bob"; x.conn_no = 7;
    in.index_list.push_back(x);

    std::string blob = network::encode_mg_snapshot(in);
    auto out = network::decode_mg_snapshot(blob);
    REQUIRE(out.has_value());

    const mgmt::MgSnapshot& s = out.value();
    CHECK(s.connections == 2);
    CHECK(s.worker_threads == 5);
    REQUIRE(s.user_list.size() == 1);
    CHECK(s.user_list[0].name == "bob");
    CHECK(s.user_list[0].conn_no == 7);
    REQUIRE(s.table_list.size() == 1);
    CHECK(s.table_list[0].name == "t.adt");
    CHECK(s.table_list[0].lock_type == 2);
    REQUIRE(s.lock_list.size() == 1);
    CHECK(s.lock_list[0].recno == 99);
    CHECK(s.lock_list[0].table == "c:/data/t.adt");
    REQUIRE(s.index_list.size() == 1);
    CHECK(s.index_list[0].name == "c:/data/t.adi");
    CHECK(s.index_list[0].tag == "byname");
    CHECK(s.index_list[0].table == "c:/data/t.adt");
    CHECK(s.index_list[0].user == "bob");
    CHECK(s.index_list[0].conn_no == 7);
}

TEST_CASE("mg_wire request round-trips") {
    std::string blob = network::encode_mg_request(
        network::MgRequestKind::KillUser, 13);
    auto req = network::decode_mg_request(blob);
    REQUIRE(req.has_value());
    CHECK(req.value().kind == network::MgRequestKind::KillUser);
    CHECK(req.value().arg == 13);
}

TEST_CASE("mg_wire rejects a truncated snapshot") {
    mgmt::MgSnapshot in;
    in.user_list.resize(3);   // claims 3 users, body absent
    std::string blob = network::encode_mg_snapshot(in);
    blob.resize(10);          // chop the body
    auto out = network::decode_mg_snapshot(blob);
    CHECK_FALSE(out.has_value());
}
