// Minimal remote scope probe: connect to OPENADS_REMOTE_URI or argv[1].
// Exit 0 = scope honoured, 1 = failure.
#include "openads/ace.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static int fail(const char* msg) {
    std::cerr << "FAIL: " << msg << '\n';
    return 1;
}

static int stage_item_table(const char* dir) {
    std::string sp(dir);
    std::vector<UNSIGNED8> srv(sp.begin(), sp.end());
    srv.push_back(0);
    ADSHANDLE hConn = 0;
    if (AdsConnect60(srv.data(), ADS_LOCAL_SERVER,
                     nullptr, nullptr, 0, &hConn) != 0)
        return fail("stage: AdsConnect60");
    UNSIGNED8 fields[] = "GRP,Character,1;DATA,Character,8";
    UNSIGNED8 tname[]  = "item.dbf";
    ADSHANDLE hT = 0;
    if (AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                       0, 0, 0, 64, fields, &hT) != 0)
        return fail("stage: AdsCreateTable");
    UNSIGNED8 bag[] = "item.cdx", tag[] = "BYGRP", expr[] = "GRP";
    ADSHANDLE hIdx = 0;
    if (AdsCreateIndex61(hT, bag, tag, expr,
                         nullptr, nullptr, 0, 512, &hIdx) != 0)
        return fail("stage: AdsCreateIndex61");
    struct Row { const char* grp; const char* data; };
    const Row rows[] = {{"A","a1"},{"B","b1"},{"A","a2"},{"A","a3"}};
    for (const auto& r : rows) {
        if (AdsAppendRecord(hT) != 0) return fail("stage: append");
        UNSIGNED8 fg[] = "GRP", fd[] = "DATA";
        AdsSetString(hT, fg, (UNSIGNED8*)r.grp, 1);
        AdsSetString(hT, fd, (UNSIGNED8*)r.data,
                     static_cast<UNSIGNED32>(std::strlen(r.data)));
        if (AdsWriteRecord(hT) != 0) return fail("stage: write");
    }
    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    std::cout << "OK staged item.dbf in " << dir << '\n';
    return 0;
}

static int visible_count(ADSHANDLE hTable, ADSHANDLE nav) {
    if (AdsGotoTop(nav) != 0) return -1;
    int n = 0;
    for (;;) {
        UNSIGNED16 eof = 0;
        if (AdsAtEOF(hTable, &eof) != 0) return -1;
        if (eof) break;
        ++n;
        if (AdsSkip(nav, 1) != 0) return -1;
    }
    return n;
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--stage") == 0)
        return stage_item_table(argv[2]);

    const char* uri = std::getenv("OPENADS_REMOTE_URI");
    if ((uri == nullptr || uri[0] == '\0') && argc > 1) uri = argv[1];
    if (uri == nullptr || uri[0] == '\0') {
        return fail("set OPENADS_REMOTE_URI or pass tcp://host:port/path");
    }

    std::vector<UNSIGNED8> buf(uri, uri + std::strlen(uri));
    buf.push_back(0);

    ADSHANDLE hConn = 0, hTable = 0, hOrd = 0;
    if (AdsConnect60(buf.data(), ADS_REMOTE_SERVER,
                     nullptr, nullptr, 0, &hConn) != 0)
        return fail("AdsConnect60");
    UNSIGNED8 tname[] = "item.dbf";
    if (AdsOpenTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, &hTable) != 0)
        return fail("AdsOpenTable item.dbf");
    if (AdsGetIndexHandleByOrder(hTable, 1, &hOrd) != 0)
        return fail("AdsGetIndexHandleByOrder");

    UNSIGNED8 top[] = "A", bot[] = "A";
    if (AdsSetScope(hOrd, ADS_TOP, top, 1, ADS_STRINGKEY) != 0)
        return fail("AdsSetScope TOP");
    if (AdsSetScope(hOrd, ADS_BOTTOM, bot, 1, ADS_STRINGKEY) != 0)
        return fail("AdsSetScope BOTTOM");

    const int scoped_ix  = visible_count(hTable, hOrd);
    const int scoped_tbl = visible_count(hTable, hTable);
    if (scoped_ix != 3 || scoped_tbl != 3) {
        std::cerr << "scoped_ix=" << scoped_ix << " scoped_tbl=" << scoped_tbl
                  << " (expected 3)\n";
        return fail("scope not honoured");
    }

    if (AdsClearScope(hOrd, ADS_TOP) != 0) return fail("ClearScope TOP");
    if (AdsClearScope(hOrd, ADS_BOTTOM) != 0) return fail("ClearScope BOTTOM");
    const int full = visible_count(hTable, hOrd);
    if (full != 4) {
        std::cerr << "full=" << full << " (expected 4)\n";
        return fail("clear scope");
    }

    AdsCloseTable(hTable);
    AdsDisconnect(hConn);
    std::cout << "OK remote scope probe (" << uri << ")\n";
    return 0;
}