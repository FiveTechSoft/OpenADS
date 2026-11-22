#include "doctest.h"
#include "abi/wire_caller.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <thread>
using namespace openads::abi::wire_caller;
TEST_CASE("wire caller: unknown and disabled entries do not invent attribution") {
    CHECK(std::strcmp(snapshot().root, "unknown") == 0);
    Entry disabled("AdsNeverEntered", false);
    Reason ignored("not-a-caller");
    CHECK(snapshot().call == 0);
    char label_text[128]; label(label_text,sizeof(label_text));
    CHECK(std::strcmp(label_text,"unknown") == 0);
}
TEST_CASE("wire caller: nested ACE API preserves outer trigger and restores on return") {
    Entry outer("AdsGotoBottom",true); const auto first=snapshot();
    CHECK(first.call != 0); CHECK(first.thread != 0);
    {
        Entry nested("AdsGetKeyCount",true); const auto second=snapshot();
        CHECK(std::strcmp(second.root,"AdsGotoBottom")==0);
        CHECK(std::strcmp(second.current,"AdsGetKeyCount")==0);
        CHECK(second.call==first.call); CHECK(second.thread==first.thread);
    }
    CHECK(std::strcmp(snapshot().current,"AdsGotoBottom")==0);
    CHECK(snapshot().call==first.call);
}
TEST_CASE("wire caller: reason nesting and exception restore cannot leave stale caller") {
    const auto before=snapshot();
    try {
        Entry entry("AdsWriteRecord",true);
        Reason reason("batch+commit");
        char text[128];label(text,sizeof(text));
        CHECK(std::strcmp(text,"AdsWriteRecord/batch+commit")==0);
        { Reason nested("buffered-sets");CHECK(std::strcmp(snapshot().reason,"buffered-sets")==0); }
        CHECK(std::strcmp(snapshot().reason,"batch+commit")==0);
        throw std::runtime_error("test-only");
    } catch(const std::runtime_error&) {}
    CHECK(snapshot().call==before.call);
    CHECK(std::strcmp(snapshot().root,before.root)==0);
}
TEST_CASE("wire caller: concurrent threads have independent triggers and correlation") {
    std::array<Snapshot,2> seen{};
    std::array<std::thread,2> threads;
    for(std::size_t i=0;i<2;++i)threads[i]=std::thread([&,i]{
        Entry e(i?"AdsUnlockTable":"AdsGetRecordCount",true);
        Reason r(i?"buffered-sets":"request");seen[i]=snapshot();
    });
    for(auto& t:threads)t.join();
    CHECK(std::strcmp(seen[0].root,"AdsGetRecordCount")==0);
    CHECK(std::strcmp(seen[1].root,"AdsUnlockTable")==0);
    CHECK(seen[0].call != seen[1].call); CHECK(seen[0].thread != seen[1].thread);
    CHECK(snapshot().call==0);
}
TEST_CASE("wire caller: separate invocations keep stable thread and distinct call IDs") {
    Snapshot first{};{Entry e("AdsGetRecordCount",true);first=snapshot();}
    CHECK(snapshot().call==0);
    {Entry e("AdsGotoRecord",true);CHECK(snapshot().call!=first.call);CHECK(snapshot().thread==first.thread);}
    CHECK(snapshot().call==0);
}
