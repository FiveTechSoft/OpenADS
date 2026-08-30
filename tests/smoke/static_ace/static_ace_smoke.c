// Link/runtime smoke test for the static ACE library (libopenace32.a /
// libopenace64.a). Proves a Harbour/MinGW-style consumer can resolve the
// ACE surface from the archive alone and exercise the embedded engine:
// version, create table, append, field write, record count, close.
//
// Build (x86, from repo root; g++ driver + -static so the .exe ends up
// with zero OpenADS/MinGW DLLs — only KERNEL32/WS2_32/UCRT remain):
//   i686-w64-mingw32-g++ -Iinclude tests/smoke/static_ace/static_ace_smoke.c \
//       build/mingw-x86/src/libopenace32.a -lws2_32 -lpsapi -static \
//       -o static_ace_smoke.exe
// Optional remote leg (wire-protocol client inside the archive):
//   set OPENADS_SMOKE_REMOTE=tcp://127.0.0.1:6262/C:/temp/static_smoke

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openads/ace.h"

static int fail(const char* what, UNSIGNED32 rc) {
    printf("FAIL: %s rc=%u\n", what, (unsigned)rc);
    return 1;
}

int main(void) {
    UNSIGNED32 rc, major = 0, minor = 0;
    UNSIGNED8 letter = 0, desc[128] = {0};
    UNSIGNED16 descLen = sizeof(desc);

    rc = AdsGetVersion(&major, &minor, &letter, desc, &descLen);
    if (rc != 0) return fail("AdsGetVersion", rc);
    printf("version: %u.%u%c %.60s\n", (unsigned)major, (unsigned)minor,
           letter ? letter : ' ', desc);

    AdsSetServerType(ADS_LOCAL_SERVER);

    const char* path = "static_ace_smoke.dbf";
    remove(path);

    ADSHANDLE hTable = 0;
    rc = AdsCreateTable90(0, (UNSIGNED8*)path, (UNSIGNED8*)"smoke",
                          ADS_CDX, ADS_ANSI, ADS_PROPRIETARY_LOCKING,
                          ADS_DEFAULT, 0,
                          (UNSIGNED8*)"NAME,C,20,0;AGE,N,3,0",
                          ADS_DEFAULT, NULL, &hTable);
    if (rc != 0) return fail("AdsCreateTable90", rc);

    rc = AdsAppendRecord(hTable);
    if (rc != 0) return fail("AdsAppendRecord", rc);
    rc = AdsSetField(hTable, (UNSIGNED8*)"NAME", (UNSIGNED8*)"Static", 6);
    if (rc != 0) return fail("AdsSetField", rc);

    UNSIGNED32 count = 0;
    rc = AdsGetRecordCount(hTable, ADS_DEFAULT, &count);
    if (rc != 0) return fail("AdsGetRecordCount", rc);
    if (count != 1) {
        printf("FAIL: expected 1 record, got %u\n", (unsigned)count);
        return 1;
    }

    rc = AdsCloseTable(hTable);
    if (rc != 0) return fail("AdsCloseTable", rc);

    /* Reopen and confirm persistence. */
    hTable = 0;
    rc = AdsOpenTable90(0, (UNSIGNED8*)path, (UNSIGNED8*)"smoke",
                        ADS_CDX, ADS_ANSI, ADS_PROPRIETARY_LOCKING,
                        ADS_DEFAULT, ADS_DEFAULT, NULL, &hTable);
    if (rc != 0) return fail("AdsOpenTable90", rc);
    count = 0;
    rc = AdsGetRecordCount(hTable, ADS_DEFAULT, &count);
    if (rc != 0) return fail("AdsGetRecordCount#2", rc);
    AdsCloseTable(hTable);
    if (count != 1) {
        printf("FAIL: after reopen expected 1 record, got %u\n",
               (unsigned)count);
        return 1;
    }

    remove(path);
    printf("STATIC_ACE_SMOKE: PASS (records=%u)\n", (unsigned)count);

    /* Optional remote leg: set OPENADS_SMOKE_REMOTE to a server URI
       (e.g. tcp://127.0.0.1:6262/C:/temp/static_smoke) to also exercise
       the wire-protocol client inside the archive — Vouch's real use. */
    {
        const char* uri = getenv("OPENADS_SMOKE_REMOTE");
        if (uri && *uri) {
            ADSHANDLE hConn = 0;
            rc = AdsConnect60((UNSIGNED8*)uri, ADS_REMOTE_SERVER,
                              NULL, NULL, ADS_DEFAULT, &hConn);
            if (rc != 0) return fail("AdsConnect60", rc);
            hTable = 0;
            rc = AdsOpenTable90(hConn, (UNSIGNED8*)"smoke_remote.dbf",
                                (UNSIGNED8*)"smoker", ADS_CDX, ADS_ANSI,
                                ADS_PROPRIETARY_LOCKING, ADS_DEFAULT,
                                ADS_DEFAULT, NULL, &hTable);
            if (rc == 5103 || rc == 5005 || rc == 5004) { /* not found: create */
                rc = AdsCreateTable90(hConn, (UNSIGNED8*)"smoke_remote.dbf",
                                      (UNSIGNED8*)"smoker", ADS_CDX, ADS_ANSI,
                                      ADS_PROPRIETARY_LOCKING, ADS_DEFAULT, 0,
                                      (UNSIGNED8*)"NAME,C,20,0",
                                      ADS_DEFAULT, NULL, &hTable);
            }
            if (rc != 0) return fail("remote open/create", rc);
            rc = AdsAppendRecord(hTable);
            if (rc != 0) return fail("remote append", rc);
            rc = AdsSetField(hTable, (UNSIGNED8*)"NAME",
                             (UNSIGNED8*)"Remote", 6);
            if (rc != 0) return fail("remote setfield", rc);
            count = 0;
            rc = AdsGetRecordCount(hTable, ADS_DEFAULT, &count);
            if (rc != 0 || count < 1)
                return fail("remote count", rc ? rc : 9999);
            AdsCloseTable(hTable);
            AdsDisconnect(hConn);
            printf("STATIC_ACE_SMOKE_REMOTE: PASS (records=%u)\n",
                   (unsigned)count);
        }
    }
    return 0;
}
