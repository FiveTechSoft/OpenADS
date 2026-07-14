// Remote SET DELETED probe — reproduces the rddads startup order that
// leaked deleted rows on remote scoped walks (Tim Stone, 2026-07-14):
//   SET DELETED ON  ->  AdsConnect60(remote)  ->  OrdScope  ->  walk.
// Usage:
//   remote_deleted_probe --stage tcp://host:port/dir   (create itemdel.dbf)
//   remote_deleted_probe tcp://host:port/dir           (run the probe)
// Exit 0 = deleted rows hidden, 1 = failure.
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

static std::vector<UNSIGNED8> zstr(const char* s) {
    std::vector<UNSIGNED8> v(s, s + std::strlen(s));
    v.push_back(0);
    return v;
}

// Stage itemdel.dbf with 3 "A" rows (one deleted) + 1 "B". A tcp://
// target stages over the wire; a plain directory stages locally (for
// servers that predate remote CreateTable — scp the files afterwards).
static int stage(const char* uri) {
    auto buf = zstr(uri);
    const bool remote = std::strncmp(uri, "tcp://", 6) == 0;
    ADSHANDLE hConn = 0;
    if (AdsConnect60(buf.data(),
                     remote ? ADS_REMOTE_SERVER : ADS_LOCAL_SERVER,
                     nullptr, nullptr, 0, &hConn) != 0)
        return fail("stage: AdsConnect60");
    UNSIGNED8 fields[] = "GRP,Character,1;DATA,Character,8";
    UNSIGNED8 tname[]  = "itemdel.dbf";
    ADSHANDLE hT = 0;
    if (AdsCreateTable(hConn, tname, nullptr, ADS_CDX,
                       0, 0, 0, 64, fields, &hT) != 0)
        return fail("stage: AdsCreateTable");
    UNSIGNED8 bag[] = "itemdel.cdx", tag[] = "BYGRP", expr[] = "GRP";
    ADSHANDLE hIdx = 0;
    if (AdsCreateIndex61(hT, bag, tag, expr,
                         nullptr, nullptr, 0, 512, &hIdx) != 0)
        return fail("stage: AdsCreateIndex61");
    struct Row { const char* grp; const char* data; };
    const Row rows[] = {{"A","live1"},{"A","gone"},{"A","live2"},{"B","other"}};
    for (const auto& r : rows) {
        if (AdsAppendRecord(hT) != 0) return fail("stage: append");
        UNSIGNED8 fg[] = "GRP", fd[] = "DATA";
        AdsSetString(hT, fg, (UNSIGNED8*)r.grp, 1);
        AdsSetString(hT, fd, (UNSIGNED8*)r.data,
                     static_cast<UNSIGNED32>(std::strlen(r.data)));
        if (AdsWriteRecord(hT) != 0) return fail("stage: write");
    }
    if (AdsGotoRecord(hT, 2) != 0) return fail("stage: goto 2");
    if (AdsDeleteRecord(hT) != 0) return fail("stage: delete");
    if (AdsWriteRecord(hT) != 0) return fail("stage: write del");
    AdsCloseTable(hT);
    AdsDisconnect(hConn);
    std::cout << "OK staged itemdel.dbf (rec 2 deleted)\n";
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--stage") == 0)
        return stage(argv[2]);

    const char* uri = std::getenv("OPENADS_REMOTE_URI");
    if ((uri == nullptr || uri[0] == '\0') && argc > 1) uri = argv[1];
    if (uri == nullptr || uri[0] == '\0')
        return fail("set OPENADS_REMOTE_URI or pass tcp://host:port/path");

    // SET DELETED ON *before* the connection exists — the order every
    // rddads/FWH app uses.
    if (AdsShowDeleted(0) != 0) return fail("AdsShowDeleted(0)");

    auto buf = zstr(uri);
    ADSHANDLE hConn = 0, hTable = 0, hOrd = 0;
    if (AdsConnect60(buf.data(), ADS_REMOTE_SERVER,
                     nullptr, nullptr, 0, &hConn) != 0)
        return fail("AdsConnect60");
    UNSIGNED8 tname[] = "itemdel.dbf";
    if (AdsOpenTable(hConn, tname, nullptr, ADS_CDX, 0, 0, 0, 0, &hTable) != 0)
        return fail("AdsOpenTable itemdel.dbf");
    if (AdsGetIndexHandleByOrder(hTable, 1, &hOrd) != 0)
        return fail("AdsGetIndexHandleByOrder");

    UNSIGNED8 top[] = "A", bot[] = "A";
    if (AdsSetScope(hOrd, ADS_TOP, top, 1, ADS_STRINGKEY) != 0)
        return fail("AdsSetScope TOP");
    if (AdsSetScope(hOrd, ADS_BOTTOM, bot, 1, ADS_STRINGKEY) != 0)
        return fail("AdsSetScope BOTTOM");

    if (AdsGotoTop(hOrd) != 0) return fail("AdsGotoTop");
    int n = 0, ndel = 0;
    for (;;) {
        UNSIGNED16 eof = 0;
        if (AdsAtEOF(hTable, &eof) != 0) return fail("AdsAtEOF");
        if (eof) break;
        UNSIGNED16 del = 0;
        if (AdsIsRecordDeleted(hTable, &del) != 0)
            return fail("AdsIsRecordDeleted");
        if (del) ++ndel;
        ++n;
        if (AdsSkip(hOrd, 1) != 0) return fail("AdsSkip");
    }

    AdsCloseTable(hTable);
    AdsDisconnect(hConn);

    std::cout << "scoped walk: " << n << " rows, " << ndel
              << " deleted (" << uri << ")\n";
    if (n != 2 || ndel != 0) {
        return fail("deleted rows visible in scoped walk");
    }
    std::cout << "OK remote SET DELETED probe\n";
    return 0;
}
