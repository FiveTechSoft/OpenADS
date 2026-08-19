/* GENERATED — do not edit by hand.
 * x86-only __stdcall aliases of the ACE entry points, so ace32.dll
 * exports the AdsXxx@N decorated names 32-bit Harbour rddads imports.
 * The plain __cdecl exports in ace_exports.cpp are untouched. The two
 * extra /export pragmas per function re-export each cdecl implementation
 * under its undecorated (AdsXxx) and underscore (_AdsXxx) names, so
 * MinGW-built rddads (cdecl references) and GetProcAddress callers can
 * resolve them too - reported by Pritpal Bedi. */
#include <stddef.h>
#include <stdint.h>

/* ARC32 bring-up trace (temporary): one line per wrapper call. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static void arc_log_rc(const char* name, unsigned rc, int nargs, ...) {
    static int on = -1;
    FILE* f; int i; va_list ap;
    if (on < 0) on = getenv("OPENADS_ARC_TRACE") != NULL;
    if (!on) return;
    f = fopen("C:/OpenADS/_arc32/ace_calls.log", "a");
    if (!f) return;
    fprintf(f, "W %s rc=%u args:", name, rc);
    va_start(ap, nargs);
    for (i = 0; i < nargs; i++) fprintf(f, " %lx", (unsigned long)va_arg(ap, uintptr_t));
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

typedef uint8_t  UNSIGNED8;
typedef int16_t  SIGNED16;
typedef uint16_t UNSIGNED16;
typedef uint32_t UNSIGNED32;
typedef int64_t  SIGNED64;
typedef uint64_t UNSIGNED64;
typedef int32_t  SIGNED32;
typedef uintptr_t ADSHANDLE;
#ifndef ENTRYPOINT
#  define ENTRYPOINT
#endif
#ifndef __stdcall
#  define __stdcall _stdcall
#endif

/* ---- AdsAddCustomKey ---- */
#define AdsAddCustomKey oadsimpl_AdsAddCustomKey
extern UNSIGNED32 ENTRYPOINT AdsAddCustomKey(ADSHANDLE hIndex);
#undef AdsAddCustomKey
#pragma comment(linker, "/alternatename:_oadsimpl_AdsAddCustomKey=_AdsAddCustomKey")
#pragma comment(linker, "/export:AdsAddCustomKey=_AdsAddCustomKey")
__declspec(dllexport) UNSIGNED32 __stdcall AdsAddCustomKey(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsAddCustomKey(a0);
    arc_log_rc("AdsAddCustomKey", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsAppendRecord ---- */
#define AdsAppendRecord oadsimpl_AdsAppendRecord
extern UNSIGNED32 ENTRYPOINT AdsAppendRecord(ADSHANDLE hTable);
#undef AdsAppendRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsAppendRecord=_AdsAppendRecord")
#pragma comment(linker, "/export:AdsAppendRecord=_AdsAppendRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsAppendRecord(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsAppendRecord(a0);
    arc_log_rc("AdsAppendRecord", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsApplicationExit ---- */
#define AdsApplicationExit oadsimpl_AdsApplicationExit
extern UNSIGNED32 ENTRYPOINT AdsApplicationExit(void);
#undef AdsApplicationExit
#pragma comment(linker, "/alternatename:_oadsimpl_AdsApplicationExit=_AdsApplicationExit")
#pragma comment(linker, "/export:AdsApplicationExit=_AdsApplicationExit")
__declspec(dllexport) UNSIGNED32 __stdcall AdsApplicationExit(void) {
    UNSIGNED32 rc = oadsimpl_AdsApplicationExit();
    arc_log_rc("AdsApplicationExit", rc, 0);
    return rc;
}

/* ---- AdsAtBOF ---- */
#define AdsAtBOF oadsimpl_AdsAtBOF
extern UNSIGNED32 ENTRYPOINT AdsAtBOF(ADSHANDLE hTable, UNSIGNED16* pbAtBegin);
#undef AdsAtBOF
#pragma comment(linker, "/alternatename:_oadsimpl_AdsAtBOF=_AdsAtBOF")
#pragma comment(linker, "/export:AdsAtBOF=_AdsAtBOF")
__declspec(dllexport) UNSIGNED32 __stdcall AdsAtBOF(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsAtBOF(a0, a1);
    arc_log_rc("AdsAtBOF", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsAtEOF ---- */
#define AdsAtEOF oadsimpl_AdsAtEOF
extern UNSIGNED32 ENTRYPOINT AdsAtEOF(ADSHANDLE hTable, UNSIGNED16* pbAtEnd);
#undef AdsAtEOF
#pragma comment(linker, "/alternatename:_oadsimpl_AdsAtEOF=_AdsAtEOF")
#pragma comment(linker, "/export:AdsAtEOF=_AdsAtEOF")
__declspec(dllexport) UNSIGNED32 __stdcall AdsAtEOF(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsAtEOF(a0, a1);
    arc_log_rc("AdsAtEOF", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsBeginTransaction ---- */
#define AdsBeginTransaction oadsimpl_AdsBeginTransaction
extern UNSIGNED32 ENTRYPOINT AdsBeginTransaction(ADSHANDLE hConnect);
#undef AdsBeginTransaction
#pragma comment(linker, "/alternatename:_oadsimpl_AdsBeginTransaction=_AdsBeginTransaction")
#pragma comment(linker, "/export:AdsBeginTransaction=_AdsBeginTransaction")
__declspec(dllexport) UNSIGNED32 __stdcall AdsBeginTransaction(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsBeginTransaction(a0);
    arc_log_rc("AdsBeginTransaction", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsBinaryToFile ---- */
#define AdsBinaryToFile oadsimpl_AdsBinaryToFile
extern UNSIGNED32 ENTRYPOINT AdsBinaryToFile(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED8* pucPath);
#undef AdsBinaryToFile
#pragma comment(linker, "/alternatename:_oadsimpl_AdsBinaryToFile=_AdsBinaryToFile")
#pragma comment(linker, "/export:AdsBinaryToFile=_AdsBinaryToFile")
__declspec(dllexport) UNSIGNED32 __stdcall AdsBinaryToFile(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsBinaryToFile(a0, a1, a2);
    arc_log_rc("AdsBinaryToFile", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsBinaryToFileW ---- */
#define AdsBinaryToFileW oadsimpl_AdsBinaryToFileW
extern UNSIGNED32 ENTRYPOINT AdsBinaryToFileW(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pwcPath);
#undef AdsBinaryToFileW
#pragma comment(linker, "/alternatename:_oadsimpl_AdsBinaryToFileW=_AdsBinaryToFileW")
#pragma comment(linker, "/export:AdsBinaryToFileW=_AdsBinaryToFileW")
__declspec(dllexport) UNSIGNED32 __stdcall AdsBinaryToFileW(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsBinaryToFileW(a0, a1, a2);
    arc_log_rc("AdsBinaryToFileW", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsCacheOpenCursors ---- */
#define AdsCacheOpenCursors oadsimpl_AdsCacheOpenCursors
extern UNSIGNED32 ENTRYPOINT AdsCacheOpenCursors(UNSIGNED16 usCacheCount);
#undef AdsCacheOpenCursors
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCacheOpenCursors=_AdsCacheOpenCursors")
#pragma comment(linker, "/export:AdsCacheOpenCursors=_AdsCacheOpenCursors")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCacheOpenCursors(UNSIGNED16 a0) {
    UNSIGNED32 rc = oadsimpl_AdsCacheOpenCursors(a0);
    arc_log_rc("AdsCacheOpenCursors", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsCacheOpenTables ---- */
#define AdsCacheOpenTables oadsimpl_AdsCacheOpenTables
extern UNSIGNED32 ENTRYPOINT AdsCacheOpenTables(UNSIGNED16 usCacheCount);
#undef AdsCacheOpenTables
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCacheOpenTables=_AdsCacheOpenTables")
#pragma comment(linker, "/export:AdsCacheOpenTables=_AdsCacheOpenTables")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCacheOpenTables(UNSIGNED16 a0) {
    UNSIGNED32 rc = oadsimpl_AdsCacheOpenTables(a0);
    arc_log_rc("AdsCacheOpenTables", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsCacheRecords ---- */
#define AdsCacheRecords oadsimpl_AdsCacheRecords
extern UNSIGNED32 ENTRYPOINT AdsCacheRecords(ADSHANDLE hTable, UNSIGNED16 usRecCount);
#undef AdsCacheRecords
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCacheRecords=_AdsCacheRecords")
#pragma comment(linker, "/export:AdsCacheRecords=_AdsCacheRecords")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCacheRecords(ADSHANDLE a0, UNSIGNED16 a1) {
    UNSIGNED32 rc = oadsimpl_AdsCacheRecords(a0, a1);
    arc_log_rc("AdsCacheRecords", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsCancelUpdate ---- */
#define AdsCancelUpdate oadsimpl_AdsCancelUpdate
extern UNSIGNED32 ENTRYPOINT AdsCancelUpdate(ADSHANDLE hTable);
#undef AdsCancelUpdate
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCancelUpdate=_AdsCancelUpdate")
#pragma comment(linker, "/export:AdsCancelUpdate=_AdsCancelUpdate")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCancelUpdate(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsCancelUpdate(a0);
    arc_log_rc("AdsCancelUpdate", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsCancelUpdate90 ---- */
#define AdsCancelUpdate90 oadsimpl_AdsCancelUpdate90
extern UNSIGNED32 ENTRYPOINT AdsCancelUpdate90(ADSHANDLE hTable, UNSIGNED32 ulOptions);
#undef AdsCancelUpdate90
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCancelUpdate90=_AdsCancelUpdate90")
#pragma comment(linker, "/export:AdsCancelUpdate90=_AdsCancelUpdate90")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCancelUpdate90(ADSHANDLE a0, UNSIGNED32 a1) {
    UNSIGNED32 rc = oadsimpl_AdsCancelUpdate90(a0, a1);
    arc_log_rc("AdsCancelUpdate90", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsCheckExistence ---- */
#define AdsCheckExistence oadsimpl_AdsCheckExistence
extern UNSIGNED32 ENTRYPOINT AdsCheckExistence(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16* pbExists);
#undef AdsCheckExistence
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCheckExistence=_AdsCheckExistence")
#pragma comment(linker, "/export:AdsCheckExistence=_AdsCheckExistence")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCheckExistence(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsCheckExistence(a0, a1, a2);
    arc_log_rc("AdsCheckExistence", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsClearAOF ---- */
#define AdsClearAOF oadsimpl_AdsClearAOF
extern UNSIGNED32 ENTRYPOINT AdsClearAOF(ADSHANDLE hTable);
#undef AdsClearAOF
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearAOF=_AdsClearAOF")
#pragma comment(linker, "/export:AdsClearAOF=_AdsClearAOF")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearAOF(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsClearAOF(a0);
    arc_log_rc("AdsClearAOF", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsClearAllScopes ---- */
#define AdsClearAllScopes oadsimpl_AdsClearAllScopes
extern UNSIGNED32 ENTRYPOINT AdsClearAllScopes(ADSHANDLE hTable);
#undef AdsClearAllScopes
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearAllScopes=_AdsClearAllScopes")
#pragma comment(linker, "/export:AdsClearAllScopes=_AdsClearAllScopes")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearAllScopes(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsClearAllScopes(a0);
    arc_log_rc("AdsClearAllScopes", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsClearCallbackFunction ---- */
#define AdsClearCallbackFunction oadsimpl_AdsClearCallbackFunction
extern UNSIGNED32 ENTRYPOINT AdsClearCallbackFunction(void);
#undef AdsClearCallbackFunction
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearCallbackFunction=_AdsClearCallbackFunction")
#pragma comment(linker, "/export:AdsClearCallbackFunction=_AdsClearCallbackFunction")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearCallbackFunction(void) {
    UNSIGNED32 rc = oadsimpl_AdsClearCallbackFunction();
    arc_log_rc("AdsClearCallbackFunction", rc, 0);
    return rc;
}

/* ---- AdsClearDefault ---- */
#define AdsClearDefault oadsimpl_AdsClearDefault
extern UNSIGNED32 ENTRYPOINT AdsClearDefault(void);
#undef AdsClearDefault
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearDefault=_AdsClearDefault")
#pragma comment(linker, "/export:AdsClearDefault=_AdsClearDefault")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearDefault(void) {
    UNSIGNED32 rc = oadsimpl_AdsClearDefault();
    arc_log_rc("AdsClearDefault", rc, 0);
    return rc;
}

/* ---- AdsClearFilter ---- */
#define AdsClearFilter oadsimpl_AdsClearFilter
extern UNSIGNED32 ENTRYPOINT AdsClearFilter(ADSHANDLE hTable);
#undef AdsClearFilter
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearFilter=_AdsClearFilter")
#pragma comment(linker, "/export:AdsClearFilter=_AdsClearFilter")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearFilter(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsClearFilter(a0);
    arc_log_rc("AdsClearFilter", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsClearProgressCallback ---- */
#define AdsClearProgressCallback oadsimpl_AdsClearProgressCallback
extern UNSIGNED32 ENTRYPOINT AdsClearProgressCallback(void);
#undef AdsClearProgressCallback
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearProgressCallback=_AdsClearProgressCallback")
#pragma comment(linker, "/export:AdsClearProgressCallback=_AdsClearProgressCallback")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearProgressCallback(void) {
    UNSIGNED32 rc = oadsimpl_AdsClearProgressCallback();
    arc_log_rc("AdsClearProgressCallback", rc, 0);
    return rc;
}

/* ---- AdsClearRelation ---- */
#define AdsClearRelation oadsimpl_AdsClearRelation
extern UNSIGNED32 ENTRYPOINT AdsClearRelation(ADSHANDLE hTable);
#undef AdsClearRelation
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearRelation=_AdsClearRelation")
#pragma comment(linker, "/export:AdsClearRelation=_AdsClearRelation")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearRelation(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsClearRelation(a0);
    arc_log_rc("AdsClearRelation", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsClearSQLAbortFunc ---- */
#define AdsClearSQLAbortFunc oadsimpl_AdsClearSQLAbortFunc
extern UNSIGNED32 ENTRYPOINT AdsClearSQLAbortFunc(void);
#undef AdsClearSQLAbortFunc
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearSQLAbortFunc=_AdsClearSQLAbortFunc")
#pragma comment(linker, "/export:AdsClearSQLAbortFunc=_AdsClearSQLAbortFunc")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearSQLAbortFunc(void) {
    UNSIGNED32 rc = oadsimpl_AdsClearSQLAbortFunc();
    arc_log_rc("AdsClearSQLAbortFunc", rc, 0);
    return rc;
}

/* ---- AdsClearSQLParams ---- */
#define AdsClearSQLParams oadsimpl_AdsClearSQLParams
extern UNSIGNED32 ENTRYPOINT AdsClearSQLParams(ADSHANDLE hStatement);
#undef AdsClearSQLParams
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearSQLParams=_AdsClearSQLParams")
#pragma comment(linker, "/export:AdsClearSQLParams=_AdsClearSQLParams")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearSQLParams(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsClearSQLParams(a0);
    arc_log_rc("AdsClearSQLParams", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsClearScope ---- */
#define AdsClearScope oadsimpl_AdsClearScope
extern UNSIGNED32 ENTRYPOINT AdsClearScope(ADSHANDLE hIndex, UNSIGNED16 usScope);
#undef AdsClearScope
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearScope=_AdsClearScope")
#pragma comment(linker, "/export:AdsClearScope=_AdsClearScope")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearScope(ADSHANDLE a0, UNSIGNED16 a1) {
    UNSIGNED32 rc = oadsimpl_AdsClearScope(a0, a1);
    arc_log_rc("AdsClearScope", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsCloneTable ---- */
#define AdsCloneTable oadsimpl_AdsCloneTable
extern UNSIGNED32 ENTRYPOINT AdsCloneTable(ADSHANDLE hTable, ADSHANDLE* phClone);
#undef AdsCloneTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCloneTable=_AdsCloneTable")
#pragma comment(linker, "/export:AdsCloneTable=_AdsCloneTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCloneTable(ADSHANDLE a0, ADSHANDLE* a1) {
    UNSIGNED32 rc = oadsimpl_AdsCloneTable(a0, a1);
    arc_log_rc("AdsCloneTable", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsCloseAllIndexes ---- */
#define AdsCloseAllIndexes oadsimpl_AdsCloseAllIndexes
extern UNSIGNED32 ENTRYPOINT AdsCloseAllIndexes(ADSHANDLE hTable);
#undef AdsCloseAllIndexes
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCloseAllIndexes=_AdsCloseAllIndexes")
#pragma comment(linker, "/export:AdsCloseAllIndexes=_AdsCloseAllIndexes")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCloseAllIndexes(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsCloseAllIndexes(a0);
    arc_log_rc("AdsCloseAllIndexes", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsCloseAllTables ---- */
#define AdsCloseAllTables oadsimpl_AdsCloseAllTables
extern UNSIGNED32 ENTRYPOINT AdsCloseAllTables(void);
#undef AdsCloseAllTables
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCloseAllTables=_AdsCloseAllTables")
#pragma comment(linker, "/export:AdsCloseAllTables=_AdsCloseAllTables")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCloseAllTables(void) {
    UNSIGNED32 rc = oadsimpl_AdsCloseAllTables();
    arc_log_rc("AdsCloseAllTables", rc, 0);
    return rc;
}

/* ---- AdsCloseCachedTables ---- */
#define AdsCloseCachedTables oadsimpl_AdsCloseCachedTables
extern UNSIGNED32 ENTRYPOINT AdsCloseCachedTables(ADSHANDLE hConnect);
#undef AdsCloseCachedTables
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCloseCachedTables=_AdsCloseCachedTables")
#pragma comment(linker, "/export:AdsCloseCachedTables=_AdsCloseCachedTables")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCloseCachedTables(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsCloseCachedTables(a0);
    arc_log_rc("AdsCloseCachedTables", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsCloseIndex ---- */
#define AdsCloseIndex oadsimpl_AdsCloseIndex
extern UNSIGNED32 ENTRYPOINT AdsCloseIndex(ADSHANDLE hIndex);
#undef AdsCloseIndex
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCloseIndex=_AdsCloseIndex")
#pragma comment(linker, "/export:AdsCloseIndex=_AdsCloseIndex")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCloseIndex(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsCloseIndex(a0);
    arc_log_rc("AdsCloseIndex", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsCloseSQLStatement ---- */
#define AdsCloseSQLStatement oadsimpl_AdsCloseSQLStatement
extern UNSIGNED32 ENTRYPOINT AdsCloseSQLStatement(ADSHANDLE hStatement);
#undef AdsCloseSQLStatement
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCloseSQLStatement=_AdsCloseSQLStatement")
#pragma comment(linker, "/export:AdsCloseSQLStatement=_AdsCloseSQLStatement")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCloseSQLStatement(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsCloseSQLStatement(a0);
    arc_log_rc("AdsCloseSQLStatement", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsCloseTable ---- */
#define AdsCloseTable oadsimpl_AdsCloseTable
extern UNSIGNED32 ENTRYPOINT AdsCloseTable(ADSHANDLE hTable);
#undef AdsCloseTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCloseTable=_AdsCloseTable")
#pragma comment(linker, "/export:AdsCloseTable=_AdsCloseTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCloseTable(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsCloseTable(a0);
    arc_log_rc("AdsCloseTable", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsCommitTransaction ---- */
#define AdsCommitTransaction oadsimpl_AdsCommitTransaction
extern UNSIGNED32 ENTRYPOINT AdsCommitTransaction(ADSHANDLE hConnect);
#undef AdsCommitTransaction
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCommitTransaction=_AdsCommitTransaction")
#pragma comment(linker, "/export:AdsCommitTransaction=_AdsCommitTransaction")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCommitTransaction(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsCommitTransaction(a0);
    arc_log_rc("AdsCommitTransaction", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsConnect ---- */
#define AdsConnect oadsimpl_AdsConnect
extern UNSIGNED32 ENTRYPOINT AdsConnect(UNSIGNED8* pucServer, ADSHANDLE* phConnect);
#undef AdsConnect
#pragma comment(linker, "/alternatename:_oadsimpl_AdsConnect=_AdsConnect")
#pragma comment(linker, "/export:AdsConnect=_AdsConnect")
__declspec(dllexport) UNSIGNED32 __stdcall AdsConnect(UNSIGNED8* a0, ADSHANDLE* a1) {
    UNSIGNED32 rc = oadsimpl_AdsConnect(a0, a1);
    arc_log_rc("AdsConnect", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsConnect26 ---- */
#define AdsConnect26 oadsimpl_AdsConnect26
extern UNSIGNED32 ENTRYPOINT AdsConnect26(UNSIGNED8* pucServer, UNSIGNED16 usServerType, ADSHANDLE* phConnect);
#undef AdsConnect26
#pragma comment(linker, "/alternatename:_oadsimpl_AdsConnect26=_AdsConnect26")
#pragma comment(linker, "/export:AdsConnect26=_AdsConnect26")
__declspec(dllexport) UNSIGNED32 __stdcall AdsConnect26(UNSIGNED8* a0, UNSIGNED16 a1, ADSHANDLE* a2) {
    UNSIGNED32 rc = oadsimpl_AdsConnect26(a0, a1, a2);
    arc_log_rc("AdsConnect26", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsConnect60 ---- */
#define AdsConnect60 oadsimpl_AdsConnect60
extern UNSIGNED32 ENTRYPOINT AdsConnect60(UNSIGNED8* pucServer, UNSIGNED16 usServerType, UNSIGNED8* pucUserName, UNSIGNED8* pucPassword, UNSIGNED32 ulOptions, ADSHANDLE* phConnect);
#undef AdsConnect60
#pragma comment(linker, "/alternatename:_oadsimpl_AdsConnect60=_AdsConnect60")
#pragma comment(linker, "/export:AdsConnect60=_AdsConnect60")
__declspec(dllexport) UNSIGNED32 __stdcall AdsConnect60(UNSIGNED8* a0, UNSIGNED16 a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED32 a4, ADSHANDLE* a5) {
    UNSIGNED32 rc = oadsimpl_AdsConnect60(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsConnect60", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsContinue ---- */
#define AdsContinue oadsimpl_AdsContinue
extern UNSIGNED32 ENTRYPOINT AdsContinue(ADSHANDLE hTable, UNSIGNED16* pbFound);
#undef AdsContinue
#pragma comment(linker, "/alternatename:_oadsimpl_AdsContinue=_AdsContinue")
#pragma comment(linker, "/export:AdsContinue=_AdsContinue")
__declspec(dllexport) UNSIGNED32 __stdcall AdsContinue(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsContinue(a0, a1);
    arc_log_rc("AdsContinue", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsConvertAnsiToOem ---- */
#define AdsConvertAnsiToOem oadsimpl_AdsConvertAnsiToOem
extern UNSIGNED32 ENTRYPOINT AdsConvertAnsiToOem(UNSIGNED8* pucBuf, UNSIGNED32* pulLen);
#undef AdsConvertAnsiToOem
#pragma comment(linker, "/alternatename:_oadsimpl_AdsConvertAnsiToOem=_AdsConvertAnsiToOem")
#pragma comment(linker, "/export:AdsConvertAnsiToOem=_AdsConvertAnsiToOem")
__declspec(dllexport) UNSIGNED32 __stdcall AdsConvertAnsiToOem(UNSIGNED8* a0, UNSIGNED32* a1) {
    UNSIGNED32 rc = oadsimpl_AdsConvertAnsiToOem(a0, a1);
    arc_log_rc("AdsConvertAnsiToOem", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsConvertOemToAnsi ---- */
#define AdsConvertOemToAnsi oadsimpl_AdsConvertOemToAnsi
extern UNSIGNED32 ENTRYPOINT AdsConvertOemToAnsi(UNSIGNED8* pucBuf, UNSIGNED32* pulLen);
#undef AdsConvertOemToAnsi
#pragma comment(linker, "/alternatename:_oadsimpl_AdsConvertOemToAnsi=_AdsConvertOemToAnsi")
#pragma comment(linker, "/export:AdsConvertOemToAnsi=_AdsConvertOemToAnsi")
__declspec(dllexport) UNSIGNED32 __stdcall AdsConvertOemToAnsi(UNSIGNED8* a0, UNSIGNED32* a1) {
    UNSIGNED32 rc = oadsimpl_AdsConvertOemToAnsi(a0, a1);
    arc_log_rc("AdsConvertOemToAnsi", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsConvertTable ---- */
#define AdsConvertTable oadsimpl_AdsConvertTable
extern UNSIGNED32 ENTRYPOINT AdsConvertTable(ADSHANDLE hHandle, UNSIGNED16 usFilterOption, UNSIGNED8* pucFile, UNSIGNED16 usTargetType);
#undef AdsConvertTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsConvertTable=_AdsConvertTable")
#pragma comment(linker, "/export:AdsConvertTable=_AdsConvertTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsConvertTable(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED8* a2, UNSIGNED16 a3) {
    UNSIGNED32 rc = oadsimpl_AdsConvertTable(a0, a1, a2, a3);
    arc_log_rc("AdsConvertTable", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsCopyTable ---- */
#define AdsCopyTable oadsimpl_AdsCopyTable
extern UNSIGNED32 ENTRYPOINT AdsCopyTable(ADSHANDLE hHandle, UNSIGNED16 usFilterOption, UNSIGNED8* pucFile);
#undef AdsCopyTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCopyTable=_AdsCopyTable")
#pragma comment(linker, "/export:AdsCopyTable=_AdsCopyTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCopyTable(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsCopyTable(a0, a1, a2);
    arc_log_rc("AdsCopyTable", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsCopyTableContent ---- */
#define AdsCopyTableContent oadsimpl_AdsCopyTableContent
extern UNSIGNED32 ENTRYPOINT AdsCopyTableContent(ADSHANDLE hSrc, ADSHANDLE hDst);
#undef AdsCopyTableContent
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCopyTableContent=_AdsCopyTableContent")
#pragma comment(linker, "/export:AdsCopyTableContent=_AdsCopyTableContent")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCopyTableContent(ADSHANDLE a0, ADSHANDLE a1) {
    UNSIGNED32 rc = oadsimpl_AdsCopyTableContent(a0, a1);
    arc_log_rc("AdsCopyTableContent", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsCopyTableContents ---- */
#define AdsCopyTableContents oadsimpl_AdsCopyTableContents
extern UNSIGNED32 ENTRYPOINT AdsCopyTableContents(ADSHANDLE hSrc, ADSHANDLE hDst, UNSIGNED16 usFilterOption);
#undef AdsCopyTableContents
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCopyTableContents=_AdsCopyTableContents")
#pragma comment(linker, "/export:AdsCopyTableContents=_AdsCopyTableContents")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCopyTableContents(ADSHANDLE a0, ADSHANDLE a1, UNSIGNED16 a2) {
    UNSIGNED32 rc = oadsimpl_AdsCopyTableContents(a0, a1, a2);
    arc_log_rc("AdsCopyTableContents", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsCopyTableStructure ---- */
#define AdsCopyTableStructure oadsimpl_AdsCopyTableStructure
extern UNSIGNED32 ENTRYPOINT AdsCopyTableStructure(ADSHANDLE hTable, UNSIGNED8* pucFile);
#undef AdsCopyTableStructure
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCopyTableStructure=_AdsCopyTableStructure")
#pragma comment(linker, "/export:AdsCopyTableStructure=_AdsCopyTableStructure")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCopyTableStructure(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsCopyTableStructure(a0, a1);
    arc_log_rc("AdsCopyTableStructure", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsCreateFTSIndex ---- */
#define AdsCreateFTSIndex oadsimpl_AdsCreateFTSIndex
extern UNSIGNED32 ENTRYPOINT AdsCreateFTSIndex(ADSHANDLE hTable, UNSIGNED8* pucFileName, UNSIGNED8* pucTag, UNSIGNED8* pucField, UNSIGNED32 ulPageSize, UNSIGNED32 ulMinWordLen, UNSIGNED32 ulMaxWordLen, UNSIGNED16 usUseDefaultDelim, UNSIGNED8* pucDelimiters, UNSIGNED16 usUseDefaultNoise, UNSIGNED8* pucNoiseWords, UNSIGNED16 usUseDefaultDrop, UNSIGNED8* pucDropChars, UNSIGNED16 usUseDefaultConditionals, UNSIGNED8* pucConditionalChars, UNSIGNED8* pucReserved1, UNSIGNED8* pucReserved2, UNSIGNED32 ulOptions);
#undef AdsCreateFTSIndex
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCreateFTSIndex=_AdsCreateFTSIndex")
#pragma comment(linker, "/export:AdsCreateFTSIndex=_AdsCreateFTSIndex")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCreateFTSIndex(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED32 a4, UNSIGNED32 a5, UNSIGNED32 a6, UNSIGNED16 a7, UNSIGNED8* a8, UNSIGNED16 a9, UNSIGNED8* a10, UNSIGNED16 a11, UNSIGNED8* a12, UNSIGNED16 a13, UNSIGNED8* a14, UNSIGNED8* a15, UNSIGNED8* a16, UNSIGNED32 a17) {
    UNSIGNED32 rc = oadsimpl_AdsCreateFTSIndex(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17);
    arc_log_rc("AdsCreateFTSIndex", rc, 18, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7, (uintptr_t)a8, (uintptr_t)a9, (uintptr_t)a10, (uintptr_t)a11, (uintptr_t)a12, (uintptr_t)a13, (uintptr_t)a14, (uintptr_t)a15, (uintptr_t)a16, (uintptr_t)a17);
    return rc;
}

/* ---- AdsCreateIndex ---- */
#define AdsCreateIndex oadsimpl_AdsCreateIndex
extern UNSIGNED32 ENTRYPOINT AdsCreateIndex(ADSHANDLE hTable, UNSIGNED8* pucFile, UNSIGNED8* pucTag, UNSIGNED8* pucExpr, UNSIGNED8* pucCondition, UNSIGNED32 ulOptions, UNSIGNED16 usKeyType, ADSHANDLE* phIndex);
#undef AdsCreateIndex
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCreateIndex=_AdsCreateIndex")
#pragma comment(linker, "/export:AdsCreateIndex=_AdsCreateIndex")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCreateIndex(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED32 a5, UNSIGNED16 a6, ADSHANDLE* a7) {
    UNSIGNED32 rc = oadsimpl_AdsCreateIndex(a0, a1, a2, a3, a4, a5, a6, a7);
    arc_log_rc("AdsCreateIndex", rc, 8, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7);
    return rc;
}

/* ---- AdsCreateIndex61 ---- */
#define AdsCreateIndex61 oadsimpl_AdsCreateIndex61
extern UNSIGNED32 ENTRYPOINT AdsCreateIndex61(ADSHANDLE hTable, UNSIGNED8* pucFileName, UNSIGNED8* pucIndexName, UNSIGNED8* pucExpr, UNSIGNED8* pucCondition, UNSIGNED8* pucKeyFilter, UNSIGNED32 ulOptions, UNSIGNED16 usPageSize, ADSHANDLE* phIndex);
#undef AdsCreateIndex61
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCreateIndex61=_AdsCreateIndex61")
#pragma comment(linker, "/export:AdsCreateIndex61=_AdsCreateIndex61")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCreateIndex61(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5, UNSIGNED32 a6, UNSIGNED16 a7, ADSHANDLE* a8) {
    UNSIGNED32 rc = oadsimpl_AdsCreateIndex61(a0, a1, a2, a3, a4, a5, a6, a7, a8);
    arc_log_rc("AdsCreateIndex61", rc, 9, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7, (uintptr_t)a8);
    return rc;
}

/* ---- AdsCreateSQLStatement ---- */
#define AdsCreateSQLStatement oadsimpl_AdsCreateSQLStatement
extern UNSIGNED32 ENTRYPOINT AdsCreateSQLStatement(ADSHANDLE hConnect, ADSHANDLE* phStatement);
#undef AdsCreateSQLStatement
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCreateSQLStatement=_AdsCreateSQLStatement")
#pragma comment(linker, "/export:AdsCreateSQLStatement=_AdsCreateSQLStatement")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCreateSQLStatement(ADSHANDLE a0, ADSHANDLE* a1) {
    UNSIGNED32 rc = oadsimpl_AdsCreateSQLStatement(a0, a1);
    arc_log_rc("AdsCreateSQLStatement", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsCreateSavepoint ---- */
#define AdsCreateSavepoint oadsimpl_AdsCreateSavepoint
extern UNSIGNED32 ENTRYPOINT AdsCreateSavepoint(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED32 ulOptions);
#undef AdsCreateSavepoint
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCreateSavepoint=_AdsCreateSavepoint")
#pragma comment(linker, "/export:AdsCreateSavepoint=_AdsCreateSavepoint")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCreateSavepoint(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32 a2) {
    UNSIGNED32 rc = oadsimpl_AdsCreateSavepoint(a0, a1, a2);
    arc_log_rc("AdsCreateSavepoint", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsCreateTable ---- */
#define AdsCreateTable oadsimpl_AdsCreateTable
extern UNSIGNED32 ENTRYPOINT AdsCreateTable(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucAlias, UNSIGNED16 usTableType, UNSIGNED16 usCharType, UNSIGNED16 usLockType, UNSIGNED16 usCheckRights, UNSIGNED16 usMemoBlockSize, UNSIGNED8* pucFields, ADSHANDLE* phTable);
#undef AdsCreateTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCreateTable=_AdsCreateTable")
#pragma comment(linker, "/export:AdsCreateTable=_AdsCreateTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCreateTable(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, UNSIGNED16 a4, UNSIGNED16 a5, UNSIGNED16 a6, UNSIGNED16 a7, UNSIGNED8* a8, ADSHANDLE* a9) {
    UNSIGNED32 rc = oadsimpl_AdsCreateTable(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9);
    arc_log_rc("AdsCreateTable", rc, 10, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7, (uintptr_t)a8, (uintptr_t)a9);
    return rc;
}

/* ---- AdsCustomizeAOF ---- */
#define AdsCustomizeAOF oadsimpl_AdsCustomizeAOF
extern UNSIGNED32 ENTRYPOINT AdsCustomizeAOF(ADSHANDLE hTable, UNSIGNED32 ulNumRecords, UNSIGNED32* pulRecords, UNSIGNED16 usOption);
#undef AdsCustomizeAOF
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCustomizeAOF=_AdsCustomizeAOF")
#pragma comment(linker, "/export:AdsCustomizeAOF=_AdsCustomizeAOF")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCustomizeAOF(ADSHANDLE a0, UNSIGNED32 a1, UNSIGNED32* a2, UNSIGNED16 a3) {
    UNSIGNED32 rc = oadsimpl_AdsCustomizeAOF(a0, a1, a2, a3);
    arc_log_rc("AdsCustomizeAOF", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsDDAddIndexFile ---- */
#define AdsDDAddIndexFile oadsimpl_AdsDDAddIndexFile
extern UNSIGNED32 ENTRYPOINT AdsDDAddIndexFile(ADSHANDLE hConnect, UNSIGNED8* pucTable, UNSIGNED8* pucIndex, UNSIGNED8* pucComment);
#undef AdsDDAddIndexFile
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDAddIndexFile=_AdsDDAddIndexFile")
#pragma comment(linker, "/export:AdsDDAddIndexFile=_AdsDDAddIndexFile")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDAddIndexFile(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsDDAddIndexFile(a0, a1, a2, a3);
    arc_log_rc("AdsDDAddIndexFile", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsDDAddProcedure ---- */
#define AdsDDAddProcedure oadsimpl_AdsDDAddProcedure
extern UNSIGNED32 ENTRYPOINT AdsDDAddProcedure(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucContainer, UNSIGNED8* pucProcName, UNSIGNED32 ulInvokeOption, UNSIGNED8* pucInParams, UNSIGNED8* pucOutParams, UNSIGNED8* pucComments);
#undef AdsDDAddProcedure
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDAddProcedure=_AdsDDAddProcedure")
#pragma comment(linker, "/export:AdsDDAddProcedure=_AdsDDAddProcedure")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDAddProcedure(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED32 a4, UNSIGNED8* a5, UNSIGNED8* a6, UNSIGNED8* a7) {
    UNSIGNED32 rc = oadsimpl_AdsDDAddProcedure(a0, a1, a2, a3, a4, a5, a6, a7);
    arc_log_rc("AdsDDAddProcedure", rc, 8, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7);
    return rc;
}

/* ---- AdsDDAddTable ---- */
#define AdsDDAddTable oadsimpl_AdsDDAddTable
extern UNSIGNED32 ENTRYPOINT AdsDDAddTable(ADSHANDLE hConnect, UNSIGNED8* pucAlias, UNSIGNED8* pucTablePath, UNSIGNED16 usFileType, UNSIGNED16 usCharType, UNSIGNED8* pucIndexPath, UNSIGNED8* pucComment);
#undef AdsDDAddTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDAddTable=_AdsDDAddTable")
#pragma comment(linker, "/export:AdsDDAddTable=_AdsDDAddTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDAddTable(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, UNSIGNED16 a4, UNSIGNED8* a5, UNSIGNED8* a6) {
    UNSIGNED32 rc = oadsimpl_AdsDDAddTable(a0, a1, a2, a3, a4, a5, a6);
    arc_log_rc("AdsDDAddTable", rc, 7, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6);
    return rc;
}

/* ---- AdsDDAddTable90 ---- */
#define AdsDDAddTable90 oadsimpl_AdsDDAddTable90
extern UNSIGNED32 ENTRYPOINT AdsDDAddTable90(ADSHANDLE hConnect, UNSIGNED8* pucAlias, UNSIGNED8* pucTablePath, UNSIGNED16 usTableType, UNSIGNED16 usCharType, UNSIGNED8* pucIndexPath, UNSIGNED8* pucComment, UNSIGNED8* pucCollation);
#undef AdsDDAddTable90
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDAddTable90=_AdsDDAddTable90")
#pragma comment(linker, "/export:AdsDDAddTable90=_AdsDDAddTable90")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDAddTable90(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, UNSIGNED16 a4, UNSIGNED8* a5, UNSIGNED8* a6, UNSIGNED8* a7) {
    UNSIGNED32 rc = oadsimpl_AdsDDAddTable90(a0, a1, a2, a3, a4, a5, a6, a7);
    arc_log_rc("AdsDDAddTable90", rc, 8, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7);
    return rc;
}

/* ---- AdsDDAddUserToGroup ---- */
#define AdsDDAddUserToGroup oadsimpl_AdsDDAddUserToGroup
extern UNSIGNED32 ENTRYPOINT AdsDDAddUserToGroup(ADSHANDLE hConnect, UNSIGNED8* pucGroup, UNSIGNED8* pucUser);
#undef AdsDDAddUserToGroup
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDAddUserToGroup=_AdsDDAddUserToGroup")
#pragma comment(linker, "/export:AdsDDAddUserToGroup=_AdsDDAddUserToGroup")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDAddUserToGroup(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsDDAddUserToGroup(a0, a1, a2);
    arc_log_rc("AdsDDAddUserToGroup", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsDDCreate ---- */
#define AdsDDCreate oadsimpl_AdsDDCreate
extern UNSIGNED32 ENTRYPOINT AdsDDCreate(UNSIGNED8* pucDictionary, UNSIGNED16 bEncrypt, UNSIGNED8* pucAdminPassword, ADSHANDLE* phConnect);
#undef AdsDDCreate
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreate=_AdsDDCreate")
#pragma comment(linker, "/export:AdsDDCreate=_AdsDDCreate")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreate(UNSIGNED8* a0, UNSIGNED16 a1, UNSIGNED8* a2, ADSHANDLE* a3) {
    UNSIGNED32 rc = oadsimpl_AdsDDCreate(a0, a1, a2, a3);
    arc_log_rc("AdsDDCreate", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsDDCreateLink ---- */
#define AdsDDCreateLink oadsimpl_AdsDDCreateLink
extern UNSIGNED32 ENTRYPOINT AdsDDCreateLink(ADSHANDLE hConnect, UNSIGNED8* pucLink, UNSIGNED8* pucPath, UNSIGNED8* pucUser, UNSIGNED8* pucPwd, UNSIGNED16 usOptions);
#undef AdsDDCreateLink
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreateLink=_AdsDDCreateLink")
#pragma comment(linker, "/export:AdsDDCreateLink=_AdsDDCreateLink")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreateLink(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED16 a5) {
    UNSIGNED32 rc = oadsimpl_AdsDDCreateLink(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsDDCreateLink", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsDDCreateProcedure ---- */
#define AdsDDCreateProcedure oadsimpl_AdsDDCreateProcedure
extern UNSIGNED32 ENTRYPOINT AdsDDCreateProcedure(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucContainer, UNSIGNED8* pucProcName, UNSIGNED32 ulInvokeOption, UNSIGNED8* pucInParams, UNSIGNED8* pucOutParams, UNSIGNED8* pucComments);
#undef AdsDDCreateProcedure
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreateProcedure=_AdsDDCreateProcedure")
#pragma comment(linker, "/export:AdsDDCreateProcedure=_AdsDDCreateProcedure")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreateProcedure(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED32 a4, UNSIGNED8* a5, UNSIGNED8* a6, UNSIGNED8* a7) {
    UNSIGNED32 rc = oadsimpl_AdsDDCreateProcedure(a0, a1, a2, a3, a4, a5, a6, a7);
    arc_log_rc("AdsDDCreateProcedure", rc, 8, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7);
    return rc;
}

/* ---- AdsDDCreateRefIntegrity ---- */
#define AdsDDCreateRefIntegrity oadsimpl_AdsDDCreateRefIntegrity
extern UNSIGNED32 ENTRYPOINT AdsDDCreateRefIntegrity(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucFail, UNSIGNED8* pucParent, UNSIGNED8* pucParentTag, UNSIGNED8* pucChild, UNSIGNED8* pucChildTag, UNSIGNED16 usUpdate, UNSIGNED16 usDelete);
#undef AdsDDCreateRefIntegrity
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreateRefIntegrity=_AdsDDCreateRefIntegrity")
#pragma comment(linker, "/export:AdsDDCreateRefIntegrity=_AdsDDCreateRefIntegrity")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreateRefIntegrity(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5, UNSIGNED8* a6, UNSIGNED16 a7, UNSIGNED16 a8) {
    UNSIGNED32 rc = oadsimpl_AdsDDCreateRefIntegrity(a0, a1, a2, a3, a4, a5, a6, a7, a8);
    arc_log_rc("AdsDDCreateRefIntegrity", rc, 9, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7, (uintptr_t)a8);
    return rc;
}

/* ---- AdsDDCreateUser ---- */
#define AdsDDCreateUser oadsimpl_AdsDDCreateUser
extern UNSIGNED32 ENTRYPOINT AdsDDCreateUser(ADSHANDLE hConnect, UNSIGNED8* pucGroup, UNSIGNED8* pucUser, UNSIGNED8* pucPwd, UNSIGNED8* pucComment);
#undef AdsDDCreateUser
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreateUser=_AdsDDCreateUser")
#pragma comment(linker, "/export:AdsDDCreateUser=_AdsDDCreateUser")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreateUser(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDCreateUser(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDCreateUser", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDCreateView ---- */
#define AdsDDCreateView oadsimpl_AdsDDCreateView
extern UNSIGNED32 ENTRYPOINT AdsDDCreateView(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucComments, UNSIGNED8* pucSQL);
#undef AdsDDCreateView
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreateView=_AdsDDCreateView")
#pragma comment(linker, "/export:AdsDDCreateView=_AdsDDCreateView")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreateView(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsDDCreateView(a0, a1, a2, a3);
    arc_log_rc("AdsDDCreateView", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsDDDeleteUser ---- */
#define AdsDDDeleteUser oadsimpl_AdsDDDeleteUser
extern UNSIGNED32 ENTRYPOINT AdsDDDeleteUser(ADSHANDLE hConnect, UNSIGNED8* pucUser);
#undef AdsDDDeleteUser
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDDeleteUser=_AdsDDDeleteUser")
#pragma comment(linker, "/export:AdsDDDeleteUser=_AdsDDDeleteUser")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDDeleteUser(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsDDDeleteUser(a0, a1);
    arc_log_rc("AdsDDDeleteUser", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsDDDropFunction ---- */
#define AdsDDDropFunction oadsimpl_AdsDDDropFunction
extern UNSIGNED32 ENTRYPOINT AdsDDDropFunction(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsDDDropFunction
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDDropFunction=_AdsDDDropFunction")
#pragma comment(linker, "/export:AdsDDDropFunction=_AdsDDDropFunction")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDDropFunction(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsDDDropFunction(a0, a1);
    arc_log_rc("AdsDDDropFunction", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsDDDropLink ---- */
#define AdsDDDropLink oadsimpl_AdsDDDropLink
extern UNSIGNED32 ENTRYPOINT AdsDDDropLink(ADSHANDLE hConnect, UNSIGNED8* pucLink, UNSIGNED16 usOptions);
#undef AdsDDDropLink
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDDropLink=_AdsDDDropLink")
#pragma comment(linker, "/export:AdsDDDropLink=_AdsDDDropLink")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDDropLink(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2) {
    UNSIGNED32 rc = oadsimpl_AdsDDDropLink(a0, a1, a2);
    arc_log_rc("AdsDDDropLink", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsDDDropProcedure ---- */
#define AdsDDDropProcedure oadsimpl_AdsDDDropProcedure
extern UNSIGNED32 ENTRYPOINT AdsDDDropProcedure(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsDDDropProcedure
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDDropProcedure=_AdsDDDropProcedure")
#pragma comment(linker, "/export:AdsDDDropProcedure=_AdsDDDropProcedure")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDDropProcedure(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsDDDropProcedure(a0, a1);
    arc_log_rc("AdsDDDropProcedure", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsDDDropTrigger ---- */
#define AdsDDDropTrigger oadsimpl_AdsDDDropTrigger
extern UNSIGNED32 ENTRYPOINT AdsDDDropTrigger(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsDDDropTrigger
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDDropTrigger=_AdsDDDropTrigger")
#pragma comment(linker, "/export:AdsDDDropTrigger=_AdsDDDropTrigger")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDDropTrigger(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsDDDropTrigger(a0, a1);
    arc_log_rc("AdsDDDropTrigger", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsDDDropView ---- */
#define AdsDDDropView oadsimpl_AdsDDDropView
extern UNSIGNED32 ENTRYPOINT AdsDDDropView(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsDDDropView
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDDropView=_AdsDDDropView")
#pragma comment(linker, "/export:AdsDDDropView=_AdsDDDropView")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDDropView(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsDDDropView(a0, a1);
    arc_log_rc("AdsDDDropView", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsDDFindClose ---- */
#define AdsDDFindClose oadsimpl_AdsDDFindClose
extern UNSIGNED32 ENTRYPOINT AdsDDFindClose(ADSHANDLE hObject, ADSHANDLE hFindHandle);
#undef AdsDDFindClose
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDFindClose=_AdsDDFindClose")
#pragma comment(linker, "/export:AdsDDFindClose=_AdsDDFindClose")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDFindClose(ADSHANDLE a0, ADSHANDLE a1) {
    UNSIGNED32 rc = oadsimpl_AdsDDFindClose(a0, a1);
    arc_log_rc("AdsDDFindClose", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsDDFindFirstObject ---- */
#define AdsDDFindFirstObject oadsimpl_AdsDDFindFirstObject
extern UNSIGNED32 ENTRYPOINT AdsDDFindFirstObject(ADSHANDLE hObject, UNSIGNED16 usFindObjectType, UNSIGNED8* pucParentName, UNSIGNED8* pucObjectName, UNSIGNED16* pusObjectNameLen, ADSHANDLE* phFindHandle);
#undef AdsDDFindFirstObject
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDFindFirstObject=_AdsDDFindFirstObject")
#pragma comment(linker, "/export:AdsDDFindFirstObject=_AdsDDFindFirstObject")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDFindFirstObject(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED16* a4, ADSHANDLE* a5) {
    UNSIGNED32 rc = oadsimpl_AdsDDFindFirstObject(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsDDFindFirstObject", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsDDFindNextObject ---- */
#define AdsDDFindNextObject oadsimpl_AdsDDFindNextObject
extern UNSIGNED32 ENTRYPOINT AdsDDFindNextObject(ADSHANDLE hObject, ADSHANDLE hFindHandle, UNSIGNED8* pucObjectName, UNSIGNED16* pusObjectNameLen);
#undef AdsDDFindNextObject
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDFindNextObject=_AdsDDFindNextObject")
#pragma comment(linker, "/export:AdsDDFindNextObject=_AdsDDFindNextObject")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDFindNextObject(ADSHANDLE a0, ADSHANDLE a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    UNSIGNED32 rc = oadsimpl_AdsDDFindNextObject(a0, a1, a2, a3);
    arc_log_rc("AdsDDFindNextObject", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsDDGetDatabaseProperty ---- */
#define AdsDDGetDatabaseProperty oadsimpl_AdsDDGetDatabaseProperty
extern UNSIGNED32 ENTRYPOINT AdsDDGetDatabaseProperty(ADSHANDLE hConnect, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16* pusLen);
#undef AdsDDGetDatabaseProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetDatabaseProperty=_AdsDDGetDatabaseProperty")
#pragma comment(linker, "/export:AdsDDGetDatabaseProperty=_AdsDDGetDatabaseProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetDatabaseProperty(ADSHANDLE a0, UNSIGNED16 a1, void* a2, UNSIGNED16* a3) {
    UNSIGNED32 rc = oadsimpl_AdsDDGetDatabaseProperty(a0, a1, a2, a3);
    arc_log_rc("AdsDDGetDatabaseProperty", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsDDGetFieldProperty ---- */
#define AdsDDGetFieldProperty oadsimpl_AdsDDGetFieldProperty
extern UNSIGNED32 ENTRYPOINT AdsDDGetFieldProperty(ADSHANDLE hConnect, UNSIGNED8* pucTable, UNSIGNED8* pucField, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16* pusLen);
#undef AdsDDGetFieldProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetFieldProperty=_AdsDDGetFieldProperty")
#pragma comment(linker, "/export:AdsDDGetFieldProperty=_AdsDDGetFieldProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetFieldProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, void* a4, UNSIGNED16* a5) {
    UNSIGNED32 rc = oadsimpl_AdsDDGetFieldProperty(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsDDGetFieldProperty", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsDDGetFunctionProperty ---- */
#define AdsDDGetFunctionProperty oadsimpl_AdsDDGetFunctionProperty
extern UNSIGNED32 ENTRYPOINT AdsDDGetFunctionProperty(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usPropertyID, void* pvProperty, UNSIGNED16* pusPropertyLen);
#undef AdsDDGetFunctionProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetFunctionProperty=_AdsDDGetFunctionProperty")
#pragma comment(linker, "/export:AdsDDGetFunctionProperty=_AdsDDGetFunctionProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetFunctionProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16* a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDGetFunctionProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDGetFunctionProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDGetIndexProperty ---- */
#define AdsDDGetIndexProperty oadsimpl_AdsDDGetIndexProperty
extern UNSIGNED32 ENTRYPOINT AdsDDGetIndexProperty(ADSHANDLE hConnect, UNSIGNED8* pucTable, UNSIGNED8* pucIndex, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16* pusLen);
#undef AdsDDGetIndexProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetIndexProperty=_AdsDDGetIndexProperty")
#pragma comment(linker, "/export:AdsDDGetIndexProperty=_AdsDDGetIndexProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetIndexProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, void* a4, UNSIGNED16* a5) {
    UNSIGNED32 rc = oadsimpl_AdsDDGetIndexProperty(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsDDGetIndexProperty", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsDDGetProcProperty ---- */
#define AdsDDGetProcProperty oadsimpl_AdsDDGetProcProperty
extern UNSIGNED32 ENTRYPOINT AdsDDGetProcProperty(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16* pusLen);
#undef AdsDDGetProcProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetProcProperty=_AdsDDGetProcProperty")
#pragma comment(linker, "/export:AdsDDGetProcProperty=_AdsDDGetProcProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetProcProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16* a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDGetProcProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDGetProcProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDGetProcedureProperty ---- */
#define AdsDDGetProcedureProperty oadsimpl_AdsDDGetProcedureProperty
extern UNSIGNED32 ENTRYPOINT AdsDDGetProcedureProperty(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16* pusLen);
#undef AdsDDGetProcedureProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetProcedureProperty=_AdsDDGetProcedureProperty")
#pragma comment(linker, "/export:AdsDDGetProcedureProperty=_AdsDDGetProcedureProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetProcedureProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16* a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDGetProcedureProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDGetProcedureProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDGetRefIntegrityProperty ---- */
#define AdsDDGetRefIntegrityProperty oadsimpl_AdsDDGetRefIntegrityProperty
extern UNSIGNED32 ENTRYPOINT AdsDDGetRefIntegrityProperty(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16* pusLen);
#undef AdsDDGetRefIntegrityProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetRefIntegrityProperty=_AdsDDGetRefIntegrityProperty")
#pragma comment(linker, "/export:AdsDDGetRefIntegrityProperty=_AdsDDGetRefIntegrityProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetRefIntegrityProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16* a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDGetRefIntegrityProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDGetRefIntegrityProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDGetUserProperty ---- */
#define AdsDDGetUserProperty oadsimpl_AdsDDGetUserProperty
extern UNSIGNED32 ENTRYPOINT AdsDDGetUserProperty(ADSHANDLE hConnect, UNSIGNED8* pucUser, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16* pusLen);
#undef AdsDDGetUserProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetUserProperty=_AdsDDGetUserProperty")
#pragma comment(linker, "/export:AdsDDGetUserProperty=_AdsDDGetUserProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetUserProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16* a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDGetUserProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDGetUserProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDGetUserTableRights ---- */
#define AdsDDGetUserTableRights oadsimpl_AdsDDGetUserTableRights
extern UNSIGNED32 ENTRYPOINT AdsDDGetUserTableRights(ADSHANDLE hConnect, UNSIGNED8* pucTable, UNSIGNED8* pucUser, UNSIGNED32* pulLevel);
#undef AdsDDGetUserTableRights
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetUserTableRights=_AdsDDGetUserTableRights")
#pragma comment(linker, "/export:AdsDDGetUserTableRights=_AdsDDGetUserTableRights")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetUserTableRights(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32* a3) {
    UNSIGNED32 rc = oadsimpl_AdsDDGetUserTableRights(a0, a1, a2, a3);
    arc_log_rc("AdsDDGetUserTableRights", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsDDModifyLink ---- */
#define AdsDDModifyLink oadsimpl_AdsDDModifyLink
extern UNSIGNED32 ENTRYPOINT AdsDDModifyLink(ADSHANDLE hConnect, UNSIGNED8* pucLink, UNSIGNED8* pucPath, UNSIGNED8* pucUser, UNSIGNED8* pucPwd, UNSIGNED16 usOptions);
#undef AdsDDModifyLink
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDModifyLink=_AdsDDModifyLink")
#pragma comment(linker, "/export:AdsDDModifyLink=_AdsDDModifyLink")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDModifyLink(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED16 a5) {
    UNSIGNED32 rc = oadsimpl_AdsDDModifyLink(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsDDModifyLink", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsDDRemoveIndexFile ---- */
#define AdsDDRemoveIndexFile oadsimpl_AdsDDRemoveIndexFile
extern UNSIGNED32 ENTRYPOINT AdsDDRemoveIndexFile(ADSHANDLE hConnect, UNSIGNED8* pucTable, UNSIGNED8* pucIndex, UNSIGNED16 usOptions);
#undef AdsDDRemoveIndexFile
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDRemoveIndexFile=_AdsDDRemoveIndexFile")
#pragma comment(linker, "/export:AdsDDRemoveIndexFile=_AdsDDRemoveIndexFile")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDRemoveIndexFile(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3) {
    UNSIGNED32 rc = oadsimpl_AdsDDRemoveIndexFile(a0, a1, a2, a3);
    arc_log_rc("AdsDDRemoveIndexFile", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsDDRemoveProcedure ---- */
#define AdsDDRemoveProcedure oadsimpl_AdsDDRemoveProcedure
extern UNSIGNED32 ENTRYPOINT AdsDDRemoveProcedure(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsDDRemoveProcedure
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDRemoveProcedure=_AdsDDRemoveProcedure")
#pragma comment(linker, "/export:AdsDDRemoveProcedure=_AdsDDRemoveProcedure")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDRemoveProcedure(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsDDRemoveProcedure(a0, a1);
    arc_log_rc("AdsDDRemoveProcedure", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsDDRemoveRefIntegrity ---- */
#define AdsDDRemoveRefIntegrity oadsimpl_AdsDDRemoveRefIntegrity
extern UNSIGNED32 ENTRYPOINT AdsDDRemoveRefIntegrity(ADSHANDLE hConnect, UNSIGNED8* pucRI);
#undef AdsDDRemoveRefIntegrity
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDRemoveRefIntegrity=_AdsDDRemoveRefIntegrity")
#pragma comment(linker, "/export:AdsDDRemoveRefIntegrity=_AdsDDRemoveRefIntegrity")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDRemoveRefIntegrity(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsDDRemoveRefIntegrity(a0, a1);
    arc_log_rc("AdsDDRemoveRefIntegrity", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsDDRemoveTable ---- */
#define AdsDDRemoveTable oadsimpl_AdsDDRemoveTable
extern UNSIGNED32 ENTRYPOINT AdsDDRemoveTable(ADSHANDLE hConnect, UNSIGNED8* pucAlias, UNSIGNED16 usDeleteFiles);
#undef AdsDDRemoveTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDRemoveTable=_AdsDDRemoveTable")
#pragma comment(linker, "/export:AdsDDRemoveTable=_AdsDDRemoveTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDRemoveTable(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2) {
    UNSIGNED32 rc = oadsimpl_AdsDDRemoveTable(a0, a1, a2);
    arc_log_rc("AdsDDRemoveTable", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsDDRemoveTrigger ---- */
#define AdsDDRemoveTrigger oadsimpl_AdsDDRemoveTrigger
extern UNSIGNED32 ENTRYPOINT AdsDDRemoveTrigger(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsDDRemoveTrigger
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDRemoveTrigger=_AdsDDRemoveTrigger")
#pragma comment(linker, "/export:AdsDDRemoveTrigger=_AdsDDRemoveTrigger")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDRemoveTrigger(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsDDRemoveTrigger(a0, a1);
    arc_log_rc("AdsDDRemoveTrigger", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsDDRemoveUserFromGroup ---- */
#define AdsDDRemoveUserFromGroup oadsimpl_AdsDDRemoveUserFromGroup
extern UNSIGNED32 ENTRYPOINT AdsDDRemoveUserFromGroup(ADSHANDLE hConnect, UNSIGNED8* pucGroup, UNSIGNED8* pucUser);
#undef AdsDDRemoveUserFromGroup
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDRemoveUserFromGroup=_AdsDDRemoveUserFromGroup")
#pragma comment(linker, "/export:AdsDDRemoveUserFromGroup=_AdsDDRemoveUserFromGroup")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDRemoveUserFromGroup(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsDDRemoveUserFromGroup(a0, a1, a2);
    arc_log_rc("AdsDDRemoveUserFromGroup", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsDDSetDatabaseProperty ---- */
#define AdsDDSetDatabaseProperty oadsimpl_AdsDDSetDatabaseProperty
extern UNSIGNED32 ENTRYPOINT AdsDDSetDatabaseProperty(ADSHANDLE hConnect, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16 usLen);
#undef AdsDDSetDatabaseProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetDatabaseProperty=_AdsDDSetDatabaseProperty")
#pragma comment(linker, "/export:AdsDDSetDatabaseProperty=_AdsDDSetDatabaseProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetDatabaseProperty(ADSHANDLE a0, UNSIGNED16 a1, void* a2, UNSIGNED16 a3) {
    UNSIGNED32 rc = oadsimpl_AdsDDSetDatabaseProperty(a0, a1, a2, a3);
    arc_log_rc("AdsDDSetDatabaseProperty", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsDDSetFieldProperty ---- */
#define AdsDDSetFieldProperty oadsimpl_AdsDDSetFieldProperty
extern UNSIGNED32 AdsDDSetFieldProperty(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3, UNSIGNED8* h4, UNSIGNED8* h5);
#undef AdsDDSetFieldProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetFieldProperty=_AdsDDSetFieldProperty")
#pragma comment(linker, "/export:AdsDDSetFieldProperty=_AdsDDSetFieldProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetFieldProperty(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5, UNSIGNED8* a6, UNSIGNED8* a7) {
    UNSIGNED32 rc = oadsimpl_AdsDDSetFieldProperty(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsDDSetFieldProperty", rc, 8, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7);
    return rc;
}

/* ---- AdsDDSetFunctionProperty ---- */
#define AdsDDSetFunctionProperty oadsimpl_AdsDDSetFunctionProperty
extern UNSIGNED32 ENTRYPOINT AdsDDSetFunctionProperty(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usPropertyID, void* pvProperty, UNSIGNED16 usPropertyLen);
#undef AdsDDSetFunctionProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetFunctionProperty=_AdsDDSetFunctionProperty")
#pragma comment(linker, "/export:AdsDDSetFunctionProperty=_AdsDDSetFunctionProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetFunctionProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16 a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDSetFunctionProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDSetFunctionProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDSetIndexProperty ---- */
#define AdsDDSetIndexProperty oadsimpl_AdsDDSetIndexProperty
extern UNSIGNED32 ENTRYPOINT AdsDDSetIndexProperty(ADSHANDLE hConnect, UNSIGNED8* pucTable, UNSIGNED8* pucIndex, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16 usLen);
#undef AdsDDSetIndexProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetIndexProperty=_AdsDDSetIndexProperty")
#pragma comment(linker, "/export:AdsDDSetIndexProperty=_AdsDDSetIndexProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetIndexProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, void* a4, UNSIGNED16 a5) {
    UNSIGNED32 rc = oadsimpl_AdsDDSetIndexProperty(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsDDSetIndexProperty", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsDDSetProcProperty ---- */
#define AdsDDSetProcProperty oadsimpl_AdsDDSetProcProperty
extern UNSIGNED32 ENTRYPOINT AdsDDSetProcProperty(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16 usLen);
#undef AdsDDSetProcProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetProcProperty=_AdsDDSetProcProperty")
#pragma comment(linker, "/export:AdsDDSetProcProperty=_AdsDDSetProcProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetProcProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16 a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDSetProcProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDSetProcProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDSetProcedureProperty ---- */
#define AdsDDSetProcedureProperty oadsimpl_AdsDDSetProcedureProperty
extern UNSIGNED32 ENTRYPOINT AdsDDSetProcedureProperty(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16 usLen);
#undef AdsDDSetProcedureProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetProcedureProperty=_AdsDDSetProcedureProperty")
#pragma comment(linker, "/export:AdsDDSetProcedureProperty=_AdsDDSetProcedureProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetProcedureProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16 a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDSetProcedureProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDSetProcedureProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDSetRefIntegrityProperty ---- */
#define AdsDDSetRefIntegrityProperty oadsimpl_AdsDDSetRefIntegrityProperty
extern UNSIGNED32 ENTRYPOINT AdsDDSetRefIntegrityProperty(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16 usLen);
#undef AdsDDSetRefIntegrityProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetRefIntegrityProperty=_AdsDDSetRefIntegrityProperty")
#pragma comment(linker, "/export:AdsDDSetRefIntegrityProperty=_AdsDDSetRefIntegrityProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetRefIntegrityProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16 a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDSetRefIntegrityProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDSetRefIntegrityProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDSetUserTableRights ---- */
#define AdsDDSetUserTableRights oadsimpl_AdsDDSetUserTableRights
extern UNSIGNED32 ENTRYPOINT AdsDDSetUserTableRights(ADSHANDLE hConnect, UNSIGNED8* pucTable, UNSIGNED8* pucUser, UNSIGNED32 ulLevel);
#undef AdsDDSetUserTableRights
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetUserTableRights=_AdsDDSetUserTableRights")
#pragma comment(linker, "/export:AdsDDSetUserTableRights=_AdsDDSetUserTableRights")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetUserTableRights(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32 a3) {
    UNSIGNED32 rc = oadsimpl_AdsDDSetUserTableRights(a0, a1, a2, a3);
    arc_log_rc("AdsDDSetUserTableRights", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsData ---- */
#define AdsData oadsimpl_AdsData
extern UNSIGNED32 ENTRYPOINT AdsData(UNSIGNED16 usFlag, void* pvData);
#undef AdsData
#pragma comment(linker, "/alternatename:_oadsimpl_AdsData=_AdsData")
#pragma comment(linker, "/export:AdsData=_AdsData")
__declspec(dllexport) UNSIGNED32 __stdcall AdsData(UNSIGNED16 a0, void* a1) {
    UNSIGNED32 rc = oadsimpl_AdsData(a0, a1);
    arc_log_rc("AdsData", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsDecryptRecord ---- */
#define AdsDecryptRecord oadsimpl_AdsDecryptRecord
extern UNSIGNED32 ENTRYPOINT AdsDecryptRecord(ADSHANDLE hTable);
#undef AdsDecryptRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDecryptRecord=_AdsDecryptRecord")
#pragma comment(linker, "/export:AdsDecryptRecord=_AdsDecryptRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDecryptRecord(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsDecryptRecord(a0);
    arc_log_rc("AdsDecryptRecord", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsDecryptTable ---- */
#define AdsDecryptTable oadsimpl_AdsDecryptTable
extern UNSIGNED32 ENTRYPOINT AdsDecryptTable(ADSHANDLE hTable);
#undef AdsDecryptTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDecryptTable=_AdsDecryptTable")
#pragma comment(linker, "/export:AdsDecryptTable=_AdsDecryptTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDecryptTable(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsDecryptTable(a0);
    arc_log_rc("AdsDecryptTable", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsDeleteCustomKey ---- */
#define AdsDeleteCustomKey oadsimpl_AdsDeleteCustomKey
extern UNSIGNED32 ENTRYPOINT AdsDeleteCustomKey(ADSHANDLE hIndex);
#undef AdsDeleteCustomKey
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDeleteCustomKey=_AdsDeleteCustomKey")
#pragma comment(linker, "/export:AdsDeleteCustomKey=_AdsDeleteCustomKey")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDeleteCustomKey(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsDeleteCustomKey(a0);
    arc_log_rc("AdsDeleteCustomKey", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsDeleteFile ---- */
#define AdsDeleteFile oadsimpl_AdsDeleteFile
extern UNSIGNED32 ENTRYPOINT AdsDeleteFile(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsDeleteFile
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDeleteFile=_AdsDeleteFile")
#pragma comment(linker, "/export:AdsDeleteFile=_AdsDeleteFile")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDeleteFile(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsDeleteFile(a0, a1);
    arc_log_rc("AdsDeleteFile", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsDeleteIndex ---- */
#define AdsDeleteIndex oadsimpl_AdsDeleteIndex
extern UNSIGNED32 ENTRYPOINT AdsDeleteIndex(ADSHANDLE hIndex);
#undef AdsDeleteIndex
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDeleteIndex=_AdsDeleteIndex")
#pragma comment(linker, "/export:AdsDeleteIndex=_AdsDeleteIndex")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDeleteIndex(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsDeleteIndex(a0);
    arc_log_rc("AdsDeleteIndex", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsDeleteRecord ---- */
#define AdsDeleteRecord oadsimpl_AdsDeleteRecord
extern UNSIGNED32 ENTRYPOINT AdsDeleteRecord(ADSHANDLE hTable);
#undef AdsDeleteRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDeleteRecord=_AdsDeleteRecord")
#pragma comment(linker, "/export:AdsDeleteRecord=_AdsDeleteRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDeleteRecord(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsDeleteRecord(a0);
    arc_log_rc("AdsDeleteRecord", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsDirExist ---- */
#define AdsDirExist oadsimpl_AdsDirExist
extern UNSIGNED32 ENTRYPOINT AdsDirExist(ADSHANDLE hConnect, UNSIGNED8* pucPath, UNSIGNED16* pbExists);
#undef AdsDirExist
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDirExist=_AdsDirExist")
#pragma comment(linker, "/export:AdsDirExist=_AdsDirExist")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDirExist(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsDirExist(a0, a1, a2);
    arc_log_rc("AdsDirExist", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsDirMake ---- */
#define AdsDirMake oadsimpl_AdsDirMake
extern UNSIGNED32 ENTRYPOINT AdsDirMake(ADSHANDLE hConnect, UNSIGNED8* pucPath);
#undef AdsDirMake
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDirMake=_AdsDirMake")
#pragma comment(linker, "/export:AdsDirMake=_AdsDirMake")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDirMake(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsDirMake(a0, a1);
    arc_log_rc("AdsDirMake", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsDirRemove ---- */
#define AdsDirRemove oadsimpl_AdsDirRemove
extern UNSIGNED32 ENTRYPOINT AdsDirRemove(ADSHANDLE hConnect, UNSIGNED8* pucPath);
#undef AdsDirRemove
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDirRemove=_AdsDirRemove")
#pragma comment(linker, "/export:AdsDirRemove=_AdsDirRemove")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDirRemove(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsDirRemove(a0, a1);
    arc_log_rc("AdsDirRemove", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsDirectory ---- */
#define AdsDirectory oadsimpl_AdsDirectory
extern UNSIGNED32 ENTRYPOINT AdsDirectory(ADSHANDLE hConnect, UNSIGNED8* pucMask, UNSIGNED16 usAttr, UNSIGNED8* pucBuffer, UNSIGNED32* pulBufLen);
#undef AdsDirectory
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDirectory=_AdsDirectory")
#pragma comment(linker, "/export:AdsDirectory=_AdsDirectory")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDirectory(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, UNSIGNED8* a3, UNSIGNED32* a4) {
    UNSIGNED32 rc = oadsimpl_AdsDirectory(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDirectory", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDisableAutoIncEnforcement ---- */
#define AdsDisableAutoIncEnforcement oadsimpl_AdsDisableAutoIncEnforcement
extern UNSIGNED32 ENTRYPOINT AdsDisableAutoIncEnforcement(ADSHANDLE hConnect);
#undef AdsDisableAutoIncEnforcement
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDisableAutoIncEnforcement=_AdsDisableAutoIncEnforcement")
#pragma comment(linker, "/export:AdsDisableAutoIncEnforcement=_AdsDisableAutoIncEnforcement")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDisableAutoIncEnforcement(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsDisableAutoIncEnforcement(a0);
    arc_log_rc("AdsDisableAutoIncEnforcement", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsDisableEncryption ---- */
#define AdsDisableEncryption oadsimpl_AdsDisableEncryption
extern UNSIGNED32 ENTRYPOINT AdsDisableEncryption(ADSHANDLE hConnect);
#undef AdsDisableEncryption
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDisableEncryption=_AdsDisableEncryption")
#pragma comment(linker, "/export:AdsDisableEncryption=_AdsDisableEncryption")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDisableEncryption(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsDisableEncryption(a0);
    arc_log_rc("AdsDisableEncryption", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsDisableLocalConnections ---- */
#define AdsDisableLocalConnections oadsimpl_AdsDisableLocalConnections
extern UNSIGNED32 ENTRYPOINT AdsDisableLocalConnections(void);
#undef AdsDisableLocalConnections
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDisableLocalConnections=_AdsDisableLocalConnections")
#pragma comment(linker, "/export:AdsDisableLocalConnections=_AdsDisableLocalConnections")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDisableLocalConnections(void) {
    UNSIGNED32 rc = oadsimpl_AdsDisableLocalConnections();
    arc_log_rc("AdsDisableLocalConnections", rc, 0);
    return rc;
}

/* ---- AdsDisableRI ---- */
#define AdsDisableRI oadsimpl_AdsDisableRI
extern UNSIGNED32 ENTRYPOINT AdsDisableRI(ADSHANDLE hConnect);
#undef AdsDisableRI
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDisableRI=_AdsDisableRI")
#pragma comment(linker, "/export:AdsDisableRI=_AdsDisableRI")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDisableRI(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsDisableRI(a0);
    arc_log_rc("AdsDisableRI", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsDisableUniqueEnforcement ---- */
#define AdsDisableUniqueEnforcement oadsimpl_AdsDisableUniqueEnforcement
extern UNSIGNED32 ENTRYPOINT AdsDisableUniqueEnforcement(ADSHANDLE hConnect);
#undef AdsDisableUniqueEnforcement
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDisableUniqueEnforcement=_AdsDisableUniqueEnforcement")
#pragma comment(linker, "/export:AdsDisableUniqueEnforcement=_AdsDisableUniqueEnforcement")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDisableUniqueEnforcement(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsDisableUniqueEnforcement(a0);
    arc_log_rc("AdsDisableUniqueEnforcement", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsDisconnect ---- */
#define AdsDisconnect oadsimpl_AdsDisconnect
extern UNSIGNED32 ENTRYPOINT AdsDisconnect(ADSHANDLE hConnect);
#undef AdsDisconnect
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDisconnect=_AdsDisconnect")
#pragma comment(linker, "/export:AdsDisconnect=_AdsDisconnect")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDisconnect(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsDisconnect(a0);
    arc_log_rc("AdsDisconnect", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsDropTable ---- */
#define AdsDropTable oadsimpl_AdsDropTable
extern UNSIGNED32 ENTRYPOINT AdsDropTable(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usDeleteFiles);
#undef AdsDropTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDropTable=_AdsDropTable")
#pragma comment(linker, "/export:AdsDropTable=_AdsDropTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDropTable(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2) {
    UNSIGNED32 rc = oadsimpl_AdsDropTable(a0, a1, a2);
    arc_log_rc("AdsDropTable", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsEnableAutoIncEnforcement ---- */
#define AdsEnableAutoIncEnforcement oadsimpl_AdsEnableAutoIncEnforcement
extern UNSIGNED32 ENTRYPOINT AdsEnableAutoIncEnforcement(ADSHANDLE hConnect);
#undef AdsEnableAutoIncEnforcement
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEnableAutoIncEnforcement=_AdsEnableAutoIncEnforcement")
#pragma comment(linker, "/export:AdsEnableAutoIncEnforcement=_AdsEnableAutoIncEnforcement")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEnableAutoIncEnforcement(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsEnableAutoIncEnforcement(a0);
    arc_log_rc("AdsEnableAutoIncEnforcement", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsEnableEncryption ---- */
#define AdsEnableEncryption oadsimpl_AdsEnableEncryption
extern UNSIGNED32 ENTRYPOINT AdsEnableEncryption(ADSHANDLE hConnect, UNSIGNED8* pucPassword);
#undef AdsEnableEncryption
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEnableEncryption=_AdsEnableEncryption")
#pragma comment(linker, "/export:AdsEnableEncryption=_AdsEnableEncryption")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEnableEncryption(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsEnableEncryption(a0, a1);
    arc_log_rc("AdsEnableEncryption", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsEnableRI ---- */
#define AdsEnableRI oadsimpl_AdsEnableRI
extern UNSIGNED32 ENTRYPOINT AdsEnableRI(ADSHANDLE hConnect);
#undef AdsEnableRI
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEnableRI=_AdsEnableRI")
#pragma comment(linker, "/export:AdsEnableRI=_AdsEnableRI")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEnableRI(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsEnableRI(a0);
    arc_log_rc("AdsEnableRI", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsEnableUniqueEnforcement ---- */
#define AdsEnableUniqueEnforcement oadsimpl_AdsEnableUniqueEnforcement
extern UNSIGNED32 ENTRYPOINT AdsEnableUniqueEnforcement(ADSHANDLE hConnect);
#undef AdsEnableUniqueEnforcement
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEnableUniqueEnforcement=_AdsEnableUniqueEnforcement")
#pragma comment(linker, "/export:AdsEnableUniqueEnforcement=_AdsEnableUniqueEnforcement")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEnableUniqueEnforcement(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsEnableUniqueEnforcement(a0);
    arc_log_rc("AdsEnableUniqueEnforcement", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsEncryptRecord ---- */
#define AdsEncryptRecord oadsimpl_AdsEncryptRecord
extern UNSIGNED32 ENTRYPOINT AdsEncryptRecord(ADSHANDLE hTable);
#undef AdsEncryptRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEncryptRecord=_AdsEncryptRecord")
#pragma comment(linker, "/export:AdsEncryptRecord=_AdsEncryptRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEncryptRecord(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsEncryptRecord(a0);
    arc_log_rc("AdsEncryptRecord", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsEncryptTable ---- */
#define AdsEncryptTable oadsimpl_AdsEncryptTable
extern UNSIGNED32 ENTRYPOINT AdsEncryptTable(ADSHANDLE hTable);
#undef AdsEncryptTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEncryptTable=_AdsEncryptTable")
#pragma comment(linker, "/export:AdsEncryptTable=_AdsEncryptTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEncryptTable(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsEncryptTable(a0);
    arc_log_rc("AdsEncryptTable", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsEvalAOF ---- */
#define AdsEvalAOF oadsimpl_AdsEvalAOF
extern UNSIGNED32 ENTRYPOINT AdsEvalAOF(ADSHANDLE hTable, UNSIGNED8* pucExpr, UNSIGNED16* pusOptLevel);
#undef AdsEvalAOF
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEvalAOF=_AdsEvalAOF")
#pragma comment(linker, "/export:AdsEvalAOF=_AdsEvalAOF")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEvalAOF(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsEvalAOF(a0, a1, a2);
    arc_log_rc("AdsEvalAOF", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsEvalLogicalExpr ---- */
#define AdsEvalLogicalExpr oadsimpl_AdsEvalLogicalExpr
extern UNSIGNED32 ENTRYPOINT AdsEvalLogicalExpr(ADSHANDLE hTable, UNSIGNED8* pucExpr, UNSIGNED16* pbResult);
#undef AdsEvalLogicalExpr
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEvalLogicalExpr=_AdsEvalLogicalExpr")
#pragma comment(linker, "/export:AdsEvalLogicalExpr=_AdsEvalLogicalExpr")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEvalLogicalExpr(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsEvalLogicalExpr(a0, a1, a2);
    arc_log_rc("AdsEvalLogicalExpr", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsEvalNumericExpr ---- */
#define AdsEvalNumericExpr oadsimpl_AdsEvalNumericExpr
extern UNSIGNED32 ENTRYPOINT AdsEvalNumericExpr(ADSHANDLE hTable, UNSIGNED8* pucExpr, double* pdResult);
#undef AdsEvalNumericExpr
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEvalNumericExpr=_AdsEvalNumericExpr")
#pragma comment(linker, "/export:AdsEvalNumericExpr=_AdsEvalNumericExpr")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEvalNumericExpr(ADSHANDLE a0, UNSIGNED8* a1, double* a2) {
    UNSIGNED32 rc = oadsimpl_AdsEvalNumericExpr(a0, a1, a2);
    arc_log_rc("AdsEvalNumericExpr", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsEvalStringExpr ---- */
#define AdsEvalStringExpr oadsimpl_AdsEvalStringExpr
extern UNSIGNED32 ENTRYPOINT AdsEvalStringExpr(ADSHANDLE hTable, UNSIGNED8* pucExpr, UNSIGNED8* pucResult, UNSIGNED16* pusLen);
#undef AdsEvalStringExpr
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEvalStringExpr=_AdsEvalStringExpr")
#pragma comment(linker, "/export:AdsEvalStringExpr=_AdsEvalStringExpr")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEvalStringExpr(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    UNSIGNED32 rc = oadsimpl_AdsEvalStringExpr(a0, a1, a2, a3);
    arc_log_rc("AdsEvalStringExpr", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsEvalTestExpr ---- */
#define AdsEvalTestExpr oadsimpl_AdsEvalTestExpr
extern UNSIGNED32 ENTRYPOINT AdsEvalTestExpr(ADSHANDLE hTable, UNSIGNED8* pucExpr, UNSIGNED16* pusType);
#undef AdsEvalTestExpr
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEvalTestExpr=_AdsEvalTestExpr")
#pragma comment(linker, "/export:AdsEvalTestExpr=_AdsEvalTestExpr")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEvalTestExpr(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsEvalTestExpr(a0, a1, a2);
    arc_log_rc("AdsEvalTestExpr", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsExecuteSQL ---- */
#define AdsExecuteSQL oadsimpl_AdsExecuteSQL
extern UNSIGNED32 ENTRYPOINT AdsExecuteSQL(ADSHANDLE hStatement, ADSHANDLE* phCursor);
#undef AdsExecuteSQL
#pragma comment(linker, "/alternatename:_oadsimpl_AdsExecuteSQL=_AdsExecuteSQL")
#pragma comment(linker, "/export:AdsExecuteSQL=_AdsExecuteSQL")
__declspec(dllexport) UNSIGNED32 __stdcall AdsExecuteSQL(ADSHANDLE a0, ADSHANDLE* a1) {
    UNSIGNED32 rc = oadsimpl_AdsExecuteSQL(a0, a1);
    arc_log_rc("AdsExecuteSQL", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsExecuteSQLDirect ---- */
#define AdsExecuteSQLDirect oadsimpl_AdsExecuteSQLDirect
extern UNSIGNED32 ENTRYPOINT AdsExecuteSQLDirect(ADSHANDLE hStatement, UNSIGNED8* pucSQL, ADSHANDLE* phCursor);
#undef AdsExecuteSQLDirect
#pragma comment(linker, "/alternatename:_oadsimpl_AdsExecuteSQLDirect=_AdsExecuteSQLDirect")
#pragma comment(linker, "/export:AdsExecuteSQLDirect=_AdsExecuteSQLDirect")
__declspec(dllexport) UNSIGNED32 __stdcall AdsExecuteSQLDirect(ADSHANDLE a0, UNSIGNED8* a1, ADSHANDLE* a2) {
    UNSIGNED32 rc = oadsimpl_AdsExecuteSQLDirect(a0, a1, a2);
    arc_log_rc("AdsExecuteSQLDirect", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsExecuteSQLDirectW ---- */
#define AdsExecuteSQLDirectW oadsimpl_AdsExecuteSQLDirectW
extern UNSIGNED32 ENTRYPOINT AdsExecuteSQLDirectW(ADSHANDLE hStatement, UNSIGNED16* pwcSQL, ADSHANDLE* phCursor);
#undef AdsExecuteSQLDirectW
#pragma comment(linker, "/alternatename:_oadsimpl_AdsExecuteSQLDirectW=_AdsExecuteSQLDirectW")
#pragma comment(linker, "/export:AdsExecuteSQLDirectW=_AdsExecuteSQLDirectW")
__declspec(dllexport) UNSIGNED32 __stdcall AdsExecuteSQLDirectW(ADSHANDLE a0, UNSIGNED16* a1, ADSHANDLE* a2) {
    UNSIGNED32 rc = oadsimpl_AdsExecuteSQLDirectW(a0, a1, a2);
    arc_log_rc("AdsExecuteSQLDirectW", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsExtractKey ---- */
#define AdsExtractKey oadsimpl_AdsExtractKey
extern UNSIGNED32 ENTRYPOINT AdsExtractKey(ADSHANDLE hIndex, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsExtractKey
#pragma comment(linker, "/alternatename:_oadsimpl_AdsExtractKey=_AdsExtractKey")
#pragma comment(linker, "/export:AdsExtractKey=_AdsExtractKey")
__declspec(dllexport) UNSIGNED32 __stdcall AdsExtractKey(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsExtractKey(a0, a1, a2);
    arc_log_rc("AdsExtractKey", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsFClose ---- */
#define AdsFClose oadsimpl_AdsFClose
extern UNSIGNED32 ENTRYPOINT AdsFClose(ADSHANDLE hFile);
#undef AdsFClose
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFClose=_AdsFClose")
#pragma comment(linker, "/export:AdsFClose=_AdsFClose")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFClose(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsFClose(a0);
    arc_log_rc("AdsFClose", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsFCreate ---- */
#define AdsFCreate oadsimpl_AdsFCreate
extern UNSIGNED32 ENTRYPOINT AdsFCreate(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usAttribute, ADSHANDLE* phFile);
#undef AdsFCreate
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFCreate=_AdsFCreate")
#pragma comment(linker, "/export:AdsFCreate=_AdsFCreate")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFCreate(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, ADSHANDLE* a3) {
    UNSIGNED32 rc = oadsimpl_AdsFCreate(a0, a1, a2, a3);
    arc_log_rc("AdsFCreate", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsFOpen ---- */
#define AdsFOpen oadsimpl_AdsFOpen
extern UNSIGNED32 ENTRYPOINT AdsFOpen(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usMode, ADSHANDLE* phFile);
#undef AdsFOpen
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFOpen=_AdsFOpen")
#pragma comment(linker, "/export:AdsFOpen=_AdsFOpen")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFOpen(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, ADSHANDLE* a3) {
    UNSIGNED32 rc = oadsimpl_AdsFOpen(a0, a1, a2, a3);
    arc_log_rc("AdsFOpen", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsFRead ---- */
#define AdsFRead oadsimpl_AdsFRead
extern UNSIGNED32 ENTRYPOINT AdsFRead(ADSHANDLE hFile, void* pBuf, UNSIGNED32 ulLen, UNSIGNED32* pulRead);
#undef AdsFRead
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFRead=_AdsFRead")
#pragma comment(linker, "/export:AdsFRead=_AdsFRead")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFRead(ADSHANDLE a0, void* a1, UNSIGNED32 a2, UNSIGNED32* a3) {
    UNSIGNED32 rc = oadsimpl_AdsFRead(a0, a1, a2, a3);
    arc_log_rc("AdsFRead", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsFSeek ---- */
#define AdsFSeek oadsimpl_AdsFSeek
extern UNSIGNED32 ENTRYPOINT AdsFSeek(ADSHANDLE hFile, SIGNED32 lOffset, UNSIGNED16 usOrigin, UNSIGNED32* pulPos);
#undef AdsFSeek
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFSeek=_AdsFSeek")
#pragma comment(linker, "/export:AdsFSeek=_AdsFSeek")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFSeek(ADSHANDLE a0, SIGNED32 a1, UNSIGNED16 a2, UNSIGNED32* a3) {
    UNSIGNED32 rc = oadsimpl_AdsFSeek(a0, a1, a2, a3);
    arc_log_rc("AdsFSeek", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsFTSSearch ---- */
#define AdsFTSSearch oadsimpl_AdsFTSSearch
extern UNSIGNED32 ENTRYPOINT AdsFTSSearch(ADSHANDLE hConnect, UNSIGNED8* pucFile, UNSIGNED8* pucQuery, UNSIGNED32* paRecnos, UNSIGNED32* pulCount);
#undef AdsFTSSearch
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFTSSearch=_AdsFTSSearch")
#pragma comment(linker, "/export:AdsFTSSearch=_AdsFTSSearch")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFTSSearch(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32* a3, UNSIGNED32* a4) {
    UNSIGNED32 rc = oadsimpl_AdsFTSSearch(a0, a1, a2, a3, a4);
    arc_log_rc("AdsFTSSearch", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsFWrite ---- */
#define AdsFWrite oadsimpl_AdsFWrite
extern UNSIGNED32 ENTRYPOINT AdsFWrite(ADSHANDLE hFile, const void* pBuf, UNSIGNED32 ulLen, UNSIGNED32* pulWritten);
#undef AdsFWrite
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFWrite=_AdsFWrite")
#pragma comment(linker, "/export:AdsFWrite=_AdsFWrite")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFWrite(ADSHANDLE a0, const void* a1, UNSIGNED32 a2, UNSIGNED32* a3) {
    UNSIGNED32 rc = oadsimpl_AdsFWrite(a0, a1, a2, a3);
    arc_log_rc("AdsFWrite", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsFailedTransactionRecovery ---- */
#define AdsFailedTransactionRecovery oadsimpl_AdsFailedTransactionRecovery
extern UNSIGNED32 ENTRYPOINT AdsFailedTransactionRecovery(UNSIGNED8* pucServer);
#undef AdsFailedTransactionRecovery
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFailedTransactionRecovery=_AdsFailedTransactionRecovery")
#pragma comment(linker, "/export:AdsFailedTransactionRecovery=_AdsFailedTransactionRecovery")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFailedTransactionRecovery(UNSIGNED8* a0) {
    UNSIGNED32 rc = oadsimpl_AdsFailedTransactionRecovery(a0);
    arc_log_rc("AdsFailedTransactionRecovery", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsFileToBinary ---- */
#define AdsFileToBinary oadsimpl_AdsFileToBinary
extern UNSIGNED32 ENTRYPOINT AdsFileToBinary(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16 usType, UNSIGNED8* pucPath);
#undef AdsFileToBinary
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFileToBinary=_AdsFileToBinary")
#pragma comment(linker, "/export:AdsFileToBinary=_AdsFileToBinary")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFileToBinary(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsFileToBinary(a0, a1, a2, a3);
    arc_log_rc("AdsFileToBinary", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsFileToBinaryW ---- */
#define AdsFileToBinaryW oadsimpl_AdsFileToBinaryW
extern UNSIGNED32 ENTRYPOINT AdsFileToBinaryW(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16 usType, UNSIGNED16* pwcPath);
#undef AdsFileToBinaryW
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFileToBinaryW=_AdsFileToBinaryW")
#pragma comment(linker, "/export:AdsFileToBinaryW=_AdsFileToBinaryW")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFileToBinaryW(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, UNSIGNED16* a3) {
    UNSIGNED32 rc = oadsimpl_AdsFileToBinaryW(a0, a1, a2, a3);
    arc_log_rc("AdsFileToBinaryW", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsFilterOption ---- */
#define AdsFilterOption oadsimpl_AdsFilterOption
extern UNSIGNED32 ENTRYPOINT AdsFilterOption(ADSHANDLE hTable, UNSIGNED16 usOption, UNSIGNED16* pusValue);
#undef AdsFilterOption
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFilterOption=_AdsFilterOption")
#pragma comment(linker, "/export:AdsFilterOption=_AdsFilterOption")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFilterOption(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsFilterOption(a0, a1, a2);
    arc_log_rc("AdsFilterOption", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsFindClose ---- */
#define AdsFindClose oadsimpl_AdsFindClose
extern UNSIGNED32 ENTRYPOINT AdsFindClose(ADSHANDLE hConnect, ADSHANDLE hFind);
#undef AdsFindClose
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFindClose=_AdsFindClose")
#pragma comment(linker, "/export:AdsFindClose=_AdsFindClose")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFindClose(ADSHANDLE a0, ADSHANDLE a1) {
    UNSIGNED32 rc = oadsimpl_AdsFindClose(a0, a1);
    arc_log_rc("AdsFindClose", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsFindConnection ---- */
#define AdsFindConnection oadsimpl_AdsFindConnection
extern UNSIGNED32 ENTRYPOINT AdsFindConnection(UNSIGNED8* pucServerName, ADSHANDLE* phConnect);
#undef AdsFindConnection
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFindConnection=_AdsFindConnection")
#pragma comment(linker, "/export:AdsFindConnection=_AdsFindConnection")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFindConnection(UNSIGNED8* a0, ADSHANDLE* a1) {
    UNSIGNED32 rc = oadsimpl_AdsFindConnection(a0, a1);
    arc_log_rc("AdsFindConnection", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsFindConnection25 ---- */
#define AdsFindConnection25 oadsimpl_AdsFindConnection25
extern UNSIGNED32 ENTRYPOINT AdsFindConnection25(UNSIGNED8* pucFullPath, ADSHANDLE* phConnect);
#undef AdsFindConnection25
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFindConnection25=_AdsFindConnection25")
#pragma comment(linker, "/export:AdsFindConnection25=_AdsFindConnection25")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFindConnection25(UNSIGNED8* a0, ADSHANDLE* a1) {
    UNSIGNED32 rc = oadsimpl_AdsFindConnection25(a0, a1);
    arc_log_rc("AdsFindConnection25", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsFindFirstTable ---- */
#define AdsFindFirstTable oadsimpl_AdsFindFirstTable
extern UNSIGNED32 ENTRYPOINT AdsFindFirstTable(ADSHANDLE hConnect, UNSIGNED8* pucMask, UNSIGNED8* pucFileName, UNSIGNED16* pusFileNameLen, ADSHANDLE* phFind);
#undef AdsFindFirstTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFindFirstTable=_AdsFindFirstTable")
#pragma comment(linker, "/export:AdsFindFirstTable=_AdsFindFirstTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFindFirstTable(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16* a3, ADSHANDLE* a4) {
    UNSIGNED32 rc = oadsimpl_AdsFindFirstTable(a0, a1, a2, a3, a4);
    arc_log_rc("AdsFindFirstTable", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsFindNextTable ---- */
#define AdsFindNextTable oadsimpl_AdsFindNextTable
extern UNSIGNED32 ENTRYPOINT AdsFindNextTable(ADSHANDLE hConnect, ADSHANDLE hFind, UNSIGNED8* pucFileName, UNSIGNED16* pusFileNameLen);
#undef AdsFindNextTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFindNextTable=_AdsFindNextTable")
#pragma comment(linker, "/export:AdsFindNextTable=_AdsFindNextTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFindNextTable(ADSHANDLE a0, ADSHANDLE a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    UNSIGNED32 rc = oadsimpl_AdsFindNextTable(a0, a1, a2, a3);
    arc_log_rc("AdsFindNextTable", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsFlushFileBuffers ---- */
#define AdsFlushFileBuffers oadsimpl_AdsFlushFileBuffers
extern UNSIGNED32 ENTRYPOINT AdsFlushFileBuffers(ADSHANDLE hTable);
#undef AdsFlushFileBuffers
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFlushFileBuffers=_AdsFlushFileBuffers")
#pragma comment(linker, "/export:AdsFlushFileBuffers=_AdsFlushFileBuffers")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFlushFileBuffers(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsFlushFileBuffers(a0);
    arc_log_rc("AdsFlushFileBuffers", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsGetAOF ---- */
#define AdsGetAOF oadsimpl_AdsGetAOF
extern UNSIGNED32 ENTRYPOINT AdsGetAOF(ADSHANDLE hTable, UNSIGNED8* pucFilter, UNSIGNED16* pusLen);
#undef AdsGetAOF
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetAOF=_AdsGetAOF")
#pragma comment(linker, "/export:AdsGetAOF=_AdsGetAOF")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetAOF(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetAOF(a0, a1, a2);
    arc_log_rc("AdsGetAOF", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetAOFOptLevel ---- */
#define AdsGetAOFOptLevel oadsimpl_AdsGetAOFOptLevel
extern UNSIGNED32 ENTRYPOINT AdsGetAOFOptLevel(ADSHANDLE hTable, UNSIGNED16* pusLevel, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetAOFOptLevel
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetAOFOptLevel=_AdsGetAOFOptLevel")
#pragma comment(linker, "/export:AdsGetAOFOptLevel=_AdsGetAOFOptLevel")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetAOFOptLevel(ADSHANDLE a0, UNSIGNED16* a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    UNSIGNED32 rc = oadsimpl_AdsGetAOFOptLevel(a0, a1, a2, a3);
    arc_log_rc("AdsGetAOFOptLevel", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsGetAllIndexes ---- */
#define AdsGetAllIndexes oadsimpl_AdsGetAllIndexes
extern UNSIGNED32 ENTRYPOINT AdsGetAllIndexes(ADSHANDLE hTable, ADSHANDLE* ahIndex, UNSIGNED16* pusArrayLen);
#undef AdsGetAllIndexes
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetAllIndexes=_AdsGetAllIndexes")
#pragma comment(linker, "/export:AdsGetAllIndexes=_AdsGetAllIndexes")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetAllIndexes(ADSHANDLE a0, ADSHANDLE* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetAllIndexes(a0, a1, a2);
    arc_log_rc("AdsGetAllIndexes", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetAllLocks ---- */
#define AdsGetAllLocks oadsimpl_AdsGetAllLocks
extern UNSIGNED32 ENTRYPOINT AdsGetAllLocks(ADSHANDLE hTable, UNSIGNED32* paRecnos, UNSIGNED16* pusCount);
#undef AdsGetAllLocks
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetAllLocks=_AdsGetAllLocks")
#pragma comment(linker, "/export:AdsGetAllLocks=_AdsGetAllLocks")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetAllLocks(ADSHANDLE a0, UNSIGNED32* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetAllLocks(a0, a1, a2);
    arc_log_rc("AdsGetAllLocks", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetAllTables ---- */
#define AdsGetAllTables oadsimpl_AdsGetAllTables
extern UNSIGNED32 ENTRYPOINT AdsGetAllTables(ADSHANDLE hConnect, ADSHANDLE* ahTable, UNSIGNED16* pusArrayLen);
#undef AdsGetAllTables
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetAllTables=_AdsGetAllTables")
#pragma comment(linker, "/export:AdsGetAllTables=_AdsGetAllTables")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetAllTables(ADSHANDLE a0, ADSHANDLE* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetAllTables(a0, a1, a2);
    arc_log_rc("AdsGetAllTables", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetBinary ---- */
#define AdsGetBinary oadsimpl_AdsGetBinary
extern UNSIGNED32 ENTRYPOINT AdsGetBinary(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED32 ulOffset, UNSIGNED8* pucBuf, UNSIGNED32* pulLen);
#undef AdsGetBinary
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetBinary=_AdsGetBinary")
#pragma comment(linker, "/export:AdsGetBinary=_AdsGetBinary")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetBinary(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32 a2, UNSIGNED8* a3, UNSIGNED32* a4) {
    UNSIGNED32 rc = oadsimpl_AdsGetBinary(a0, a1, a2, a3, a4);
    arc_log_rc("AdsGetBinary", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsGetBinaryLength ---- */
#define AdsGetBinaryLength oadsimpl_AdsGetBinaryLength
extern UNSIGNED32 ENTRYPOINT AdsGetBinaryLength(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED32* pulLength);
#undef AdsGetBinaryLength
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetBinaryLength=_AdsGetBinaryLength")
#pragma comment(linker, "/export:AdsGetBinaryLength=_AdsGetBinaryLength")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetBinaryLength(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetBinaryLength(a0, a1, a2);
    arc_log_rc("AdsGetBinaryLength", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetBookmark ---- */
#define AdsGetBookmark oadsimpl_AdsGetBookmark
extern UNSIGNED32 ENTRYPOINT AdsGetBookmark(ADSHANDLE hTable, ADSHANDLE* phBookmark);
#undef AdsGetBookmark
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetBookmark=_AdsGetBookmark")
#pragma comment(linker, "/export:AdsGetBookmark=_AdsGetBookmark")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetBookmark(ADSHANDLE a0, ADSHANDLE* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetBookmark(a0, a1);
    arc_log_rc("AdsGetBookmark", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetBookmark60 ---- */
#define AdsGetBookmark60 oadsimpl_AdsGetBookmark60
extern UNSIGNED32 ENTRYPOINT AdsGetBookmark60(ADSHANDLE hObj, UNSIGNED8* pucBookmark, UNSIGNED32* pulLength);
#undef AdsGetBookmark60
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetBookmark60=_AdsGetBookmark60")
#pragma comment(linker, "/export:AdsGetBookmark60=_AdsGetBookmark60")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetBookmark60(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetBookmark60(a0, a1, a2);
    arc_log_rc("AdsGetBookmark60", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetConnectionType ---- */
#define AdsGetConnectionType oadsimpl_AdsGetConnectionType
extern UNSIGNED32 ENTRYPOINT AdsGetConnectionType(ADSHANDLE hConnect, UNSIGNED16* pusType);
#undef AdsGetConnectionType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetConnectionType=_AdsGetConnectionType")
#pragma comment(linker, "/export:AdsGetConnectionType=_AdsGetConnectionType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetConnectionType(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetConnectionType(a0, a1);
    arc_log_rc("AdsGetConnectionType", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetDate ---- */
#define AdsGetDate oadsimpl_AdsGetDate
extern UNSIGNED32 ENTRYPOINT AdsGetDate(ADSHANDLE hObj, UNSIGNED8* pucFldId, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetDate
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetDate=_AdsGetDate")
#pragma comment(linker, "/export:AdsGetDate=_AdsGetDate")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetDate(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    UNSIGNED32 rc = oadsimpl_AdsGetDate(a0, a1, a2, a3);
    arc_log_rc("AdsGetDate", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsGetDateFormat ---- */
#define AdsGetDateFormat oadsimpl_AdsGetDateFormat
extern UNSIGNED32 ENTRYPOINT AdsGetDateFormat(UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetDateFormat
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetDateFormat=_AdsGetDateFormat")
#pragma comment(linker, "/export:AdsGetDateFormat=_AdsGetDateFormat")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetDateFormat(UNSIGNED8* a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetDateFormat(a0, a1);
    arc_log_rc("AdsGetDateFormat", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetDateFormat60 ---- */
#define AdsGetDateFormat60 oadsimpl_AdsGetDateFormat60
extern UNSIGNED32 ENTRYPOINT AdsGetDateFormat60(ADSHANDLE hConnect, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetDateFormat60
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetDateFormat60=_AdsGetDateFormat60")
#pragma comment(linker, "/export:AdsGetDateFormat60=_AdsGetDateFormat60")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetDateFormat60(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetDateFormat60(a0, a1, a2);
    arc_log_rc("AdsGetDateFormat60", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetDefault ---- */
#define AdsGetDefault oadsimpl_AdsGetDefault
extern UNSIGNED32 ENTRYPOINT AdsGetDefault(UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetDefault
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetDefault=_AdsGetDefault")
#pragma comment(linker, "/export:AdsGetDefault=_AdsGetDefault")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetDefault(UNSIGNED8* a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetDefault(a0, a1);
    arc_log_rc("AdsGetDefault", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetDefaultConnection ---- */
#define AdsGetDefaultConnection oadsimpl_AdsGetDefaultConnection
extern UNSIGNED32 ENTRYPOINT AdsGetDefaultConnection(ADSHANDLE* phConn);
#undef AdsGetDefaultConnection
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetDefaultConnection=_AdsGetDefaultConnection")
#pragma comment(linker, "/export:AdsGetDefaultConnection=_AdsGetDefaultConnection")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetDefaultConnection(ADSHANDLE* a0) {
    UNSIGNED32 rc = oadsimpl_AdsGetDefaultConnection(a0);
    arc_log_rc("AdsGetDefaultConnection", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsGetDeleted ---- */
#define AdsGetDeleted oadsimpl_AdsGetDeleted
extern UNSIGNED32 ENTRYPOINT AdsGetDeleted(UNSIGNED16* pbShow);
#undef AdsGetDeleted
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetDeleted=_AdsGetDeleted")
#pragma comment(linker, "/export:AdsGetDeleted=_AdsGetDeleted")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetDeleted(UNSIGNED16* a0) {
    UNSIGNED32 rc = oadsimpl_AdsGetDeleted(a0);
    arc_log_rc("AdsGetDeleted", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsGetDouble ---- */
#define AdsGetDouble oadsimpl_AdsGetDouble
extern UNSIGNED32 ENTRYPOINT AdsGetDouble(ADSHANDLE hTable, UNSIGNED8* pucField, double* pdValue);
#undef AdsGetDouble
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetDouble=_AdsGetDouble")
#pragma comment(linker, "/export:AdsGetDouble=_AdsGetDouble")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetDouble(ADSHANDLE a0, UNSIGNED8* a1, double* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetDouble(a0, a1, a2);
    arc_log_rc("AdsGetDouble", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetEpoch ---- */
#define AdsGetEpoch oadsimpl_AdsGetEpoch
extern UNSIGNED32 ENTRYPOINT AdsGetEpoch(UNSIGNED16* pusEpoch);
#undef AdsGetEpoch
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetEpoch=_AdsGetEpoch")
#pragma comment(linker, "/export:AdsGetEpoch=_AdsGetEpoch")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetEpoch(UNSIGNED16* a0) {
    UNSIGNED32 rc = oadsimpl_AdsGetEpoch(a0);
    arc_log_rc("AdsGetEpoch", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsGetErrorString ---- */
#define AdsGetErrorString oadsimpl_AdsGetErrorString
extern UNSIGNED32 ENTRYPOINT AdsGetErrorString(UNSIGNED32 ulErr, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetErrorString
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetErrorString=_AdsGetErrorString")
#pragma comment(linker, "/export:AdsGetErrorString=_AdsGetErrorString")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetErrorString(UNSIGNED32 a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetErrorString(a0, a1, a2);
    arc_log_rc("AdsGetErrorString", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetExact ---- */
#define AdsGetExact oadsimpl_AdsGetExact
extern UNSIGNED32 ENTRYPOINT AdsGetExact(UNSIGNED16* pbExact);
#undef AdsGetExact
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetExact=_AdsGetExact")
#pragma comment(linker, "/export:AdsGetExact=_AdsGetExact")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetExact(UNSIGNED16* a0) {
    UNSIGNED32 rc = oadsimpl_AdsGetExact(a0);
    arc_log_rc("AdsGetExact", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsGetExact22 ---- */
#define AdsGetExact22 oadsimpl_AdsGetExact22
extern UNSIGNED32 ENTRYPOINT AdsGetExact22(ADSHANDLE hObj, UNSIGNED16* pbExact);
#undef AdsGetExact22
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetExact22=_AdsGetExact22")
#pragma comment(linker, "/export:AdsGetExact22=_AdsGetExact22")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetExact22(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetExact22(a0, a1);
    arc_log_rc("AdsGetExact22", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetFTSIndexes ---- */
#define AdsGetFTSIndexes oadsimpl_AdsGetFTSIndexes
extern UNSIGNED32 ENTRYPOINT AdsGetFTSIndexes(ADSHANDLE hTable, ADSHANDLE* ahIndex, UNSIGNED16* pusArrayLen);
#undef AdsGetFTSIndexes
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFTSIndexes=_AdsGetFTSIndexes")
#pragma comment(linker, "/export:AdsGetFTSIndexes=_AdsGetFTSIndexes")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFTSIndexes(ADSHANDLE a0, ADSHANDLE* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetFTSIndexes(a0, a1, a2);
    arc_log_rc("AdsGetFTSIndexes", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetField ---- */
#define AdsGetField oadsimpl_AdsGetField
extern UNSIGNED32 ENTRYPOINT AdsGetField(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED8* pucBuf, UNSIGNED32* pulLen, UNSIGNED16 usOption);
#undef AdsGetField
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetField=_AdsGetField")
#pragma comment(linker, "/export:AdsGetField=_AdsGetField")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetField(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32* a3, UNSIGNED16 a4) {
    UNSIGNED32 rc = oadsimpl_AdsGetField(a0, a1, a2, a3, a4);
    arc_log_rc("AdsGetField", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsGetFieldDecimals ---- */
#define AdsGetFieldDecimals oadsimpl_AdsGetFieldDecimals
extern UNSIGNED32 ENTRYPOINT AdsGetFieldDecimals(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pusDec);
#undef AdsGetFieldDecimals
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFieldDecimals=_AdsGetFieldDecimals")
#pragma comment(linker, "/export:AdsGetFieldDecimals=_AdsGetFieldDecimals")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFieldDecimals(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetFieldDecimals(a0, a1, a2);
    arc_log_rc("AdsGetFieldDecimals", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetFieldLength ---- */
#define AdsGetFieldLength oadsimpl_AdsGetFieldLength
extern UNSIGNED32 ENTRYPOINT AdsGetFieldLength(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED32* pulLen);
#undef AdsGetFieldLength
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFieldLength=_AdsGetFieldLength")
#pragma comment(linker, "/export:AdsGetFieldLength=_AdsGetFieldLength")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFieldLength(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetFieldLength(a0, a1, a2);
    arc_log_rc("AdsGetFieldLength", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetFieldLength100 ---- */
#define AdsGetFieldLength100 oadsimpl_AdsGetFieldLength100
extern UNSIGNED32 ENTRYPOINT AdsGetFieldLength100(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED32 ulOptions, UNSIGNED32* pulLen);
#undef AdsGetFieldLength100
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFieldLength100=_AdsGetFieldLength100")
#pragma comment(linker, "/export:AdsGetFieldLength100=_AdsGetFieldLength100")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFieldLength100(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32 a2, UNSIGNED32* a3) {
    UNSIGNED32 rc = oadsimpl_AdsGetFieldLength100(a0, a1, a2, a3);
    arc_log_rc("AdsGetFieldLength100", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsGetFieldName ---- */
#define AdsGetFieldName oadsimpl_AdsGetFieldName
extern UNSIGNED32 ENTRYPOINT AdsGetFieldName(ADSHANDLE hTable, UNSIGNED16 usFieldNum, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetFieldName
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFieldName=_AdsGetFieldName")
#pragma comment(linker, "/export:AdsGetFieldName=_AdsGetFieldName")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFieldName(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    UNSIGNED32 rc = oadsimpl_AdsGetFieldName(a0, a1, a2, a3);
    arc_log_rc("AdsGetFieldName", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsGetFieldNum ---- */
#define AdsGetFieldNum oadsimpl_AdsGetFieldNum
extern UNSIGNED32 ENTRYPOINT AdsGetFieldNum(ADSHANDLE hTable, UNSIGNED8* pucFldName, UNSIGNED16* pusNum);
#undef AdsGetFieldNum
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFieldNum=_AdsGetFieldNum")
#pragma comment(linker, "/export:AdsGetFieldNum=_AdsGetFieldNum")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFieldNum(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetFieldNum(a0, a1, a2);
    arc_log_rc("AdsGetFieldNum", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetFieldOffset ---- */
#define AdsGetFieldOffset oadsimpl_AdsGetFieldOffset
extern UNSIGNED32 ENTRYPOINT AdsGetFieldOffset(ADSHANDLE hTable, UNSIGNED8* pucFldName, UNSIGNED32* pulOffset);
#undef AdsGetFieldOffset
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFieldOffset=_AdsGetFieldOffset")
#pragma comment(linker, "/export:AdsGetFieldOffset=_AdsGetFieldOffset")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFieldOffset(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetFieldOffset(a0, a1, a2);
    arc_log_rc("AdsGetFieldOffset", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetFieldRaw ---- */
#define AdsGetFieldRaw oadsimpl_AdsGetFieldRaw
extern UNSIGNED32 ENTRYPOINT AdsGetFieldRaw(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED8* pucBuf, UNSIGNED32* pulLen);
#undef AdsGetFieldRaw
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFieldRaw=_AdsGetFieldRaw")
#pragma comment(linker, "/export:AdsGetFieldRaw=_AdsGetFieldRaw")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFieldRaw(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32* a3) {
    UNSIGNED32 rc = oadsimpl_AdsGetFieldRaw(a0, a1, a2, a3);
    arc_log_rc("AdsGetFieldRaw", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsGetFieldType ---- */
#define AdsGetFieldType oadsimpl_AdsGetFieldType
extern UNSIGNED32 ENTRYPOINT AdsGetFieldType(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pusType);
#undef AdsGetFieldType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFieldType=_AdsGetFieldType")
#pragma comment(linker, "/export:AdsGetFieldType=_AdsGetFieldType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFieldType(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetFieldType(a0, a1, a2);
    arc_log_rc("AdsGetFieldType", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetFieldW ---- */
#define AdsGetFieldW oadsimpl_AdsGetFieldW
extern UNSIGNED32 ENTRYPOINT AdsGetFieldW(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pucBufW, UNSIGNED32* pulLenW, UNSIGNED16 usOption);
#undef AdsGetFieldW
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFieldW=_AdsGetFieldW")
#pragma comment(linker, "/export:AdsGetFieldW=_AdsGetFieldW")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFieldW(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2, UNSIGNED32* a3, UNSIGNED16 a4) {
    UNSIGNED32 rc = oadsimpl_AdsGetFieldW(a0, a1, a2, a3, a4);
    arc_log_rc("AdsGetFieldW", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsGetFileDate ---- */
#define AdsGetFileDate oadsimpl_AdsGetFileDate
extern UNSIGNED32 ENTRYPOINT AdsGetFileDate(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucDate, UNSIGNED16* pusLen);
#undef AdsGetFileDate
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFileDate=_AdsGetFileDate")
#pragma comment(linker, "/export:AdsGetFileDate=_AdsGetFileDate")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFileDate(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    UNSIGNED32 rc = oadsimpl_AdsGetFileDate(a0, a1, a2, a3);
    arc_log_rc("AdsGetFileDate", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsGetFileSize ---- */
#define AdsGetFileSize oadsimpl_AdsGetFileSize
extern UNSIGNED32 ENTRYPOINT AdsGetFileSize(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED32* pulSize);
#undef AdsGetFileSize
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFileSize=_AdsGetFileSize")
#pragma comment(linker, "/export:AdsGetFileSize=_AdsGetFileSize")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFileSize(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetFileSize(a0, a1, a2);
    arc_log_rc("AdsGetFileSize", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetFileTime ---- */
#define AdsGetFileTime oadsimpl_AdsGetFileTime
extern UNSIGNED32 ENTRYPOINT AdsGetFileTime(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucTime, UNSIGNED16* pusLen);
#undef AdsGetFileTime
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFileTime=_AdsGetFileTime")
#pragma comment(linker, "/export:AdsGetFileTime=_AdsGetFileTime")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFileTime(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    UNSIGNED32 rc = oadsimpl_AdsGetFileTime(a0, a1, a2, a3);
    arc_log_rc("AdsGetFileTime", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsGetFilter ---- */
#define AdsGetFilter oadsimpl_AdsGetFilter
extern UNSIGNED32 ENTRYPOINT AdsGetFilter(ADSHANDLE hTable, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetFilter
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFilter=_AdsGetFilter")
#pragma comment(linker, "/export:AdsGetFilter=_AdsGetFilter")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFilter(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetFilter(a0, a1, a2);
    arc_log_rc("AdsGetFilter", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetHandleType ---- */
#define AdsGetHandleType oadsimpl_AdsGetHandleType
extern UNSIGNED32 ENTRYPOINT AdsGetHandleType(ADSHANDLE hAny, UNSIGNED16* pusType);
#undef AdsGetHandleType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetHandleType=_AdsGetHandleType")
#pragma comment(linker, "/export:AdsGetHandleType=_AdsGetHandleType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetHandleType(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetHandleType(a0, a1);
    arc_log_rc("AdsGetHandleType", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetIndexCondition ---- */
#define AdsGetIndexCondition oadsimpl_AdsGetIndexCondition
extern UNSIGNED32 ENTRYPOINT AdsGetIndexCondition(ADSHANDLE hIndex, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetIndexCondition
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetIndexCondition=_AdsGetIndexCondition")
#pragma comment(linker, "/export:AdsGetIndexCondition=_AdsGetIndexCondition")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetIndexCondition(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetIndexCondition(a0, a1, a2);
    arc_log_rc("AdsGetIndexCondition", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetIndexExpr ---- */
#define AdsGetIndexExpr oadsimpl_AdsGetIndexExpr
extern UNSIGNED32 ENTRYPOINT AdsGetIndexExpr(ADSHANDLE hIndex, UNSIGNED8* pucBuf, UNSIGNED16* pusBufLen);
#undef AdsGetIndexExpr
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetIndexExpr=_AdsGetIndexExpr")
#pragma comment(linker, "/export:AdsGetIndexExpr=_AdsGetIndexExpr")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetIndexExpr(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetIndexExpr(a0, a1, a2);
    arc_log_rc("AdsGetIndexExpr", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetIndexFilename ---- */
#define AdsGetIndexFilename oadsimpl_AdsGetIndexFilename
extern UNSIGNED32 ENTRYPOINT AdsGetIndexFilename(ADSHANDLE hIndex, UNSIGNED16 usOption, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetIndexFilename
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetIndexFilename=_AdsGetIndexFilename")
#pragma comment(linker, "/export:AdsGetIndexFilename=_AdsGetIndexFilename")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetIndexFilename(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    UNSIGNED32 rc = oadsimpl_AdsGetIndexFilename(a0, a1, a2, a3);
    arc_log_rc("AdsGetIndexFilename", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsGetIndexHandle ---- */
#define AdsGetIndexHandle oadsimpl_AdsGetIndexHandle
extern UNSIGNED32 ENTRYPOINT AdsGetIndexHandle(ADSHANDLE hTable, UNSIGNED8* pucName, ADSHANDLE* phIndex);
#undef AdsGetIndexHandle
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetIndexHandle=_AdsGetIndexHandle")
#pragma comment(linker, "/export:AdsGetIndexHandle=_AdsGetIndexHandle")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetIndexHandle(ADSHANDLE a0, UNSIGNED8* a1, ADSHANDLE* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetIndexHandle(a0, a1, a2);
    arc_log_rc("AdsGetIndexHandle", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetIndexHandleByOrder ---- */
#define AdsGetIndexHandleByOrder oadsimpl_AdsGetIndexHandleByOrder
extern UNSIGNED32 ENTRYPOINT AdsGetIndexHandleByOrder(ADSHANDLE hTable, UNSIGNED16 usOrder, ADSHANDLE* phIndex);
#undef AdsGetIndexHandleByOrder
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetIndexHandleByOrder=_AdsGetIndexHandleByOrder")
#pragma comment(linker, "/export:AdsGetIndexHandleByOrder=_AdsGetIndexHandleByOrder")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetIndexHandleByOrder(ADSHANDLE a0, UNSIGNED16 a1, ADSHANDLE* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetIndexHandleByOrder(a0, a1, a2);
    arc_log_rc("AdsGetIndexHandleByOrder", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetIndexName ---- */
#define AdsGetIndexName oadsimpl_AdsGetIndexName
extern UNSIGNED32 ENTRYPOINT AdsGetIndexName(ADSHANDLE hIndex, UNSIGNED8* pucBuf, UNSIGNED16* pusBufLen);
#undef AdsGetIndexName
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetIndexName=_AdsGetIndexName")
#pragma comment(linker, "/export:AdsGetIndexName=_AdsGetIndexName")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetIndexName(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetIndexName(a0, a1, a2);
    arc_log_rc("AdsGetIndexName", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetIndexOrderByHandle ---- */
#define AdsGetIndexOrderByHandle oadsimpl_AdsGetIndexOrderByHandle
extern UNSIGNED32 ENTRYPOINT AdsGetIndexOrderByHandle(ADSHANDLE hIndex, UNSIGNED16* pusOrder);
#undef AdsGetIndexOrderByHandle
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetIndexOrderByHandle=_AdsGetIndexOrderByHandle")
#pragma comment(linker, "/export:AdsGetIndexOrderByHandle=_AdsGetIndexOrderByHandle")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetIndexOrderByHandle(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetIndexOrderByHandle(a0, a1);
    arc_log_rc("AdsGetIndexOrderByHandle", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetJulian ---- */
#define AdsGetJulian oadsimpl_AdsGetJulian
extern UNSIGNED32 ENTRYPOINT AdsGetJulian(ADSHANDLE hTable, UNSIGNED8* pucField, SIGNED32* plJulian);
#undef AdsGetJulian
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetJulian=_AdsGetJulian")
#pragma comment(linker, "/export:AdsGetJulian=_AdsGetJulian")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetJulian(ADSHANDLE a0, UNSIGNED8* a1, SIGNED32* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetJulian(a0, a1, a2);
    arc_log_rc("AdsGetJulian", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetKeyCount ---- */
#define AdsGetKeyCount oadsimpl_AdsGetKeyCount
extern UNSIGNED32 ENTRYPOINT AdsGetKeyCount(ADSHANDLE hIndex, UNSIGNED16 usFilterOption, UNSIGNED32* pulCount);
#undef AdsGetKeyCount
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetKeyCount=_AdsGetKeyCount")
#pragma comment(linker, "/export:AdsGetKeyCount=_AdsGetKeyCount")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetKeyCount(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED32* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetKeyCount(a0, a1, a2);
    arc_log_rc("AdsGetKeyCount", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetKeyLength ---- */
#define AdsGetKeyLength oadsimpl_AdsGetKeyLength
extern UNSIGNED32 ENTRYPOINT AdsGetKeyLength(ADSHANDLE hIndex, UNSIGNED16* pusLen);
#undef AdsGetKeyLength
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetKeyLength=_AdsGetKeyLength")
#pragma comment(linker, "/export:AdsGetKeyLength=_AdsGetKeyLength")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetKeyLength(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetKeyLength(a0, a1);
    arc_log_rc("AdsGetKeyLength", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetKeyNum ---- */
#define AdsGetKeyNum oadsimpl_AdsGetKeyNum
extern UNSIGNED32 ENTRYPOINT AdsGetKeyNum(ADSHANDLE hIndex, UNSIGNED16 usFlag, UNSIGNED32* pulKey);
#undef AdsGetKeyNum
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetKeyNum=_AdsGetKeyNum")
#pragma comment(linker, "/export:AdsGetKeyNum=_AdsGetKeyNum")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetKeyNum(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED32* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetKeyNum(a0, a1, a2);
    arc_log_rc("AdsGetKeyNum", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetKeyType ---- */
#define AdsGetKeyType oadsimpl_AdsGetKeyType
extern UNSIGNED32 ENTRYPOINT AdsGetKeyType(ADSHANDLE hIndex, UNSIGNED16* pusType);
#undef AdsGetKeyType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetKeyType=_AdsGetKeyType")
#pragma comment(linker, "/export:AdsGetKeyType=_AdsGetKeyType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetKeyType(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetKeyType(a0, a1);
    arc_log_rc("AdsGetKeyType", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetLastAutoinc ---- */
#define AdsGetLastAutoinc oadsimpl_AdsGetLastAutoinc
extern UNSIGNED32 ENTRYPOINT AdsGetLastAutoinc(ADSHANDLE hTable, UNSIGNED32* pulValue);
#undef AdsGetLastAutoinc
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetLastAutoinc=_AdsGetLastAutoinc")
#pragma comment(linker, "/export:AdsGetLastAutoinc=_AdsGetLastAutoinc")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetLastAutoinc(ADSHANDLE a0, UNSIGNED32* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetLastAutoinc(a0, a1);
    arc_log_rc("AdsGetLastAutoinc", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetLastError ---- */
#define AdsGetLastError oadsimpl_AdsGetLastError
extern UNSIGNED32 ENTRYPOINT AdsGetLastError(UNSIGNED32* pulCode, UNSIGNED8* pucBuf, UNSIGNED16* pusBufLen);
#undef AdsGetLastError
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetLastError=_AdsGetLastError")
#pragma comment(linker, "/export:AdsGetLastError=_AdsGetLastError")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetLastError(UNSIGNED32* a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetLastError(a0, a1, a2);
    arc_log_rc("AdsGetLastError", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetLastTableUpdate ---- */
#define AdsGetLastTableUpdate oadsimpl_AdsGetLastTableUpdate
extern UNSIGNED32 ENTRYPOINT AdsGetLastTableUpdate(ADSHANDLE hTable, UNSIGNED8* pucDate, UNSIGNED16* pusLen);
#undef AdsGetLastTableUpdate
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetLastTableUpdate=_AdsGetLastTableUpdate")
#pragma comment(linker, "/export:AdsGetLastTableUpdate=_AdsGetLastTableUpdate")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetLastTableUpdate(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetLastTableUpdate(a0, a1, a2);
    arc_log_rc("AdsGetLastTableUpdate", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetLockCycle ---- */
#define AdsGetLockCycle oadsimpl_AdsGetLockCycle
extern UNSIGNED32 ENTRYPOINT AdsGetLockCycle(ADSHANDLE hConnect, UNSIGNED32* pulCycle);
#undef AdsGetLockCycle
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetLockCycle=_AdsGetLockCycle")
#pragma comment(linker, "/export:AdsGetLockCycle=_AdsGetLockCycle")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetLockCycle(ADSHANDLE a0, UNSIGNED32* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetLockCycle(a0, a1);
    arc_log_rc("AdsGetLockCycle", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetLockRetryCount ---- */
#define AdsGetLockRetryCount oadsimpl_AdsGetLockRetryCount
extern UNSIGNED32 ENTRYPOINT AdsGetLockRetryCount(ADSHANDLE hConnect, UNSIGNED16* pusRetryCount);
#undef AdsGetLockRetryCount
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetLockRetryCount=_AdsGetLockRetryCount")
#pragma comment(linker, "/export:AdsGetLockRetryCount=_AdsGetLockRetryCount")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetLockRetryCount(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetLockRetryCount(a0, a1);
    arc_log_rc("AdsGetLockRetryCount", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetLogical ---- */
#define AdsGetLogical oadsimpl_AdsGetLogical
extern UNSIGNED32 ENTRYPOINT AdsGetLogical(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pbValue);
#undef AdsGetLogical
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetLogical=_AdsGetLogical")
#pragma comment(linker, "/export:AdsGetLogical=_AdsGetLogical")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetLogical(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetLogical(a0, a1, a2);
    arc_log_rc("AdsGetLogical", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetLong ---- */
#define AdsGetLong oadsimpl_AdsGetLong
extern UNSIGNED32 ENTRYPOINT AdsGetLong(ADSHANDLE hTable, UNSIGNED8* pucField, SIGNED32* plVal);
#undef AdsGetLong
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetLong=_AdsGetLong")
#pragma comment(linker, "/export:AdsGetLong=_AdsGetLong")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetLong(ADSHANDLE a0, UNSIGNED8* a1, SIGNED32* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetLong(a0, a1, a2);
    arc_log_rc("AdsGetLong", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetLongLong ---- */
#define AdsGetLongLong oadsimpl_AdsGetLongLong
extern UNSIGNED32 ENTRYPOINT AdsGetLongLong(ADSHANDLE hTable, UNSIGNED8* pucField, int64_t* pllValue);
#undef AdsGetLongLong
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetLongLong=_AdsGetLongLong")
#pragma comment(linker, "/export:AdsGetLongLong=_AdsGetLongLong")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetLongLong(ADSHANDLE a0, UNSIGNED8* a1, int64_t* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetLongLong(a0, a1, a2);
    arc_log_rc("AdsGetLongLong", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetMemoBlockSize ---- */
#define AdsGetMemoBlockSize oadsimpl_AdsGetMemoBlockSize
extern UNSIGNED32 ENTRYPOINT AdsGetMemoBlockSize(ADSHANDLE hObj, UNSIGNED16* pusBlockSize);
#undef AdsGetMemoBlockSize
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetMemoBlockSize=_AdsGetMemoBlockSize")
#pragma comment(linker, "/export:AdsGetMemoBlockSize=_AdsGetMemoBlockSize")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetMemoBlockSize(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetMemoBlockSize(a0, a1);
    arc_log_rc("AdsGetMemoBlockSize", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetMemoDataType ---- */
#define AdsGetMemoDataType oadsimpl_AdsGetMemoDataType
extern UNSIGNED32 ENTRYPOINT AdsGetMemoDataType(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pusType);
#undef AdsGetMemoDataType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetMemoDataType=_AdsGetMemoDataType")
#pragma comment(linker, "/export:AdsGetMemoDataType=_AdsGetMemoDataType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetMemoDataType(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetMemoDataType(a0, a1, a2);
    arc_log_rc("AdsGetMemoDataType", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetMemoLength ---- */
#define AdsGetMemoLength oadsimpl_AdsGetMemoLength
extern UNSIGNED32 ENTRYPOINT AdsGetMemoLength(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED32* pulLen);
#undef AdsGetMemoLength
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetMemoLength=_AdsGetMemoLength")
#pragma comment(linker, "/export:AdsGetMemoLength=_AdsGetMemoLength")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetMemoLength(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetMemoLength(a0, a1, a2);
    arc_log_rc("AdsGetMemoLength", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetMilliseconds ---- */
#define AdsGetMilliseconds oadsimpl_AdsGetMilliseconds
extern UNSIGNED32 ENTRYPOINT AdsGetMilliseconds(ADSHANDLE hTable, UNSIGNED8* pucField, SIGNED32* plMs);
#undef AdsGetMilliseconds
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetMilliseconds=_AdsGetMilliseconds")
#pragma comment(linker, "/export:AdsGetMilliseconds=_AdsGetMilliseconds")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetMilliseconds(ADSHANDLE a0, UNSIGNED8* a1, SIGNED32* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetMilliseconds(a0, a1, a2);
    arc_log_rc("AdsGetMilliseconds", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetNumActiveLinks ---- */
#define AdsGetNumActiveLinks oadsimpl_AdsGetNumActiveLinks
extern UNSIGNED32 ENTRYPOINT AdsGetNumActiveLinks(ADSHANDLE hTable, UNSIGNED16* pusCount);
#undef AdsGetNumActiveLinks
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetNumActiveLinks=_AdsGetNumActiveLinks")
#pragma comment(linker, "/export:AdsGetNumActiveLinks=_AdsGetNumActiveLinks")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetNumActiveLinks(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetNumActiveLinks(a0, a1);
    arc_log_rc("AdsGetNumActiveLinks", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetNumFields ---- */
#define AdsGetNumFields oadsimpl_AdsGetNumFields
extern UNSIGNED32 ENTRYPOINT AdsGetNumFields(ADSHANDLE hTable, UNSIGNED16* pusFields);
#undef AdsGetNumFields
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetNumFields=_AdsGetNumFields")
#pragma comment(linker, "/export:AdsGetNumFields=_AdsGetNumFields")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetNumFields(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetNumFields(a0, a1);
    arc_log_rc("AdsGetNumFields", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetNumIndexes ---- */
#define AdsGetNumIndexes oadsimpl_AdsGetNumIndexes
extern UNSIGNED32 ENTRYPOINT AdsGetNumIndexes(ADSHANDLE hTable, UNSIGNED16* pusCount);
#undef AdsGetNumIndexes
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetNumIndexes=_AdsGetNumIndexes")
#pragma comment(linker, "/export:AdsGetNumIndexes=_AdsGetNumIndexes")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetNumIndexes(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetNumIndexes(a0, a1);
    arc_log_rc("AdsGetNumIndexes", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetNumLocks ---- */
#define AdsGetNumLocks oadsimpl_AdsGetNumLocks
extern UNSIGNED32 ENTRYPOINT AdsGetNumLocks(ADSHANDLE hTable, UNSIGNED16* pusCount);
#undef AdsGetNumLocks
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetNumLocks=_AdsGetNumLocks")
#pragma comment(linker, "/export:AdsGetNumLocks=_AdsGetNumLocks")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetNumLocks(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetNumLocks(a0, a1);
    arc_log_rc("AdsGetNumLocks", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetNumOpenTables ---- */
#define AdsGetNumOpenTables oadsimpl_AdsGetNumOpenTables
extern UNSIGNED32 ENTRYPOINT AdsGetNumOpenTables(UNSIGNED16* pusCount);
#undef AdsGetNumOpenTables
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetNumOpenTables=_AdsGetNumOpenTables")
#pragma comment(linker, "/export:AdsGetNumOpenTables=_AdsGetNumOpenTables")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetNumOpenTables(UNSIGNED16* a0) {
    UNSIGNED32 rc = oadsimpl_AdsGetNumOpenTables(a0);
    arc_log_rc("AdsGetNumOpenTables", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsGetNumParams ---- */
#define AdsGetNumParams oadsimpl_AdsGetNumParams
extern UNSIGNED32 ENTRYPOINT AdsGetNumParams(ADSHANDLE hStatement, UNSIGNED16* pusNumParams);
#undef AdsGetNumParams
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetNumParams=_AdsGetNumParams")
#pragma comment(linker, "/export:AdsGetNumParams=_AdsGetNumParams")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetNumParams(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetNumParams(a0, a1);
    arc_log_rc("AdsGetNumParams", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetRecord ---- */
#define AdsGetRecord oadsimpl_AdsGetRecord
extern UNSIGNED32 ENTRYPOINT AdsGetRecord(ADSHANDLE hTable, UNSIGNED8* pucBuf, UNSIGNED32* pulLen);
#undef AdsGetRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetRecord=_AdsGetRecord")
#pragma comment(linker, "/export:AdsGetRecord=_AdsGetRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetRecord(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetRecord(a0, a1, a2);
    arc_log_rc("AdsGetRecord", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetRecordCRC ---- */
#define AdsGetRecordCRC oadsimpl_AdsGetRecordCRC
extern UNSIGNED32 ENTRYPOINT AdsGetRecordCRC(ADSHANDLE hTable, UNSIGNED32* pulCRC, UNSIGNED32 ulOptions);
#undef AdsGetRecordCRC
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetRecordCRC=_AdsGetRecordCRC")
#pragma comment(linker, "/export:AdsGetRecordCRC=_AdsGetRecordCRC")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetRecordCRC(ADSHANDLE a0, UNSIGNED32* a1, UNSIGNED32 a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetRecordCRC(a0, a1, a2);
    arc_log_rc("AdsGetRecordCRC", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetRecordCount ---- */
#define AdsGetRecordCount oadsimpl_AdsGetRecordCount
extern UNSIGNED32 ENTRYPOINT AdsGetRecordCount(ADSHANDLE hTable, UNSIGNED16 bFilterOption, UNSIGNED32* pulRecordCount);
#undef AdsGetRecordCount
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetRecordCount=_AdsGetRecordCount")
#pragma comment(linker, "/export:AdsGetRecordCount=_AdsGetRecordCount")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetRecordCount(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED32* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetRecordCount(a0, a1, a2);
    arc_log_rc("AdsGetRecordCount", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetRecordLength ---- */
#define AdsGetRecordLength oadsimpl_AdsGetRecordLength
extern UNSIGNED32 ENTRYPOINT AdsGetRecordLength(ADSHANDLE hTable, UNSIGNED32* pulLen);
#undef AdsGetRecordLength
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetRecordLength=_AdsGetRecordLength")
#pragma comment(linker, "/export:AdsGetRecordLength=_AdsGetRecordLength")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetRecordLength(ADSHANDLE a0, UNSIGNED32* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetRecordLength(a0, a1);
    arc_log_rc("AdsGetRecordLength", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetRecordNum ---- */
#define AdsGetRecordNum oadsimpl_AdsGetRecordNum
extern UNSIGNED32 ENTRYPOINT AdsGetRecordNum(ADSHANDLE hTable, UNSIGNED16 bFilterOption, UNSIGNED32* pulRecordNum);
#undef AdsGetRecordNum
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetRecordNum=_AdsGetRecordNum")
#pragma comment(linker, "/export:AdsGetRecordNum=_AdsGetRecordNum")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetRecordNum(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED32* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetRecordNum(a0, a1, a2);
    arc_log_rc("AdsGetRecordNum", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetRelKeyPos ---- */
#define AdsGetRelKeyPos oadsimpl_AdsGetRelKeyPos
extern UNSIGNED32 ENTRYPOINT AdsGetRelKeyPos(ADSHANDLE hIndex, double* pdPos);
#undef AdsGetRelKeyPos
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetRelKeyPos=_AdsGetRelKeyPos")
#pragma comment(linker, "/export:AdsGetRelKeyPos=_AdsGetRelKeyPos")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetRelKeyPos(ADSHANDLE a0, double* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetRelKeyPos(a0, a1);
    arc_log_rc("AdsGetRelKeyPos", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetScope ---- */
#define AdsGetScope oadsimpl_AdsGetScope
extern UNSIGNED32 ENTRYPOINT AdsGetScope(ADSHANDLE hIndex, UNSIGNED16 usScope, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetScope
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetScope=_AdsGetScope")
#pragma comment(linker, "/export:AdsGetScope=_AdsGetScope")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetScope(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    UNSIGNED32 rc = oadsimpl_AdsGetScope(a0, a1, a2, a3);
    arc_log_rc("AdsGetScope", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsGetSearchPath ---- */
#define AdsGetSearchPath oadsimpl_AdsGetSearchPath
extern UNSIGNED32 ENTRYPOINT AdsGetSearchPath(UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetSearchPath
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetSearchPath=_AdsGetSearchPath")
#pragma comment(linker, "/export:AdsGetSearchPath=_AdsGetSearchPath")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetSearchPath(UNSIGNED8* a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetSearchPath(a0, a1);
    arc_log_rc("AdsGetSearchPath", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetServerName ---- */
#define AdsGetServerName oadsimpl_AdsGetServerName
extern UNSIGNED32 ENTRYPOINT AdsGetServerName(ADSHANDLE hConnect, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetServerName
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetServerName=_AdsGetServerName")
#pragma comment(linker, "/export:AdsGetServerName=_AdsGetServerName")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetServerName(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetServerName(a0, a1, a2);
    arc_log_rc("AdsGetServerName", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetServerTime ---- */
#define AdsGetServerTime oadsimpl_AdsGetServerTime
extern UNSIGNED32 ENTRYPOINT AdsGetServerTime(ADSHANDLE hConnect, UNSIGNED8* pucDateBuf, UNSIGNED16* pusDateLen, SIGNED32* plTime, UNSIGNED8* pucTimeBuf, UNSIGNED16* pusTimeLen);
#undef AdsGetServerTime
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetServerTime=_AdsGetServerTime")
#pragma comment(linker, "/export:AdsGetServerTime=_AdsGetServerTime")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetServerTime(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2, SIGNED32* a3, UNSIGNED8* a4, UNSIGNED16* a5) {
    UNSIGNED32 rc = oadsimpl_AdsGetServerTime(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsGetServerTime", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsGetString ---- */
#define AdsGetString oadsimpl_AdsGetString
extern UNSIGNED32 ENTRYPOINT AdsGetString(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED8* pucBuf, UNSIGNED32* pulLen, UNSIGNED16 usOption);
#undef AdsGetString
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetString=_AdsGetString")
#pragma comment(linker, "/export:AdsGetString=_AdsGetString")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetString(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32* a3, UNSIGNED16 a4) {
    UNSIGNED32 rc = oadsimpl_AdsGetString(a0, a1, a2, a3, a4);
    arc_log_rc("AdsGetString", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsGetStringW ---- */
#define AdsGetStringW oadsimpl_AdsGetStringW
extern UNSIGNED32 ENTRYPOINT AdsGetStringW(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pucBufW, UNSIGNED32* pulLenW, UNSIGNED16 usOption);
#undef AdsGetStringW
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetStringW=_AdsGetStringW")
#pragma comment(linker, "/export:AdsGetStringW=_AdsGetStringW")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetStringW(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2, UNSIGNED32* a3, UNSIGNED16 a4) {
    UNSIGNED32 rc = oadsimpl_AdsGetStringW(a0, a1, a2, a3, a4);
    arc_log_rc("AdsGetStringW", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsGetTableAlias ---- */
#define AdsGetTableAlias oadsimpl_AdsGetTableAlias
extern UNSIGNED32 ENTRYPOINT AdsGetTableAlias(ADSHANDLE hTable, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetTableAlias
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableAlias=_AdsGetTableAlias")
#pragma comment(linker, "/export:AdsGetTableAlias=_AdsGetTableAlias")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableAlias(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetTableAlias(a0, a1, a2);
    arc_log_rc("AdsGetTableAlias", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetTableCharType ---- */
#define AdsGetTableCharType oadsimpl_AdsGetTableCharType
extern UNSIGNED32 ENTRYPOINT AdsGetTableCharType(ADSHANDLE hTable, UNSIGNED16* pusType);
#undef AdsGetTableCharType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableCharType=_AdsGetTableCharType")
#pragma comment(linker, "/export:AdsGetTableCharType=_AdsGetTableCharType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableCharType(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetTableCharType(a0, a1);
    arc_log_rc("AdsGetTableCharType", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetTableConType ---- */
#define AdsGetTableConType oadsimpl_AdsGetTableConType
extern UNSIGNED32 ENTRYPOINT AdsGetTableConType(ADSHANDLE hTable, UNSIGNED16* pusType);
#undef AdsGetTableConType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableConType=_AdsGetTableConType")
#pragma comment(linker, "/export:AdsGetTableConType=_AdsGetTableConType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableConType(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetTableConType(a0, a1);
    arc_log_rc("AdsGetTableConType", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetTableConnection ---- */
#define AdsGetTableConnection oadsimpl_AdsGetTableConnection
extern UNSIGNED32 ENTRYPOINT AdsGetTableConnection(ADSHANDLE hTable, ADSHANDLE* phConnect);
#undef AdsGetTableConnection
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableConnection=_AdsGetTableConnection")
#pragma comment(linker, "/export:AdsGetTableConnection=_AdsGetTableConnection")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableConnection(ADSHANDLE a0, ADSHANDLE* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetTableConnection(a0, a1);
    arc_log_rc("AdsGetTableConnection", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetTableFilename ---- */
#define AdsGetTableFilename oadsimpl_AdsGetTableFilename
extern UNSIGNED32 ENTRYPOINT AdsGetTableFilename(ADSHANDLE hTable, UNSIGNED16 usOption, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetTableFilename
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableFilename=_AdsGetTableFilename")
#pragma comment(linker, "/export:AdsGetTableFilename=_AdsGetTableFilename")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableFilename(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    UNSIGNED32 rc = oadsimpl_AdsGetTableFilename(a0, a1, a2, a3);
    arc_log_rc("AdsGetTableFilename", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsGetTableHandle25 ---- */
#define AdsGetTableHandle25 oadsimpl_AdsGetTableHandle25
extern UNSIGNED32 ENTRYPOINT AdsGetTableHandle25(ADSHANDLE hConnect, UNSIGNED8* pucName, ADSHANDLE* phTable);
#undef AdsGetTableHandle25
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableHandle25=_AdsGetTableHandle25")
#pragma comment(linker, "/export:AdsGetTableHandle25=_AdsGetTableHandle25")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableHandle25(ADSHANDLE a0, UNSIGNED8* a1, ADSHANDLE* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetTableHandle25(a0, a1, a2);
    arc_log_rc("AdsGetTableHandle25", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetTableLockType ---- */
#define AdsGetTableLockType oadsimpl_AdsGetTableLockType
extern UNSIGNED32 ENTRYPOINT AdsGetTableLockType(ADSHANDLE hTable, UNSIGNED16* pusLockType);
#undef AdsGetTableLockType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableLockType=_AdsGetTableLockType")
#pragma comment(linker, "/export:AdsGetTableLockType=_AdsGetTableLockType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableLockType(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetTableLockType(a0, a1);
    arc_log_rc("AdsGetTableLockType", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetTableOpenOptions ---- */
#define AdsGetTableOpenOptions oadsimpl_AdsGetTableOpenOptions
extern UNSIGNED32 ENTRYPOINT AdsGetTableOpenOptions(ADSHANDLE hTable, UNSIGNED32* pulOptions);
#undef AdsGetTableOpenOptions
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableOpenOptions=_AdsGetTableOpenOptions")
#pragma comment(linker, "/export:AdsGetTableOpenOptions=_AdsGetTableOpenOptions")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableOpenOptions(ADSHANDLE a0, UNSIGNED32* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetTableOpenOptions(a0, a1);
    arc_log_rc("AdsGetTableOpenOptions", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetTableType ---- */
#define AdsGetTableType oadsimpl_AdsGetTableType
extern UNSIGNED32 ENTRYPOINT AdsGetTableType(ADSHANDLE hTable, UNSIGNED16* pusType);
#undef AdsGetTableType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableType=_AdsGetTableType")
#pragma comment(linker, "/export:AdsGetTableType=_AdsGetTableType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableType(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetTableType(a0, a1);
    arc_log_rc("AdsGetTableType", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetVersion ---- */
#define AdsGetVersion oadsimpl_AdsGetVersion
extern UNSIGNED32 ENTRYPOINT AdsGetVersion(UNSIGNED32* pulMajor, UNSIGNED32* pulMinor, UNSIGNED8* pucLetter, UNSIGNED8* pucDesc, UNSIGNED16* pusDescLen);
#undef AdsGetVersion
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetVersion=_AdsGetVersion")
#pragma comment(linker, "/export:AdsGetVersion=_AdsGetVersion")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetVersion(UNSIGNED32* a0, UNSIGNED32* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED16* a4) {
    UNSIGNED32 rc = oadsimpl_AdsGetVersion(a0, a1, a2, a3, a4);
    arc_log_rc("AdsGetVersion", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsGotoBookmark60 ---- */
#define AdsGotoBookmark60 oadsimpl_AdsGotoBookmark60
extern UNSIGNED32 AdsGotoBookmark60(UNSIGNED8* a0, UNSIGNED8* a1);
#undef AdsGotoBookmark60
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGotoBookmark60=_AdsGotoBookmark60")
#pragma comment(linker, "/export:AdsGotoBookmark60=_AdsGotoBookmark60")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGotoBookmark60(UNSIGNED8* a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGotoBookmark60(a0, a1);
    arc_log_rc("AdsGotoBookmark60", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGotoBottom ---- */
#define AdsGotoBottom oadsimpl_AdsGotoBottom
extern UNSIGNED32 ENTRYPOINT AdsGotoBottom(ADSHANDLE hTable);
#undef AdsGotoBottom
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGotoBottom=_AdsGotoBottom")
#pragma comment(linker, "/export:AdsGotoBottom=_AdsGotoBottom")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGotoBottom(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsGotoBottom(a0);
    arc_log_rc("AdsGotoBottom", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsGotoRecord ---- */
#define AdsGotoRecord oadsimpl_AdsGotoRecord
extern UNSIGNED32 ENTRYPOINT AdsGotoRecord(ADSHANDLE hTable, UNSIGNED32 ulRecord);
#undef AdsGotoRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGotoRecord=_AdsGotoRecord")
#pragma comment(linker, "/export:AdsGotoRecord=_AdsGotoRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGotoRecord(ADSHANDLE a0, UNSIGNED32 a1) {
    UNSIGNED32 rc = oadsimpl_AdsGotoRecord(a0, a1);
    arc_log_rc("AdsGotoRecord", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGotoTop ---- */
#define AdsGotoTop oadsimpl_AdsGotoTop
extern UNSIGNED32 ENTRYPOINT AdsGotoTop(ADSHANDLE hTable);
#undef AdsGotoTop
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGotoTop=_AdsGotoTop")
#pragma comment(linker, "/export:AdsGotoTop=_AdsGotoTop")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGotoTop(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsGotoTop(a0);
    arc_log_rc("AdsGotoTop", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsInTransaction ---- */
#define AdsInTransaction oadsimpl_AdsInTransaction
extern UNSIGNED32 ENTRYPOINT AdsInTransaction(ADSHANDLE hConnect, UNSIGNED16* pbInTx);
#undef AdsInTransaction
#pragma comment(linker, "/alternatename:_oadsimpl_AdsInTransaction=_AdsInTransaction")
#pragma comment(linker, "/export:AdsInTransaction=_AdsInTransaction")
__declspec(dllexport) UNSIGNED32 __stdcall AdsInTransaction(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsInTransaction(a0, a1);
    arc_log_rc("AdsInTransaction", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsInitRawKey ---- */
#define AdsInitRawKey oadsimpl_AdsInitRawKey
extern UNSIGNED32 ENTRYPOINT AdsInitRawKey(ADSHANDLE hIndex);
#undef AdsInitRawKey
#pragma comment(linker, "/alternatename:_oadsimpl_AdsInitRawKey=_AdsInitRawKey")
#pragma comment(linker, "/export:AdsInitRawKey=_AdsInitRawKey")
__declspec(dllexport) UNSIGNED32 __stdcall AdsInitRawKey(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsInitRawKey(a0);
    arc_log_rc("AdsInitRawKey", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsIsConnectionAlive ---- */
#define AdsIsConnectionAlive oadsimpl_AdsIsConnectionAlive
extern UNSIGNED32 ENTRYPOINT AdsIsConnectionAlive(ADSHANDLE hConnect, UNSIGNED16* pbAlive);
#undef AdsIsConnectionAlive
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsConnectionAlive=_AdsIsConnectionAlive")
#pragma comment(linker, "/export:AdsIsConnectionAlive=_AdsIsConnectionAlive")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsConnectionAlive(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsIsConnectionAlive(a0, a1);
    arc_log_rc("AdsIsConnectionAlive", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsIsEmpty ---- */
#define AdsIsEmpty oadsimpl_AdsIsEmpty
extern UNSIGNED32 ENTRYPOINT AdsIsEmpty(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pbEmpty);
#undef AdsIsEmpty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsEmpty=_AdsIsEmpty")
#pragma comment(linker, "/export:AdsIsEmpty=_AdsIsEmpty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsEmpty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsIsEmpty(a0, a1, a2);
    arc_log_rc("AdsIsEmpty", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsIsEncryptionEnabled ---- */
#define AdsIsEncryptionEnabled oadsimpl_AdsIsEncryptionEnabled
extern UNSIGNED32 ENTRYPOINT AdsIsEncryptionEnabled(ADSHANDLE hConnect, UNSIGNED16* pbEnabled);
#undef AdsIsEncryptionEnabled
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsEncryptionEnabled=_AdsIsEncryptionEnabled")
#pragma comment(linker, "/export:AdsIsEncryptionEnabled=_AdsIsEncryptionEnabled")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsEncryptionEnabled(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsIsEncryptionEnabled(a0, a1);
    arc_log_rc("AdsIsEncryptionEnabled", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsIsExprValid ---- */
#define AdsIsExprValid oadsimpl_AdsIsExprValid
extern UNSIGNED32 ENTRYPOINT AdsIsExprValid(ADSHANDLE hTable, UNSIGNED8* pucExpr, UNSIGNED16* pbValid);
#undef AdsIsExprValid
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsExprValid=_AdsIsExprValid")
#pragma comment(linker, "/export:AdsIsExprValid=_AdsIsExprValid")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsExprValid(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsIsExprValid(a0, a1, a2);
    arc_log_rc("AdsIsExprValid", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsIsFound ---- */
#define AdsIsFound oadsimpl_AdsIsFound
extern UNSIGNED32 ENTRYPOINT AdsIsFound(ADSHANDLE hTable, UNSIGNED16* pbFound);
#undef AdsIsFound
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsFound=_AdsIsFound")
#pragma comment(linker, "/export:AdsIsFound=_AdsIsFound")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsFound(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsIsFound(a0, a1);
    arc_log_rc("AdsIsFound", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsIsIndexCustom ---- */
#define AdsIsIndexCustom oadsimpl_AdsIsIndexCustom
extern UNSIGNED32 ENTRYPOINT AdsIsIndexCustom(ADSHANDLE hIndex, UNSIGNED16* pbCustom);
#undef AdsIsIndexCustom
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsIndexCustom=_AdsIsIndexCustom")
#pragma comment(linker, "/export:AdsIsIndexCustom=_AdsIsIndexCustom")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsIndexCustom(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsIsIndexCustom(a0, a1);
    arc_log_rc("AdsIsIndexCustom", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsIsIndexDescending ---- */
#define AdsIsIndexDescending oadsimpl_AdsIsIndexDescending
extern UNSIGNED32 ENTRYPOINT AdsIsIndexDescending(ADSHANDLE hIndex, UNSIGNED16* pbDesc);
#undef AdsIsIndexDescending
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsIndexDescending=_AdsIsIndexDescending")
#pragma comment(linker, "/export:AdsIsIndexDescending=_AdsIsIndexDescending")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsIndexDescending(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsIsIndexDescending(a0, a1);
    arc_log_rc("AdsIsIndexDescending", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsIsIndexUnique ---- */
#define AdsIsIndexUnique oadsimpl_AdsIsIndexUnique
extern UNSIGNED32 ENTRYPOINT AdsIsIndexUnique(ADSHANDLE hIndex, UNSIGNED16* pbUnique);
#undef AdsIsIndexUnique
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsIndexUnique=_AdsIsIndexUnique")
#pragma comment(linker, "/export:AdsIsIndexUnique=_AdsIsIndexUnique")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsIndexUnique(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsIsIndexUnique(a0, a1);
    arc_log_rc("AdsIsIndexUnique", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsIsNull ---- */
#define AdsIsNull oadsimpl_AdsIsNull
extern UNSIGNED32 ENTRYPOINT AdsIsNull(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pbNull);
#undef AdsIsNull
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsNull=_AdsIsNull")
#pragma comment(linker, "/export:AdsIsNull=_AdsIsNull")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsNull(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsIsNull(a0, a1, a2);
    arc_log_rc("AdsIsNull", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsIsNullable ---- */
#define AdsIsNullable oadsimpl_AdsIsNullable
extern UNSIGNED32 ENTRYPOINT AdsIsNullable(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pbNullable);
#undef AdsIsNullable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsNullable=_AdsIsNullable")
#pragma comment(linker, "/export:AdsIsNullable=_AdsIsNullable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsNullable(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsIsNullable(a0, a1, a2);
    arc_log_rc("AdsIsNullable", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsIsRecordDeleted ---- */
#define AdsIsRecordDeleted oadsimpl_AdsIsRecordDeleted
extern UNSIGNED32 ENTRYPOINT AdsIsRecordDeleted(ADSHANDLE hTable, UNSIGNED16* pbDeleted);
#undef AdsIsRecordDeleted
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsRecordDeleted=_AdsIsRecordDeleted")
#pragma comment(linker, "/export:AdsIsRecordDeleted=_AdsIsRecordDeleted")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsRecordDeleted(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsIsRecordDeleted(a0, a1);
    arc_log_rc("AdsIsRecordDeleted", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsIsRecordEncrypted ---- */
#define AdsIsRecordEncrypted oadsimpl_AdsIsRecordEncrypted
extern UNSIGNED32 ENTRYPOINT AdsIsRecordEncrypted(ADSHANDLE hTable, UNSIGNED16* pbEncrypted);
#undef AdsIsRecordEncrypted
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsRecordEncrypted=_AdsIsRecordEncrypted")
#pragma comment(linker, "/export:AdsIsRecordEncrypted=_AdsIsRecordEncrypted")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsRecordEncrypted(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsIsRecordEncrypted(a0, a1);
    arc_log_rc("AdsIsRecordEncrypted", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsIsRecordInAOF ---- */
#define AdsIsRecordInAOF oadsimpl_AdsIsRecordInAOF
extern UNSIGNED32 ENTRYPOINT AdsIsRecordInAOF(ADSHANDLE hTable, UNSIGNED32 ulRecord, UNSIGNED16* pbInAOF);
#undef AdsIsRecordInAOF
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsRecordInAOF=_AdsIsRecordInAOF")
#pragma comment(linker, "/export:AdsIsRecordInAOF=_AdsIsRecordInAOF")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsRecordInAOF(ADSHANDLE a0, UNSIGNED32 a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsIsRecordInAOF(a0, a1, a2);
    arc_log_rc("AdsIsRecordInAOF", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsIsRecordLocked ---- */
#define AdsIsRecordLocked oadsimpl_AdsIsRecordLocked
extern UNSIGNED32 ENTRYPOINT AdsIsRecordLocked(ADSHANDLE hTable, UNSIGNED32 ulRecord, UNSIGNED16* pbLocked);
#undef AdsIsRecordLocked
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsRecordLocked=_AdsIsRecordLocked")
#pragma comment(linker, "/export:AdsIsRecordLocked=_AdsIsRecordLocked")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsRecordLocked(ADSHANDLE a0, UNSIGNED32 a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsIsRecordLocked(a0, a1, a2);
    arc_log_rc("AdsIsRecordLocked", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsIsRecordVisible ---- */
#define AdsIsRecordVisible oadsimpl_AdsIsRecordVisible
extern UNSIGNED32 ENTRYPOINT AdsIsRecordVisible(ADSHANDLE hObj, UNSIGNED16* pbVisible);
#undef AdsIsRecordVisible
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsRecordVisible=_AdsIsRecordVisible")
#pragma comment(linker, "/export:AdsIsRecordVisible=_AdsIsRecordVisible")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsRecordVisible(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsIsRecordVisible(a0, a1);
    arc_log_rc("AdsIsRecordVisible", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsIsServerLoaded ---- */
#define AdsIsServerLoaded oadsimpl_AdsIsServerLoaded
extern UNSIGNED32 ENTRYPOINT AdsIsServerLoaded(UNSIGNED8* pucServer, UNSIGNED16* pbLoaded);
#undef AdsIsServerLoaded
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsServerLoaded=_AdsIsServerLoaded")
#pragma comment(linker, "/export:AdsIsServerLoaded=_AdsIsServerLoaded")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsServerLoaded(UNSIGNED8* a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsIsServerLoaded(a0, a1);
    arc_log_rc("AdsIsServerLoaded", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsIsTableEncrypted ---- */
#define AdsIsTableEncrypted oadsimpl_AdsIsTableEncrypted
extern UNSIGNED32 ENTRYPOINT AdsIsTableEncrypted(ADSHANDLE hTable, UNSIGNED16* pbEncrypted);
#undef AdsIsTableEncrypted
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsTableEncrypted=_AdsIsTableEncrypted")
#pragma comment(linker, "/export:AdsIsTableEncrypted=_AdsIsTableEncrypted")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsTableEncrypted(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsIsTableEncrypted(a0, a1);
    arc_log_rc("AdsIsTableEncrypted", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsIsTableLocked ---- */
#define AdsIsTableLocked oadsimpl_AdsIsTableLocked
extern UNSIGNED32 ENTRYPOINT AdsIsTableLocked(ADSHANDLE hTable, UNSIGNED16* pbLocked);
#undef AdsIsTableLocked
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsTableLocked=_AdsIsTableLocked")
#pragma comment(linker, "/export:AdsIsTableLocked=_AdsIsTableLocked")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsTableLocked(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsIsTableLocked(a0, a1);
    arc_log_rc("AdsIsTableLocked", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsLockRecord ---- */
#define AdsLockRecord oadsimpl_AdsLockRecord
extern UNSIGNED32 ENTRYPOINT AdsLockRecord(ADSHANDLE hTable, UNSIGNED32 ulRecord);
#undef AdsLockRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsLockRecord=_AdsLockRecord")
#pragma comment(linker, "/export:AdsLockRecord=_AdsLockRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsLockRecord(ADSHANDLE a0, UNSIGNED32 a1) {
    UNSIGNED32 rc = oadsimpl_AdsLockRecord(a0, a1);
    arc_log_rc("AdsLockRecord", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsLockTable ---- */
#define AdsLockTable oadsimpl_AdsLockTable
extern UNSIGNED32 ENTRYPOINT AdsLockTable(ADSHANDLE hTable);
#undef AdsLockTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsLockTable=_AdsLockTable")
#pragma comment(linker, "/export:AdsLockTable=_AdsLockTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsLockTable(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsLockTable(a0);
    arc_log_rc("AdsLockTable", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsMgConnect ---- */
#define AdsMgConnect oadsimpl_AdsMgConnect
extern UNSIGNED32 ENTRYPOINT AdsMgConnect(UNSIGNED8* pucServer, UNSIGNED8* pucUser, UNSIGNED8* pucPwd, ADSHANDLE* phMg);
#undef AdsMgConnect
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgConnect=_AdsMgConnect")
#pragma comment(linker, "/export:AdsMgConnect=_AdsMgConnect")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgConnect(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, ADSHANDLE* a3) {
    UNSIGNED32 rc = oadsimpl_AdsMgConnect(a0, a1, a2, a3);
    arc_log_rc("AdsMgConnect", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsMgDisconnect ---- */
#define AdsMgDisconnect oadsimpl_AdsMgDisconnect
extern UNSIGNED32 ENTRYPOINT AdsMgDisconnect(ADSHANDLE hMg);
#undef AdsMgDisconnect
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgDisconnect=_AdsMgDisconnect")
#pragma comment(linker, "/export:AdsMgDisconnect=_AdsMgDisconnect")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgDisconnect(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsMgDisconnect(a0);
    arc_log_rc("AdsMgDisconnect", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsMgDumpInternalTables ---- */
#define AdsMgDumpInternalTables oadsimpl_AdsMgDumpInternalTables
extern UNSIGNED32 ENTRYPOINT AdsMgDumpInternalTables(ADSHANDLE hMgmtHandle);
#undef AdsMgDumpInternalTables
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgDumpInternalTables=_AdsMgDumpInternalTables")
#pragma comment(linker, "/export:AdsMgDumpInternalTables=_AdsMgDumpInternalTables")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgDumpInternalTables(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsMgDumpInternalTables(a0);
    arc_log_rc("AdsMgDumpInternalTables", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsMgGetActivityInfo ---- */
#define AdsMgGetActivityInfo oadsimpl_AdsMgGetActivityInfo
extern UNSIGNED32 ENTRYPOINT AdsMgGetActivityInfo(ADSHANDLE hMg, void* pInfo, UNSIGNED16* pusSize);
#undef AdsMgGetActivityInfo
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetActivityInfo=_AdsMgGetActivityInfo")
#pragma comment(linker, "/export:AdsMgGetActivityInfo=_AdsMgGetActivityInfo")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetActivityInfo(ADSHANDLE a0, void* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsMgGetActivityInfo(a0, a1, a2);
    arc_log_rc("AdsMgGetActivityInfo", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsMgGetCommStats ---- */
#define AdsMgGetCommStats oadsimpl_AdsMgGetCommStats
extern UNSIGNED32 ENTRYPOINT AdsMgGetCommStats(ADSHANDLE hMg, void* pInfo, UNSIGNED16* pusSize);
#undef AdsMgGetCommStats
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetCommStats=_AdsMgGetCommStats")
#pragma comment(linker, "/export:AdsMgGetCommStats=_AdsMgGetCommStats")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetCommStats(ADSHANDLE a0, void* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsMgGetCommStats(a0, a1, a2);
    arc_log_rc("AdsMgGetCommStats", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsMgGetConfigInfo ---- */
#define AdsMgGetConfigInfo oadsimpl_AdsMgGetConfigInfo
extern UNSIGNED32 ENTRYPOINT AdsMgGetConfigInfo(ADSHANDLE hMg, void* pVals, UNSIGNED16* pusValsSize, void* pMem, UNSIGNED16* pusMemSize);
#undef AdsMgGetConfigInfo
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetConfigInfo=_AdsMgGetConfigInfo")
#pragma comment(linker, "/export:AdsMgGetConfigInfo=_AdsMgGetConfigInfo")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetConfigInfo(ADSHANDLE a0, void* a1, UNSIGNED16* a2, void* a3, UNSIGNED16* a4) {
    UNSIGNED32 rc = oadsimpl_AdsMgGetConfigInfo(a0, a1, a2, a3, a4);
    arc_log_rc("AdsMgGetConfigInfo", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsMgGetInstallInfo ---- */
#define AdsMgGetInstallInfo oadsimpl_AdsMgGetInstallInfo
extern UNSIGNED32 ENTRYPOINT AdsMgGetInstallInfo(ADSHANDLE hMg, void* pInfo, UNSIGNED16* pusSize);
#undef AdsMgGetInstallInfo
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetInstallInfo=_AdsMgGetInstallInfo")
#pragma comment(linker, "/export:AdsMgGetInstallInfo=_AdsMgGetInstallInfo")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetInstallInfo(ADSHANDLE a0, void* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsMgGetInstallInfo(a0, a1, a2);
    arc_log_rc("AdsMgGetInstallInfo", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsMgGetLockOwner ---- */
#define AdsMgGetLockOwner oadsimpl_AdsMgGetLockOwner
extern UNSIGNED32 ENTRYPOINT AdsMgGetLockOwner(ADSHANDLE hMg, UNSIGNED8* pucTable, UNSIGNED32 ulRecord, void* pInfo, UNSIGNED16* pusSize, UNSIGNED16* pusLockType);
#undef AdsMgGetLockOwner
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetLockOwner=_AdsMgGetLockOwner")
#pragma comment(linker, "/export:AdsMgGetLockOwner=_AdsMgGetLockOwner")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetLockOwner(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32 a2, void* a3, UNSIGNED16* a4, UNSIGNED16* a5) {
    UNSIGNED32 rc = oadsimpl_AdsMgGetLockOwner(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsMgGetLockOwner", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsMgGetLocks ---- */
#define AdsMgGetLocks oadsimpl_AdsMgGetLocks
extern UNSIGNED32 ENTRYPOINT AdsMgGetLocks(ADSHANDLE hMg, UNSIGNED8* pucTable, UNSIGNED8* pucUser, UNSIGNED16 usConnNumber, void* pInfo, UNSIGNED16* pusCount, UNSIGNED16* pusSize);
#undef AdsMgGetLocks
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetLocks=_AdsMgGetLocks")
#pragma comment(linker, "/export:AdsMgGetLocks=_AdsMgGetLocks")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetLocks(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, void* a4, UNSIGNED16* a5, UNSIGNED16* a6) {
    UNSIGNED32 rc = oadsimpl_AdsMgGetLocks(a0, a1, a2, a3, a4, a5, a6);
    arc_log_rc("AdsMgGetLocks", rc, 7, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6);
    return rc;
}

/* ---- AdsMgGetOpenIndexes ---- */
#define AdsMgGetOpenIndexes oadsimpl_AdsMgGetOpenIndexes
extern UNSIGNED32 ENTRYPOINT AdsMgGetOpenIndexes(ADSHANDLE hMg, UNSIGNED8* pucTable, UNSIGNED8* pucUser, UNSIGNED16 usConnNumber, void* pInfo, UNSIGNED16* pusCount, UNSIGNED16* pusSize);
#undef AdsMgGetOpenIndexes
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetOpenIndexes=_AdsMgGetOpenIndexes")
#pragma comment(linker, "/export:AdsMgGetOpenIndexes=_AdsMgGetOpenIndexes")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetOpenIndexes(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, void* a4, UNSIGNED16* a5, UNSIGNED16* a6) {
    UNSIGNED32 rc = oadsimpl_AdsMgGetOpenIndexes(a0, a1, a2, a3, a4, a5, a6);
    arc_log_rc("AdsMgGetOpenIndexes", rc, 7, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6);
    return rc;
}

/* ---- AdsMgGetOpenTables ---- */
#define AdsMgGetOpenTables oadsimpl_AdsMgGetOpenTables
extern UNSIGNED32 ENTRYPOINT AdsMgGetOpenTables(ADSHANDLE hMg, UNSIGNED8* pucUser, UNSIGNED16 usConnNumber, void* pInfo, UNSIGNED16* pusCount, UNSIGNED16* pusSize);
#undef AdsMgGetOpenTables
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetOpenTables=_AdsMgGetOpenTables")
#pragma comment(linker, "/export:AdsMgGetOpenTables=_AdsMgGetOpenTables")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetOpenTables(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16* a4, UNSIGNED16* a5) {
    UNSIGNED32 rc = oadsimpl_AdsMgGetOpenTables(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsMgGetOpenTables", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsMgGetOpenTables2 ---- */
#define AdsMgGetOpenTables2 oadsimpl_AdsMgGetOpenTables2
extern UNSIGNED32 ENTRYPOINT AdsMgGetOpenTables2(ADSHANDLE hMg, UNSIGNED8* pucUser, UNSIGNED16 usConnNumber, void* pInfo, UNSIGNED16* pusCount, UNSIGNED16* pusSize);
#undef AdsMgGetOpenTables2
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetOpenTables2=_AdsMgGetOpenTables2")
#pragma comment(linker, "/export:AdsMgGetOpenTables2=_AdsMgGetOpenTables2")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetOpenTables2(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16* a4, UNSIGNED16* a5) {
    UNSIGNED32 rc = oadsimpl_AdsMgGetOpenTables2(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsMgGetOpenTables2", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsMgGetServerType ---- */
#define AdsMgGetServerType oadsimpl_AdsMgGetServerType
extern UNSIGNED32 ENTRYPOINT AdsMgGetServerType(ADSHANDLE hMg, UNSIGNED16* pusT);
#undef AdsMgGetServerType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetServerType=_AdsMgGetServerType")
#pragma comment(linker, "/export:AdsMgGetServerType=_AdsMgGetServerType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetServerType(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsMgGetServerType(a0, a1);
    arc_log_rc("AdsMgGetServerType", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsMgGetThreadSql ---- */
#define AdsMgGetThreadSql oadsimpl_AdsMgGetThreadSql
extern UNSIGNED32 ENTRYPOINT AdsMgGetThreadSql(ADSHANDLE hMg, UNSIGNED32 ulThreadNumber, UNSIGNED8* pucBuf, UNSIGNED32* pulLen, UNSIGNED64* pullStartEpoch);
#undef AdsMgGetThreadSql
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetThreadSql=_AdsMgGetThreadSql")
#pragma comment(linker, "/export:AdsMgGetThreadSql=_AdsMgGetThreadSql")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetThreadSql(ADSHANDLE a0, UNSIGNED32 a1, UNSIGNED8* a2, UNSIGNED32* a3, UNSIGNED64* a4) {
    UNSIGNED32 rc = oadsimpl_AdsMgGetThreadSql(a0, a1, a2, a3, a4);
    arc_log_rc("AdsMgGetThreadSql", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsMgGetUserAvgCost ---- */
#define AdsMgGetUserAvgCost oadsimpl_AdsMgGetUserAvgCost
extern UNSIGNED32 ENTRYPOINT AdsMgGetUserAvgCost(ADSHANDLE hMg, UNSIGNED32* pulCosts, UNSIGNED16* pusCount, UNSIGNED16* pusSize);
#undef AdsMgGetUserAvgCost
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetUserAvgCost=_AdsMgGetUserAvgCost")
#pragma comment(linker, "/export:AdsMgGetUserAvgCost=_AdsMgGetUserAvgCost")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetUserAvgCost(ADSHANDLE a0, UNSIGNED32* a1, UNSIGNED16* a2, UNSIGNED16* a3) {
    UNSIGNED32 rc = oadsimpl_AdsMgGetUserAvgCost(a0, a1, a2, a3);
    arc_log_rc("AdsMgGetUserAvgCost", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsMgGetUserNames ---- */
#define AdsMgGetUserNames oadsimpl_AdsMgGetUserNames
extern UNSIGNED32 ENTRYPOINT AdsMgGetUserNames(ADSHANDLE hMg, UNSIGNED8* pucFile, void* pInfo, UNSIGNED16* pusCount, UNSIGNED16* pusSize);
#undef AdsMgGetUserNames
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetUserNames=_AdsMgGetUserNames")
#pragma comment(linker, "/export:AdsMgGetUserNames=_AdsMgGetUserNames")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetUserNames(ADSHANDLE a0, UNSIGNED8* a1, void* a2, UNSIGNED16* a3, UNSIGNED16* a4) {
    UNSIGNED32 rc = oadsimpl_AdsMgGetUserNames(a0, a1, a2, a3, a4);
    arc_log_rc("AdsMgGetUserNames", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsMgGetWorkerThreadActivity ---- */
#define AdsMgGetWorkerThreadActivity oadsimpl_AdsMgGetWorkerThreadActivity
extern UNSIGNED32 ENTRYPOINT AdsMgGetWorkerThreadActivity(ADSHANDLE hMg, void* pInfo, UNSIGNED16* pusCount, UNSIGNED16* pusSize);
#undef AdsMgGetWorkerThreadActivity
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetWorkerThreadActivity=_AdsMgGetWorkerThreadActivity")
#pragma comment(linker, "/export:AdsMgGetWorkerThreadActivity=_AdsMgGetWorkerThreadActivity")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetWorkerThreadActivity(ADSHANDLE a0, void* a1, UNSIGNED16* a2, UNSIGNED16* a3) {
    UNSIGNED32 rc = oadsimpl_AdsMgGetWorkerThreadActivity(a0, a1, a2, a3);
    arc_log_rc("AdsMgGetWorkerThreadActivity", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsMgKillUser ---- */
#define AdsMgKillUser oadsimpl_AdsMgKillUser
extern UNSIGNED32 ENTRYPOINT AdsMgKillUser(ADSHANDLE hMg, UNSIGNED8* pucUser, UNSIGNED16 usOption);
#undef AdsMgKillUser
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgKillUser=_AdsMgKillUser")
#pragma comment(linker, "/export:AdsMgKillUser=_AdsMgKillUser")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgKillUser(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2) {
    UNSIGNED32 rc = oadsimpl_AdsMgKillUser(a0, a1, a2);
    arc_log_rc("AdsMgKillUser", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsMgResetCommStats ---- */
#define AdsMgResetCommStats oadsimpl_AdsMgResetCommStats
extern UNSIGNED32 ENTRYPOINT AdsMgResetCommStats(ADSHANDLE hMg);
#undef AdsMgResetCommStats
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgResetCommStats=_AdsMgResetCommStats")
#pragma comment(linker, "/export:AdsMgResetCommStats=_AdsMgResetCommStats")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgResetCommStats(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsMgResetCommStats(a0);
    arc_log_rc("AdsMgResetCommStats", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsOpenIndex ---- */
#define AdsOpenIndex oadsimpl_AdsOpenIndex
extern UNSIGNED32 ENTRYPOINT AdsOpenIndex(ADSHANDLE hTable, UNSIGNED8* pucName, ADSHANDLE* ahIndex, UNSIGNED16* pu16ArrayLen);
#undef AdsOpenIndex
#pragma comment(linker, "/alternatename:_oadsimpl_AdsOpenIndex=_AdsOpenIndex")
#pragma comment(linker, "/export:AdsOpenIndex=_AdsOpenIndex")
__declspec(dllexport) UNSIGNED32 __stdcall AdsOpenIndex(ADSHANDLE a0, UNSIGNED8* a1, ADSHANDLE* a2, UNSIGNED16* a3) {
    UNSIGNED32 rc = oadsimpl_AdsOpenIndex(a0, a1, a2, a3);
    arc_log_rc("AdsOpenIndex", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsOpenTable ---- */
#define AdsOpenTable oadsimpl_AdsOpenTable
extern UNSIGNED32 ENTRYPOINT AdsOpenTable(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucAlias, UNSIGNED16 usTableType, UNSIGNED16 usCharType, UNSIGNED16 usLockType, UNSIGNED16 usCheckRights, UNSIGNED16 usMode, ADSHANDLE* phTable);
#undef AdsOpenTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsOpenTable=_AdsOpenTable")
#pragma comment(linker, "/export:AdsOpenTable=_AdsOpenTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsOpenTable(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, UNSIGNED16 a4, UNSIGNED16 a5, UNSIGNED16 a6, UNSIGNED16 a7, ADSHANDLE* a8) {
    UNSIGNED32 rc = oadsimpl_AdsOpenTable(a0, a1, a2, a3, a4, a5, a6, a7, a8);
    arc_log_rc("AdsOpenTable", rc, 9, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7, (uintptr_t)a8);
    return rc;
}

/* ---- AdsPackTable ---- */
#define AdsPackTable oadsimpl_AdsPackTable
extern UNSIGNED32 ENTRYPOINT AdsPackTable(ADSHANDLE hTable);
#undef AdsPackTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsPackTable=_AdsPackTable")
#pragma comment(linker, "/export:AdsPackTable=_AdsPackTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsPackTable(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsPackTable(a0);
    arc_log_rc("AdsPackTable", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsPackTable120 ---- */
#define AdsPackTable120 oadsimpl_AdsPackTable120
extern UNSIGNED32 ENTRYPOINT AdsPackTable120(ADSHANDLE hTable, UNSIGNED32 ulMemoBlockSize, UNSIGNED32 ulOptions);
#undef AdsPackTable120
#pragma comment(linker, "/alternatename:_oadsimpl_AdsPackTable120=_AdsPackTable120")
#pragma comment(linker, "/export:AdsPackTable120=_AdsPackTable120")
__declspec(dllexport) UNSIGNED32 __stdcall AdsPackTable120(ADSHANDLE a0, UNSIGNED32 a1, UNSIGNED32 a2) {
    UNSIGNED32 rc = oadsimpl_AdsPackTable120(a0, a1, a2);
    arc_log_rc("AdsPackTable120", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsPrepareSQL ---- */
#define AdsPrepareSQL oadsimpl_AdsPrepareSQL
extern UNSIGNED32 ENTRYPOINT AdsPrepareSQL(ADSHANDLE hStatement, UNSIGNED8* pucSQL);
#undef AdsPrepareSQL
#pragma comment(linker, "/alternatename:_oadsimpl_AdsPrepareSQL=_AdsPrepareSQL")
#pragma comment(linker, "/export:AdsPrepareSQL=_AdsPrepareSQL")
__declspec(dllexport) UNSIGNED32 __stdcall AdsPrepareSQL(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsPrepareSQL(a0, a1);
    arc_log_rc("AdsPrepareSQL", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsPrepareSQLW ---- */
#define AdsPrepareSQLW oadsimpl_AdsPrepareSQLW
extern UNSIGNED32 ENTRYPOINT AdsPrepareSQLW(ADSHANDLE hStatement, UNSIGNED16* pwcSQL);
#undef AdsPrepareSQLW
#pragma comment(linker, "/alternatename:_oadsimpl_AdsPrepareSQLW=_AdsPrepareSQLW")
#pragma comment(linker, "/export:AdsPrepareSQLW=_AdsPrepareSQLW")
__declspec(dllexport) UNSIGNED32 __stdcall AdsPrepareSQLW(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsPrepareSQLW(a0, a1);
    arc_log_rc("AdsPrepareSQLW", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsRecallAllRecords ---- */
#define AdsRecallAllRecords oadsimpl_AdsRecallAllRecords
extern UNSIGNED32 ENTRYPOINT AdsRecallAllRecords(ADSHANDLE hTable);
#undef AdsRecallAllRecords
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRecallAllRecords=_AdsRecallAllRecords")
#pragma comment(linker, "/export:AdsRecallAllRecords=_AdsRecallAllRecords")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRecallAllRecords(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsRecallAllRecords(a0);
    arc_log_rc("AdsRecallAllRecords", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsRecallRecord ---- */
#define AdsRecallRecord oadsimpl_AdsRecallRecord
extern UNSIGNED32 ENTRYPOINT AdsRecallRecord(ADSHANDLE hTable);
#undef AdsRecallRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRecallRecord=_AdsRecallRecord")
#pragma comment(linker, "/export:AdsRecallRecord=_AdsRecallRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRecallRecord(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsRecallRecord(a0);
    arc_log_rc("AdsRecallRecord", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsRefreshAOF ---- */
#define AdsRefreshAOF oadsimpl_AdsRefreshAOF
extern UNSIGNED32 ENTRYPOINT AdsRefreshAOF(ADSHANDLE hTable);
#undef AdsRefreshAOF
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRefreshAOF=_AdsRefreshAOF")
#pragma comment(linker, "/export:AdsRefreshAOF=_AdsRefreshAOF")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRefreshAOF(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsRefreshAOF(a0);
    arc_log_rc("AdsRefreshAOF", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsRefreshRecord ---- */
#define AdsRefreshRecord oadsimpl_AdsRefreshRecord
extern UNSIGNED32 ENTRYPOINT AdsRefreshRecord(ADSHANDLE hTable);
#undef AdsRefreshRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRefreshRecord=_AdsRefreshRecord")
#pragma comment(linker, "/export:AdsRefreshRecord=_AdsRefreshRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRefreshRecord(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsRefreshRecord(a0);
    arc_log_rc("AdsRefreshRecord", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsRegisterCallbackFunction ---- */
#define AdsRegisterCallbackFunction oadsimpl_AdsRegisterCallbackFunction
extern UNSIGNED32 ENTRYPOINT AdsRegisterCallbackFunction(void* pCallback, ADSHANDLE hTable);
#undef AdsRegisterCallbackFunction
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRegisterCallbackFunction=_AdsRegisterCallbackFunction")
#pragma comment(linker, "/export:AdsRegisterCallbackFunction=_AdsRegisterCallbackFunction")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRegisterCallbackFunction(void* pCallback, ADSHANDLE hTable) {
    UNSIGNED32 rc = oadsimpl_AdsRegisterCallbackFunction(pCallback, hTable);
    arc_log_rc("AdsRegisterCallbackFunction", rc, 2, (uintptr_t)pCallback, (uintptr_t)hTable);
    return rc;
}

/* ---- AdsRegisterProgressCallback ---- */
#define AdsRegisterProgressCallback oadsimpl_AdsRegisterProgressCallback
extern UNSIGNED32 ENTRYPOINT AdsRegisterProgressCallback(void* pCallback);
#undef AdsRegisterProgressCallback
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRegisterProgressCallback=_AdsRegisterProgressCallback")
#pragma comment(linker, "/export:AdsRegisterProgressCallback=_AdsRegisterProgressCallback")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRegisterProgressCallback(void* a0) {
    UNSIGNED32 rc = oadsimpl_AdsRegisterProgressCallback(a0);
    arc_log_rc("AdsRegisterProgressCallback", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsReindex ---- */
#define AdsReindex oadsimpl_AdsReindex
extern UNSIGNED32 ENTRYPOINT AdsReindex(ADSHANDLE hTable);
#undef AdsReindex
#pragma comment(linker, "/alternatename:_oadsimpl_AdsReindex=_AdsReindex")
#pragma comment(linker, "/export:AdsReindex=_AdsReindex")
__declspec(dllexport) UNSIGNED32 __stdcall AdsReindex(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsReindex(a0);
    arc_log_rc("AdsReindex", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsReindex61 ---- */
#define AdsReindex61 oadsimpl_AdsReindex61
extern UNSIGNED32 ENTRYPOINT AdsReindex61(ADSHANDLE hObject, UNSIGNED32 ulPageSize);
#undef AdsReindex61
#pragma comment(linker, "/alternatename:_oadsimpl_AdsReindex61=_AdsReindex61")
#pragma comment(linker, "/export:AdsReindex61=_AdsReindex61")
__declspec(dllexport) UNSIGNED32 __stdcall AdsReindex61(ADSHANDLE a0, UNSIGNED32 a1) {
    UNSIGNED32 rc = oadsimpl_AdsReindex61(a0, a1);
    arc_log_rc("AdsReindex61", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsReleaseSavepoint ---- */
#define AdsReleaseSavepoint oadsimpl_AdsReleaseSavepoint
extern UNSIGNED32 ENTRYPOINT AdsReleaseSavepoint(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsReleaseSavepoint
#pragma comment(linker, "/alternatename:_oadsimpl_AdsReleaseSavepoint=_AdsReleaseSavepoint")
#pragma comment(linker, "/export:AdsReleaseSavepoint=_AdsReleaseSavepoint")
__declspec(dllexport) UNSIGNED32 __stdcall AdsReleaseSavepoint(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsReleaseSavepoint(a0, a1);
    arc_log_rc("AdsReleaseSavepoint", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsRenameFile ---- */
#define AdsRenameFile oadsimpl_AdsRenameFile
extern UNSIGNED32 ENTRYPOINT AdsRenameFile(ADSHANDLE hConnect, UNSIGNED8* pucOld, UNSIGNED8* pucNew);
#undef AdsRenameFile
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRenameFile=_AdsRenameFile")
#pragma comment(linker, "/export:AdsRenameFile=_AdsRenameFile")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRenameFile(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsRenameFile(a0, a1, a2);
    arc_log_rc("AdsRenameFile", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsResetConnection ---- */
#define AdsResetConnection oadsimpl_AdsResetConnection
extern UNSIGNED32 ENTRYPOINT AdsResetConnection(ADSHANDLE hConnect);
#undef AdsResetConnection
#pragma comment(linker, "/alternatename:_oadsimpl_AdsResetConnection=_AdsResetConnection")
#pragma comment(linker, "/export:AdsResetConnection=_AdsResetConnection")
__declspec(dllexport) UNSIGNED32 __stdcall AdsResetConnection(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsResetConnection(a0);
    arc_log_rc("AdsResetConnection", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsRestructureTable ---- */
#define AdsRestructureTable oadsimpl_AdsRestructureTable
extern UNSIGNED32 ENTRYPOINT AdsRestructureTable(ADSHANDLE hConnect, UNSIGNED8* pucTableName, UNSIGNED8* pucAlias, UNSIGNED16 usFileType, UNSIGNED16 usCharType, UNSIGNED16 usLockType, UNSIGNED16 usCheckRights, UNSIGNED8* pucAddFields, UNSIGNED8* pucDeleteFields, UNSIGNED8* pucChangeFields);
#undef AdsRestructureTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRestructureTable=_AdsRestructureTable")
#pragma comment(linker, "/export:AdsRestructureTable=_AdsRestructureTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRestructureTable(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, UNSIGNED16 a4, UNSIGNED16 a5, UNSIGNED16 a6, UNSIGNED8* a7, UNSIGNED8* a8, UNSIGNED8* a9) {
    UNSIGNED32 rc = oadsimpl_AdsRestructureTable(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9);
    arc_log_rc("AdsRestructureTable", rc, 10, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7, (uintptr_t)a8, (uintptr_t)a9);
    return rc;
}

/* ---- AdsRollbackTransaction ---- */
#define AdsRollbackTransaction oadsimpl_AdsRollbackTransaction
extern UNSIGNED32 ENTRYPOINT AdsRollbackTransaction(ADSHANDLE hConnect);
#undef AdsRollbackTransaction
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRollbackTransaction=_AdsRollbackTransaction")
#pragma comment(linker, "/export:AdsRollbackTransaction=_AdsRollbackTransaction")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRollbackTransaction(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsRollbackTransaction(a0);
    arc_log_rc("AdsRollbackTransaction", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsRollbackTransaction80 ---- */
#define AdsRollbackTransaction80 oadsimpl_AdsRollbackTransaction80
extern UNSIGNED32 ENTRYPOINT AdsRollbackTransaction80(ADSHANDLE hConnect, UNSIGNED8* pucSavepoint, UNSIGNED32 ulOptions);
#undef AdsRollbackTransaction80
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRollbackTransaction80=_AdsRollbackTransaction80")
#pragma comment(linker, "/export:AdsRollbackTransaction80=_AdsRollbackTransaction80")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRollbackTransaction80(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32 a2) {
    UNSIGNED32 rc = oadsimpl_AdsRollbackTransaction80(a0, a1, a2);
    arc_log_rc("AdsRollbackTransaction80", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsSeek ---- */
#define AdsSeek oadsimpl_AdsSeek
extern UNSIGNED32 ENTRYPOINT AdsSeek(ADSHANDLE hIndex, UNSIGNED8* pucKey, UNSIGNED16 usKeyLen, UNSIGNED16 usKeyType, UNSIGNED16 usSeekType, UNSIGNED16* pbFound);
#undef AdsSeek
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSeek=_AdsSeek")
#pragma comment(linker, "/export:AdsSeek=_AdsSeek")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSeek(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, UNSIGNED16 a3, UNSIGNED16 a4, UNSIGNED16* a5) {
    UNSIGNED32 rc = oadsimpl_AdsSeek(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsSeek", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsSeekLast ---- */
#define AdsSeekLast oadsimpl_AdsSeekLast
extern UNSIGNED32 ENTRYPOINT AdsSeekLast(ADSHANDLE hIndex, UNSIGNED8* pucKey, UNSIGNED16 usKeyLen, UNSIGNED16 usKeyType, UNSIGNED16* pbFound);
#undef AdsSeekLast
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSeekLast=_AdsSeekLast")
#pragma comment(linker, "/export:AdsSeekLast=_AdsSeekLast")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSeekLast(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, UNSIGNED16 a3, UNSIGNED16* a4) {
    UNSIGNED32 rc = oadsimpl_AdsSeekLast(a0, a1, a2, a3, a4);
    arc_log_rc("AdsSeekLast", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsSetAOF ---- */
#define AdsSetAOF oadsimpl_AdsSetAOF
extern UNSIGNED32 ENTRYPOINT AdsSetAOF(ADSHANDLE hTable, UNSIGNED8* pucCondition, UNSIGNED16 usResolve);
#undef AdsSetAOF
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetAOF=_AdsSetAOF")
#pragma comment(linker, "/export:AdsSetAOF=_AdsSetAOF")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetAOF(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2) {
    UNSIGNED32 rc = oadsimpl_AdsSetAOF(a0, a1, a2);
    arc_log_rc("AdsSetAOF", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsSetBinary ---- */
#define AdsSetBinary oadsimpl_AdsSetBinary
extern UNSIGNED32 ENTRYPOINT AdsSetBinary(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16 usBinaryType, UNSIGNED32 ulTotalBytes, UNSIGNED32 ulOffset, UNSIGNED8* pucBuf, UNSIGNED32 ulBytes);
#undef AdsSetBinary
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetBinary=_AdsSetBinary")
#pragma comment(linker, "/export:AdsSetBinary=_AdsSetBinary")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetBinary(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, UNSIGNED32 a3, UNSIGNED32 a4, UNSIGNED8* a5, UNSIGNED32 a6) {
    UNSIGNED32 rc = oadsimpl_AdsSetBinary(a0, a1, a2, a3, a4, a5, a6);
    arc_log_rc("AdsSetBinary", rc, 7, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6);
    return rc;
}

/* ---- AdsSetCollation ---- */
#define AdsSetCollation oadsimpl_AdsSetCollation
extern UNSIGNED32 ENTRYPOINT AdsSetCollation(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsSetCollation
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetCollation=_AdsSetCollation")
#pragma comment(linker, "/export:AdsSetCollation=_AdsSetCollation")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetCollation(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsSetCollation(a0, a1);
    arc_log_rc("AdsSetCollation", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsSetDateFormat ---- */
#define AdsSetDateFormat oadsimpl_AdsSetDateFormat
extern UNSIGNED32 ENTRYPOINT AdsSetDateFormat(UNSIGNED8* pucFormat);
#undef AdsSetDateFormat
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetDateFormat=_AdsSetDateFormat")
#pragma comment(linker, "/export:AdsSetDateFormat=_AdsSetDateFormat")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetDateFormat(UNSIGNED8* a0) {
    UNSIGNED32 rc = oadsimpl_AdsSetDateFormat(a0);
    arc_log_rc("AdsSetDateFormat", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsSetDateFormat60 ---- */
#define AdsSetDateFormat60 oadsimpl_AdsSetDateFormat60
extern UNSIGNED32 ENTRYPOINT AdsSetDateFormat60(ADSHANDLE hConnect, UNSIGNED8* pucFormat);
#undef AdsSetDateFormat60
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetDateFormat60=_AdsSetDateFormat60")
#pragma comment(linker, "/export:AdsSetDateFormat60=_AdsSetDateFormat60")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetDateFormat60(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsSetDateFormat60(a0, a1);
    arc_log_rc("AdsSetDateFormat60", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsSetDecimals ---- */
#define AdsSetDecimals oadsimpl_AdsSetDecimals
extern UNSIGNED32 ENTRYPOINT AdsSetDecimals(UNSIGNED16 usDecimals);
#undef AdsSetDecimals
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetDecimals=_AdsSetDecimals")
#pragma comment(linker, "/export:AdsSetDecimals=_AdsSetDecimals")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetDecimals(UNSIGNED16 a0) {
    UNSIGNED32 rc = oadsimpl_AdsSetDecimals(a0);
    arc_log_rc("AdsSetDecimals", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsSetDefault ---- */
#define AdsSetDefault oadsimpl_AdsSetDefault
extern UNSIGNED32 ENTRYPOINT AdsSetDefault(UNSIGNED8* pucDir);
#undef AdsSetDefault
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetDefault=_AdsSetDefault")
#pragma comment(linker, "/export:AdsSetDefault=_AdsSetDefault")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetDefault(UNSIGNED8* a0) {
    UNSIGNED32 rc = oadsimpl_AdsSetDefault(a0);
    arc_log_rc("AdsSetDefault", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsSetDefaultConnection ---- */
#define AdsSetDefaultConnection oadsimpl_AdsSetDefaultConnection
extern UNSIGNED32 ENTRYPOINT AdsSetDefaultConnection(ADSHANDLE hConn);
#undef AdsSetDefaultConnection
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetDefaultConnection=_AdsSetDefaultConnection")
#pragma comment(linker, "/export:AdsSetDefaultConnection=_AdsSetDefaultConnection")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetDefaultConnection(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsSetDefaultConnection(a0);
    arc_log_rc("AdsSetDefaultConnection", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsSetDeferredFlush ---- */
#define AdsSetDeferredFlush oadsimpl_AdsSetDeferredFlush
extern UNSIGNED32 ENTRYPOINT AdsSetDeferredFlush(ADSHANDLE hTable, UNSIGNED16 usDeferred);
#undef AdsSetDeferredFlush
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetDeferredFlush=_AdsSetDeferredFlush")
#pragma comment(linker, "/export:AdsSetDeferredFlush=_AdsSetDeferredFlush")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetDeferredFlush(ADSHANDLE a0, UNSIGNED16 a1) {
    UNSIGNED32 rc = oadsimpl_AdsSetDeferredFlush(a0, a1);
    arc_log_rc("AdsSetDeferredFlush", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsSetDouble ---- */
#define AdsSetDouble oadsimpl_AdsSetDouble
extern UNSIGNED32 ENTRYPOINT AdsSetDouble(ADSHANDLE hTable, UNSIGNED8* pucField, double dValue);
#undef AdsSetDouble
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetDouble=_AdsSetDouble")
#pragma comment(linker, "/export:AdsSetDouble=_AdsSetDouble")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetDouble(ADSHANDLE a0, UNSIGNED8* a1, double a2) {
    UNSIGNED32 rc = oadsimpl_AdsSetDouble(a0, a1, a2);
    arc_log_rc("AdsSetDouble", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsSetEmpty ---- */
#define AdsSetEmpty oadsimpl_AdsSetEmpty
extern UNSIGNED32 ENTRYPOINT AdsSetEmpty(ADSHANDLE hObj, UNSIGNED8* pucFldId);
#undef AdsSetEmpty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetEmpty=_AdsSetEmpty")
#pragma comment(linker, "/export:AdsSetEmpty=_AdsSetEmpty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetEmpty(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsSetEmpty(a0, a1);
    arc_log_rc("AdsSetEmpty", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsSetEncryptionPassword ---- */
#define AdsSetEncryptionPassword oadsimpl_AdsSetEncryptionPassword
extern UNSIGNED32 ENTRYPOINT AdsSetEncryptionPassword(ADSHANDLE hConnect, UNSIGNED8* pucPassword);
#undef AdsSetEncryptionPassword
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetEncryptionPassword=_AdsSetEncryptionPassword")
#pragma comment(linker, "/export:AdsSetEncryptionPassword=_AdsSetEncryptionPassword")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetEncryptionPassword(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsSetEncryptionPassword(a0, a1);
    arc_log_rc("AdsSetEncryptionPassword", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsSetEpoch ---- */
#define AdsSetEpoch oadsimpl_AdsSetEpoch
extern UNSIGNED32 ENTRYPOINT AdsSetEpoch(UNSIGNED16 usEpoch);
#undef AdsSetEpoch
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetEpoch=_AdsSetEpoch")
#pragma comment(linker, "/export:AdsSetEpoch=_AdsSetEpoch")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetEpoch(UNSIGNED16 a0) {
    UNSIGNED32 rc = oadsimpl_AdsSetEpoch(a0);
    arc_log_rc("AdsSetEpoch", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsSetExact ---- */
#define AdsSetExact oadsimpl_AdsSetExact
extern UNSIGNED32 ENTRYPOINT AdsSetExact(UNSIGNED16 bExact);
#undef AdsSetExact
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetExact=_AdsSetExact")
#pragma comment(linker, "/export:AdsSetExact=_AdsSetExact")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetExact(UNSIGNED16 a0) {
    UNSIGNED32 rc = oadsimpl_AdsSetExact(a0);
    arc_log_rc("AdsSetExact", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsSetField ---- */
#define AdsSetField oadsimpl_AdsSetField
extern UNSIGNED32 ENTRYPOINT AdsSetField(ADSHANDLE hObj, UNSIGNED8* pucFldId, UNSIGNED8* pucBuf, UNSIGNED32 ulLen);
#undef AdsSetField
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetField=_AdsSetField")
#pragma comment(linker, "/export:AdsSetField=_AdsSetField")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetField(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32 a3) {
    UNSIGNED32 rc = oadsimpl_AdsSetField(a0, a1, a2, a3);
    arc_log_rc("AdsSetField", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsSetFieldRaw ---- */
#define AdsSetFieldRaw oadsimpl_AdsSetFieldRaw
extern UNSIGNED32 ENTRYPOINT AdsSetFieldRaw(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED8* pucBuf, UNSIGNED32 ulLen);
#undef AdsSetFieldRaw
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetFieldRaw=_AdsSetFieldRaw")
#pragma comment(linker, "/export:AdsSetFieldRaw=_AdsSetFieldRaw")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetFieldRaw(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32 a3) {
    UNSIGNED32 rc = oadsimpl_AdsSetFieldRaw(a0, a1, a2, a3);
    arc_log_rc("AdsSetFieldRaw", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsSetFilter ---- */
#define AdsSetFilter oadsimpl_AdsSetFilter
extern UNSIGNED32 ENTRYPOINT AdsSetFilter(ADSHANDLE hTable, UNSIGNED8* pucExpr);
#undef AdsSetFilter
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetFilter=_AdsSetFilter")
#pragma comment(linker, "/export:AdsSetFilter=_AdsSetFilter")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetFilter(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsSetFilter(a0, a1);
    arc_log_rc("AdsSetFilter", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsSetIndexDirection ---- */
#define AdsSetIndexDirection oadsimpl_AdsSetIndexDirection
extern UNSIGNED32 ENTRYPOINT AdsSetIndexDirection(ADSHANDLE hIndex, UNSIGNED16 usDir);
#undef AdsSetIndexDirection
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetIndexDirection=_AdsSetIndexDirection")
#pragma comment(linker, "/export:AdsSetIndexDirection=_AdsSetIndexDirection")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetIndexDirection(ADSHANDLE a0, UNSIGNED16 a1) {
    UNSIGNED32 rc = oadsimpl_AdsSetIndexDirection(a0, a1);
    arc_log_rc("AdsSetIndexDirection", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsSetIndexOrderByHandle ---- */
#define AdsSetIndexOrderByHandle oadsimpl_AdsSetIndexOrderByHandle
extern UNSIGNED32 ENTRYPOINT AdsSetIndexOrderByHandle(ADSHANDLE hTable, ADSHANDLE hIndex);
#undef AdsSetIndexOrderByHandle
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetIndexOrderByHandle=_AdsSetIndexOrderByHandle")
#pragma comment(linker, "/export:AdsSetIndexOrderByHandle=_AdsSetIndexOrderByHandle")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetIndexOrderByHandle(ADSHANDLE a0, ADSHANDLE a1) {
    UNSIGNED32 rc = oadsimpl_AdsSetIndexOrderByHandle(a0, a1);
    arc_log_rc("AdsSetIndexOrderByHandle", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsSetIndexOrder ---- */
#define AdsSetIndexOrder oadsimpl_AdsSetIndexOrder
extern UNSIGNED32 ENTRYPOINT AdsSetIndexOrder(ADSHANDLE hTable, UNSIGNED8* pucName);
#undef AdsSetIndexOrder
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetIndexOrder=_AdsSetIndexOrder")
#pragma comment(linker, "/export:AdsSetIndexOrder=_AdsSetIndexOrder")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetIndexOrder(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsSetIndexOrder(a0, a1);
    arc_log_rc("AdsSetIndexOrder", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsSetJulian ---- */
#define AdsSetJulian oadsimpl_AdsSetJulian
extern UNSIGNED32 ENTRYPOINT AdsSetJulian(ADSHANDLE hTable, UNSIGNED8* pucField, SIGNED32 lJulian);
#undef AdsSetJulian
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetJulian=_AdsSetJulian")
#pragma comment(linker, "/export:AdsSetJulian=_AdsSetJulian")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetJulian(ADSHANDLE a0, UNSIGNED8* a1, SIGNED32 a2) {
    UNSIGNED32 rc = oadsimpl_AdsSetJulian(a0, a1, a2);
    arc_log_rc("AdsSetJulian", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsSetLockCycle ---- */
#define AdsSetLockCycle oadsimpl_AdsSetLockCycle
extern UNSIGNED32 ENTRYPOINT AdsSetLockCycle(ADSHANDLE hConnect, UNSIGNED32 ulCycle);
#undef AdsSetLockCycle
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetLockCycle=_AdsSetLockCycle")
#pragma comment(linker, "/export:AdsSetLockCycle=_AdsSetLockCycle")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetLockCycle(ADSHANDLE a0, UNSIGNED32 a1) {
    UNSIGNED32 rc = oadsimpl_AdsSetLockCycle(a0, a1);
    arc_log_rc("AdsSetLockCycle", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsSetLockRetryCount ---- */
#define AdsSetLockRetryCount oadsimpl_AdsSetLockRetryCount
extern UNSIGNED32 ENTRYPOINT AdsSetLockRetryCount(ADSHANDLE hConnect, UNSIGNED16 usRetryCount);
#undef AdsSetLockRetryCount
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetLockRetryCount=_AdsSetLockRetryCount")
#pragma comment(linker, "/export:AdsSetLockRetryCount=_AdsSetLockRetryCount")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetLockRetryCount(ADSHANDLE a0, UNSIGNED16 a1) {
    UNSIGNED32 rc = oadsimpl_AdsSetLockRetryCount(a0, a1);
    arc_log_rc("AdsSetLockRetryCount", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsSetLogical ---- */
#define AdsSetLogical oadsimpl_AdsSetLogical
extern UNSIGNED32 ENTRYPOINT AdsSetLogical(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16 bValue);
#undef AdsSetLogical
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetLogical=_AdsSetLogical")
#pragma comment(linker, "/export:AdsSetLogical=_AdsSetLogical")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetLogical(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2) {
    UNSIGNED32 rc = oadsimpl_AdsSetLogical(a0, a1, a2);
    arc_log_rc("AdsSetLogical", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsSetLongLong ---- */
#define AdsSetLongLong oadsimpl_AdsSetLongLong
extern UNSIGNED32 ENTRYPOINT AdsSetLongLong(ADSHANDLE hTable, UNSIGNED8* pucField, int64_t llValue);
#undef AdsSetLongLong
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetLongLong=_AdsSetLongLong")
#pragma comment(linker, "/export:AdsSetLongLong=_AdsSetLongLong")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetLongLong(ADSHANDLE a0, UNSIGNED8* a1, int64_t a2) {
    UNSIGNED32 rc = oadsimpl_AdsSetLongLong(a0, a1, a2);
    arc_log_rc("AdsSetLongLong", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsSetMilliseconds ---- */
#define AdsSetMilliseconds oadsimpl_AdsSetMilliseconds
extern UNSIGNED32 ENTRYPOINT AdsSetMilliseconds(ADSHANDLE hTable, UNSIGNED8* pucField, SIGNED32 lMs);
#undef AdsSetMilliseconds
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetMilliseconds=_AdsSetMilliseconds")
#pragma comment(linker, "/export:AdsSetMilliseconds=_AdsSetMilliseconds")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetMilliseconds(ADSHANDLE a0, UNSIGNED8* a1, SIGNED32 a2) {
    UNSIGNED32 rc = oadsimpl_AdsSetMilliseconds(a0, a1, a2);
    arc_log_rc("AdsSetMilliseconds", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsSetMoney ---- */
#define AdsSetMoney oadsimpl_AdsSetMoney
extern UNSIGNED32 ENTRYPOINT AdsSetMoney(ADSHANDLE hObj, UNSIGNED8* pucFldId, SIGNED64 qValue);
#undef AdsSetMoney
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetMoney=_AdsSetMoney")
#pragma comment(linker, "/export:AdsSetMoney=_AdsSetMoney")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetMoney(ADSHANDLE a0, UNSIGNED8* a1, SIGNED64 a2) {
    UNSIGNED32 rc = oadsimpl_AdsSetMoney(a0, a1, a2);
    arc_log_rc("AdsSetMoney", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsSetNull ---- */
#define AdsSetNull oadsimpl_AdsSetNull
extern UNSIGNED32 ENTRYPOINT AdsSetNull(ADSHANDLE hTable, UNSIGNED8* pucFldId);
#undef AdsSetNull
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetNull=_AdsSetNull")
#pragma comment(linker, "/export:AdsSetNull=_AdsSetNull")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetNull(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsSetNull(a0, a1);
    arc_log_rc("AdsSetNull", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsSetRecord ---- */
#define AdsSetRecord oadsimpl_AdsSetRecord
extern UNSIGNED32 ENTRYPOINT AdsSetRecord(ADSHANDLE hTable, UNSIGNED8* pucBuf, UNSIGNED32 ulLen);
#undef AdsSetRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetRecord=_AdsSetRecord")
#pragma comment(linker, "/export:AdsSetRecord=_AdsSetRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetRecord(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32 a2) {
    UNSIGNED32 rc = oadsimpl_AdsSetRecord(a0, a1, a2);
    arc_log_rc("AdsSetRecord", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsSetRelKeyPos ---- */
#define AdsSetRelKeyPos oadsimpl_AdsSetRelKeyPos
extern UNSIGNED32 ENTRYPOINT AdsSetRelKeyPos(ADSHANDLE hIndex, double dPos);
#undef AdsSetRelKeyPos
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetRelKeyPos=_AdsSetRelKeyPos")
#pragma comment(linker, "/export:AdsSetRelKeyPos=_AdsSetRelKeyPos")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetRelKeyPos(ADSHANDLE a0, double a1) {
    UNSIGNED32 rc = oadsimpl_AdsSetRelKeyPos(a0, a1);
    arc_log_rc("AdsSetRelKeyPos", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsSetRelation ---- */
#define AdsSetRelation oadsimpl_AdsSetRelation
extern UNSIGNED32 ENTRYPOINT AdsSetRelation(ADSHANDLE hParent, ADSHANDLE hChild, UNSIGNED8* pucExpr);
#undef AdsSetRelation
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetRelation=_AdsSetRelation")
#pragma comment(linker, "/export:AdsSetRelation=_AdsSetRelation")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetRelation(ADSHANDLE a0, ADSHANDLE a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsSetRelation(a0, a1, a2);
    arc_log_rc("AdsSetRelation", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsSetScope ---- */
#define AdsSetScope oadsimpl_AdsSetScope
extern UNSIGNED32 ENTRYPOINT AdsSetScope(ADSHANDLE hIndex, UNSIGNED16 usScope, UNSIGNED8* pucScope, UNSIGNED16 usLen, UNSIGNED16 usDataType);
#undef AdsSetScope
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetScope=_AdsSetScope")
#pragma comment(linker, "/export:AdsSetScope=_AdsSetScope")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetScope(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED8* a2, UNSIGNED16 a3, UNSIGNED16 a4) {
    UNSIGNED32 rc = oadsimpl_AdsSetScope(a0, a1, a2, a3, a4);
    arc_log_rc("AdsSetScope", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsSetScopedRelation ---- */
#define AdsSetScopedRelation oadsimpl_AdsSetScopedRelation
extern UNSIGNED32 ENTRYPOINT AdsSetScopedRelation(ADSHANDLE hParent, ADSHANDLE hChild, UNSIGNED8* pucExpr);
#undef AdsSetScopedRelation
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetScopedRelation=_AdsSetScopedRelation")
#pragma comment(linker, "/export:AdsSetScopedRelation=_AdsSetScopedRelation")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetScopedRelation(ADSHANDLE a0, ADSHANDLE a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsSetScopedRelation(a0, a1, a2);
    arc_log_rc("AdsSetScopedRelation", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsSetSearchPath ---- */
#define AdsSetSearchPath oadsimpl_AdsSetSearchPath
extern UNSIGNED32 ENTRYPOINT AdsSetSearchPath(UNSIGNED8* pucPath);
#undef AdsSetSearchPath
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetSearchPath=_AdsSetSearchPath")
#pragma comment(linker, "/export:AdsSetSearchPath=_AdsSetSearchPath")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetSearchPath(UNSIGNED8* a0) {
    UNSIGNED32 rc = oadsimpl_AdsSetSearchPath(a0);
    arc_log_rc("AdsSetSearchPath", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsSetServerType ---- */
#define AdsSetServerType oadsimpl_AdsSetServerType
extern UNSIGNED32 ENTRYPOINT AdsSetServerType(UNSIGNED16 usType);
#undef AdsSetServerType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetServerType=_AdsSetServerType")
#pragma comment(linker, "/export:AdsSetServerType=_AdsSetServerType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetServerType(UNSIGNED16 a0) {
    UNSIGNED32 rc = oadsimpl_AdsSetServerType(a0);
    arc_log_rc("AdsSetServerType", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsSetShort ---- */
#define AdsSetShort oadsimpl_AdsSetShort
extern UNSIGNED32 ENTRYPOINT AdsSetShort(ADSHANDLE hObj, UNSIGNED8* pucFldId, SIGNED32 sValue);
#undef AdsSetShort
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetShort=_AdsSetShort")
#pragma comment(linker, "/export:AdsSetShort=_AdsSetShort")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetShort(ADSHANDLE a0, UNSIGNED8* a1, SIGNED32 a2) {
    UNSIGNED32 rc = oadsimpl_AdsSetShort(a0, a1, a2);
    arc_log_rc("AdsSetShort", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsSetString ---- */
#define AdsSetString oadsimpl_AdsSetString
extern UNSIGNED32 ENTRYPOINT AdsSetString(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED8* pucValue, UNSIGNED32 ulLen);
#undef AdsSetString
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetString=_AdsSetString")
#pragma comment(linker, "/export:AdsSetString=_AdsSetString")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetString(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32 a3) {
    UNSIGNED32 rc = oadsimpl_AdsSetString(a0, a1, a2, a3);
    arc_log_rc("AdsSetString", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsSetStringW ---- */
#define AdsSetStringW oadsimpl_AdsSetStringW
extern UNSIGNED32 ENTRYPOINT AdsSetStringW(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pucValueW, UNSIGNED32 ulLen);
#undef AdsSetStringW
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetStringW=_AdsSetStringW")
#pragma comment(linker, "/export:AdsSetStringW=_AdsSetStringW")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetStringW(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2, UNSIGNED32 a3) {
    UNSIGNED32 rc = oadsimpl_AdsSetStringW(a0, a1, a2, a3);
    arc_log_rc("AdsSetStringW", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsSetTime ---- */
#define AdsSetTime oadsimpl_AdsSetTime
extern UNSIGNED32 ENTRYPOINT AdsSetTime(ADSHANDLE hObj, UNSIGNED8* pucFldId, UNSIGNED8* pucValue, UNSIGNED16 usLen);
#undef AdsSetTime
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetTime=_AdsSetTime")
#pragma comment(linker, "/export:AdsSetTime=_AdsSetTime")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetTime(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3) {
    UNSIGNED32 rc = oadsimpl_AdsSetTime(a0, a1, a2, a3);
    arc_log_rc("AdsSetTime", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsSetTimeStamp ---- */
#define AdsSetTimeStamp oadsimpl_AdsSetTimeStamp
extern UNSIGNED32 ENTRYPOINT AdsSetTimeStamp(ADSHANDLE hObj, UNSIGNED8* pucFldId, UNSIGNED8* pucBuf, UNSIGNED32 ulLen);
#undef AdsSetTimeStamp
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetTimeStamp=_AdsSetTimeStamp")
#pragma comment(linker, "/export:AdsSetTimeStamp=_AdsSetTimeStamp")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetTimeStamp(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32 a3) {
    UNSIGNED32 rc = oadsimpl_AdsSetTimeStamp(a0, a1, a2, a3);
    arc_log_rc("AdsSetTimeStamp", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsShowDeleted ---- */
#define AdsShowDeleted oadsimpl_AdsShowDeleted
extern UNSIGNED32 ENTRYPOINT AdsShowDeleted(UNSIGNED16 bShow);
#undef AdsShowDeleted
#pragma comment(linker, "/alternatename:_oadsimpl_AdsShowDeleted=_AdsShowDeleted")
#pragma comment(linker, "/export:AdsShowDeleted=_AdsShowDeleted")
__declspec(dllexport) UNSIGNED32 __stdcall AdsShowDeleted(UNSIGNED16 a0) {
    UNSIGNED32 rc = oadsimpl_AdsShowDeleted(a0);
    arc_log_rc("AdsShowDeleted", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsShowError ---- */
#define AdsShowError oadsimpl_AdsShowError
extern UNSIGNED32 ENTRYPOINT AdsShowError(UNSIGNED8* pucCaption);
#undef AdsShowError
#pragma comment(linker, "/alternatename:_oadsimpl_AdsShowError=_AdsShowError")
#pragma comment(linker, "/export:AdsShowError=_AdsShowError")
__declspec(dllexport) UNSIGNED32 __stdcall AdsShowError(UNSIGNED8* a0) {
    UNSIGNED32 rc = oadsimpl_AdsShowError(a0);
    arc_log_rc("AdsShowError", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsSkip ---- */
#define AdsSkip oadsimpl_AdsSkip
extern UNSIGNED32 ENTRYPOINT AdsSkip(ADSHANDLE hTable, SIGNED32 lRows);
#undef AdsSkip
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSkip=_AdsSkip")
#pragma comment(linker, "/export:AdsSkip=_AdsSkip")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSkip(ADSHANDLE a0, SIGNED32 a1) {
    UNSIGNED32 rc = oadsimpl_AdsSkip(a0, a1);
    arc_log_rc("AdsSkip", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsSkipUnique ---- */
#define AdsSkipUnique oadsimpl_AdsSkipUnique
extern UNSIGNED32 ENTRYPOINT AdsSkipUnique(ADSHANDLE hIndex, SIGNED32 lDirection);
#undef AdsSkipUnique
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSkipUnique=_AdsSkipUnique")
#pragma comment(linker, "/export:AdsSkipUnique=_AdsSkipUnique")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSkipUnique(ADSHANDLE a0, SIGNED32 a1) {
    UNSIGNED32 rc = oadsimpl_AdsSkipUnique(a0, a1);
    arc_log_rc("AdsSkipUnique", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsStmtClearTablePasswords ---- */
#define AdsStmtClearTablePasswords oadsimpl_AdsStmtClearTablePasswords
extern UNSIGNED32 ENTRYPOINT AdsStmtClearTablePasswords(ADSHANDLE hStatement);
#undef AdsStmtClearTablePasswords
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStmtClearTablePasswords=_AdsStmtClearTablePasswords")
#pragma comment(linker, "/export:AdsStmtClearTablePasswords=_AdsStmtClearTablePasswords")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStmtClearTablePasswords(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsStmtClearTablePasswords(a0);
    arc_log_rc("AdsStmtClearTablePasswords", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsStmtDisableEncryption ---- */
#define AdsStmtDisableEncryption oadsimpl_AdsStmtDisableEncryption
extern UNSIGNED32 ENTRYPOINT AdsStmtDisableEncryption(ADSHANDLE hStatement);
#undef AdsStmtDisableEncryption
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStmtDisableEncryption=_AdsStmtDisableEncryption")
#pragma comment(linker, "/export:AdsStmtDisableEncryption=_AdsStmtDisableEncryption")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStmtDisableEncryption(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsStmtDisableEncryption(a0);
    arc_log_rc("AdsStmtDisableEncryption", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsStmtSetTableCollation ---- */
#define AdsStmtSetTableCollation oadsimpl_AdsStmtSetTableCollation
extern UNSIGNED32 ENTRYPOINT AdsStmtSetTableCollation(ADSHANDLE hStatement, UNSIGNED8* pucCollation);
#undef AdsStmtSetTableCollation
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStmtSetTableCollation=_AdsStmtSetTableCollation")
#pragma comment(linker, "/export:AdsStmtSetTableCollation=_AdsStmtSetTableCollation")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStmtSetTableCollation(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsStmtSetTableCollation(a0, a1);
    arc_log_rc("AdsStmtSetTableCollation", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsStmtSetTableLockType ---- */
#define AdsStmtSetTableLockType oadsimpl_AdsStmtSetTableLockType
extern UNSIGNED32 ENTRYPOINT AdsStmtSetTableLockType(ADSHANDLE hStmt, UNSIGNED16 usType);
#undef AdsStmtSetTableLockType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStmtSetTableLockType=_AdsStmtSetTableLockType")
#pragma comment(linker, "/export:AdsStmtSetTableLockType=_AdsStmtSetTableLockType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStmtSetTableLockType(ADSHANDLE a0, UNSIGNED16 a1) {
    UNSIGNED32 rc = oadsimpl_AdsStmtSetTableLockType(a0, a1);
    arc_log_rc("AdsStmtSetTableLockType", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsStmtSetTablePassword ---- */
#define AdsStmtSetTablePassword oadsimpl_AdsStmtSetTablePassword
extern UNSIGNED32 ENTRYPOINT AdsStmtSetTablePassword(ADSHANDLE hStmt, UNSIGNED8* pucName, UNSIGNED8* pucPwd);
#undef AdsStmtSetTablePassword
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStmtSetTablePassword=_AdsStmtSetTablePassword")
#pragma comment(linker, "/export:AdsStmtSetTablePassword=_AdsStmtSetTablePassword")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStmtSetTablePassword(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsStmtSetTablePassword(a0, a1, a2);
    arc_log_rc("AdsStmtSetTablePassword", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsStmtSetTableReadOnly ---- */
#define AdsStmtSetTableReadOnly oadsimpl_AdsStmtSetTableReadOnly
extern UNSIGNED32 ENTRYPOINT AdsStmtSetTableReadOnly(ADSHANDLE hStmt, UNSIGNED16 bReadOnly);
#undef AdsStmtSetTableReadOnly
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStmtSetTableReadOnly=_AdsStmtSetTableReadOnly")
#pragma comment(linker, "/export:AdsStmtSetTableReadOnly=_AdsStmtSetTableReadOnly")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStmtSetTableReadOnly(ADSHANDLE a0, UNSIGNED16 a1) {
    UNSIGNED32 rc = oadsimpl_AdsStmtSetTableReadOnly(a0, a1);
    arc_log_rc("AdsStmtSetTableReadOnly", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsStmtSetTableType ---- */
#define AdsStmtSetTableType oadsimpl_AdsStmtSetTableType
extern UNSIGNED32 ENTRYPOINT AdsStmtSetTableType(ADSHANDLE hStmt, UNSIGNED16 usType);
#undef AdsStmtSetTableType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStmtSetTableType=_AdsStmtSetTableType")
#pragma comment(linker, "/export:AdsStmtSetTableType=_AdsStmtSetTableType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStmtSetTableType(ADSHANDLE a0, UNSIGNED16 a1) {
    UNSIGNED32 rc = oadsimpl_AdsStmtSetTableType(a0, a1);
    arc_log_rc("AdsStmtSetTableType", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsAggregate ---- */
#define AdsAggregate oadsimpl_AdsAggregate
extern UNSIGNED32 ENTRYPOINT AdsAggregate(ADSHANDLE hTbl, UNSIGNED8* pszForCond, UNSIGNED8* pszAggSpec, ADSHANDLE* phResult);
#undef AdsAggregate
#pragma comment(linker, "/alternatename:_oadsimpl_AdsAggregate=_AdsAggregate")
#pragma comment(linker, "/export:AdsAggregate=_AdsAggregate")
__declspec(dllexport) UNSIGNED32 __stdcall AdsAggregate(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, ADSHANDLE* a3) {
    UNSIGNED32 rc = oadsimpl_AdsAggregate(a0, a1, a2, a3);
    arc_log_rc("AdsAggregate", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsAggregateClose ---- */
#define AdsAggregateClose oadsimpl_AdsAggregateClose
extern UNSIGNED32 ENTRYPOINT AdsAggregateClose(ADSHANDLE hRes);
#undef AdsAggregateClose
#pragma comment(linker, "/alternatename:_oadsimpl_AdsAggregateClose=_AdsAggregateClose")
#pragma comment(linker, "/export:AdsAggregateClose=_AdsAggregateClose")
__declspec(dllexport) UNSIGNED32 __stdcall AdsAggregateClose(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsAggregateClose(a0);
    arc_log_rc("AdsAggregateClose", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsAggregateCount ---- */
#define AdsAggregateCount oadsimpl_AdsAggregateCount
extern UNSIGNED32 ENTRYPOINT AdsAggregateCount(ADSHANDLE hRes, UNSIGNED32* pulCount);
#undef AdsAggregateCount
#pragma comment(linker, "/alternatename:_oadsimpl_AdsAggregateCount=_AdsAggregateCount")
#pragma comment(linker, "/export:AdsAggregateCount=_AdsAggregateCount")
__declspec(dllexport) UNSIGNED32 __stdcall AdsAggregateCount(ADSHANDLE a0, UNSIGNED32* a1) {
    UNSIGNED32 rc = oadsimpl_AdsAggregateCount(a0, a1);
    arc_log_rc("AdsAggregateCount", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsAggregateValue ---- */
#define AdsAggregateValue oadsimpl_AdsAggregateValue
extern UNSIGNED32 ENTRYPOINT AdsAggregateValue(ADSHANDLE hRes, UNSIGNED32 ulIndex, UNSIGNED16* pusType, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsAggregateValue
#pragma comment(linker, "/alternatename:_oadsimpl_AdsAggregateValue=_AdsAggregateValue")
#pragma comment(linker, "/export:AdsAggregateValue=_AdsAggregateValue")
__declspec(dllexport) UNSIGNED32 __stdcall AdsAggregateValue(ADSHANDLE a0, UNSIGNED32 a1, UNSIGNED16* a2, UNSIGNED8* a3, UNSIGNED16* a4) {
    UNSIGNED32 rc = oadsimpl_AdsAggregateValue(a0, a1, a2, a3, a4);
    arc_log_rc("AdsAggregateValue", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsConnect101 ---- */
#define AdsConnect101 oadsimpl_AdsConnect101
extern UNSIGNED32 ENTRYPOINT AdsConnect101(UNSIGNED8* pucConnectString, ADSHANDLE* phConnectOptions, ADSHANDLE* phConnect);
#undef AdsConnect101
#pragma comment(linker, "/alternatename:_oadsimpl_AdsConnect101=_AdsConnect101")
#pragma comment(linker, "/export:AdsConnect101=_AdsConnect101")
__declspec(dllexport) UNSIGNED32 __stdcall AdsConnect101(UNSIGNED8* a0, ADSHANDLE* a1, ADSHANDLE* a2) {
    UNSIGNED32 rc = oadsimpl_AdsConnect101(a0, a1, a2);
    arc_log_rc("AdsConnect101", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsCreateIndex90 ---- */
#define AdsCreateIndex90 oadsimpl_AdsCreateIndex90
extern UNSIGNED32 ENTRYPOINT AdsCreateIndex90(ADSHANDLE hObj, UNSIGNED8* pucFileName, UNSIGNED8* pucTag, UNSIGNED8* pucExpr, UNSIGNED8* pucCondition, UNSIGNED8* pucWhile, UNSIGNED32 ulOptions, UNSIGNED32 ulPageSize, UNSIGNED8* pucCollation, ADSHANDLE* phIndex);
#undef AdsCreateIndex90
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCreateIndex90=_AdsCreateIndex90")
#pragma comment(linker, "/export:AdsCreateIndex90=_AdsCreateIndex90")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCreateIndex90(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5, UNSIGNED32 a6, UNSIGNED32 a7, UNSIGNED8* a8, ADSHANDLE* a9) {
    UNSIGNED32 rc = oadsimpl_AdsCreateIndex90(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9);
    arc_log_rc("AdsCreateIndex90", rc, 10, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7, (uintptr_t)a8, (uintptr_t)a9);
    return rc;
}

/* ---- AdsCreateTable71 ---- */
#define AdsCreateTable71 oadsimpl_AdsCreateTable71
extern UNSIGNED32 ENTRYPOINT AdsCreateTable71(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucAlias, UNSIGNED16 usTableType, UNSIGNED16 usCharType, UNSIGNED16 usLockType, UNSIGNED16 usCheckRights, UNSIGNED16 usMemoSize, UNSIGNED8* pucFields, UNSIGNED32 ulOptions, ADSHANDLE* phTable);
#undef AdsCreateTable71
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCreateTable71=_AdsCreateTable71")
#pragma comment(linker, "/export:AdsCreateTable71=_AdsCreateTable71")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCreateTable71(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, UNSIGNED16 a4, UNSIGNED16 a5, UNSIGNED16 a6, UNSIGNED16 a7, UNSIGNED8* a8, UNSIGNED32 a9, ADSHANDLE* a10) {
    UNSIGNED32 rc = oadsimpl_AdsCreateTable71(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
    arc_log_rc("AdsCreateTable71", rc, 11, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7, (uintptr_t)a8, (uintptr_t)a9, (uintptr_t)a10);
    return rc;
}

/* ---- AdsCreateTable90 ---- */
#define AdsCreateTable90 oadsimpl_AdsCreateTable90
extern UNSIGNED32 ENTRYPOINT AdsCreateTable90(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucAlias, UNSIGNED16 usTableType, UNSIGNED16 usCharType, UNSIGNED16 usLockType, UNSIGNED16 usCheckRights, UNSIGNED16 usMemoSize, UNSIGNED8* pucFields, UNSIGNED32 ulOptions, UNSIGNED8* pucCollation, ADSHANDLE* phTable);
#undef AdsCreateTable90
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCreateTable90=_AdsCreateTable90")
#pragma comment(linker, "/export:AdsCreateTable90=_AdsCreateTable90")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCreateTable90(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, UNSIGNED16 a4, UNSIGNED16 a5, UNSIGNED16 a6, UNSIGNED16 a7, UNSIGNED8* a8, UNSIGNED32 a9, UNSIGNED8* a10, ADSHANDLE* a11) {
    UNSIGNED32 rc = oadsimpl_AdsCreateTable90(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11);
    arc_log_rc("AdsCreateTable90", rc, 12, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7, (uintptr_t)a8, (uintptr_t)a9, (uintptr_t)a10, (uintptr_t)a11);
    return rc;
}

/* ---- AdsDDCreateFunction ---- */
#define AdsDDCreateFunction oadsimpl_AdsDDCreateFunction
extern UNSIGNED32 ENTRYPOINT AdsDDCreateFunction(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucContainer, UNSIGNED8* pucImplementation, UNSIGNED8* pucRetType, UNSIGNED8* pucInParams, UNSIGNED8* pucComment);
#undef AdsDDCreateFunction
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreateFunction=_AdsDDCreateFunction")
#pragma comment(linker, "/export:AdsDDCreateFunction=_AdsDDCreateFunction")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreateFunction(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5, UNSIGNED8* a6) {
    UNSIGNED32 rc = oadsimpl_AdsDDCreateFunction(a0, a1, a2, a3, a4, a5, a6);
    arc_log_rc("AdsDDCreateFunction", rc, 7, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6);
    return rc;
}

/* ---- AdsDDCreateRefIntegrity62 ---- */
#define AdsDDCreateRefIntegrity62 oadsimpl_AdsDDCreateRefIntegrity62
extern UNSIGNED32 ENTRYPOINT AdsDDCreateRefIntegrity62(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucFail, UNSIGNED8* pucParent, UNSIGNED8* pucParentTag, UNSIGNED8* pucChild, UNSIGNED8* pucChildTag, UNSIGNED16 usUpdate, UNSIGNED16 usDelete, UNSIGNED8* pucNoPrimaryError, UNSIGNED8* pucCascadeError);
#undef AdsDDCreateRefIntegrity62
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreateRefIntegrity62=_AdsDDCreateRefIntegrity62")
#pragma comment(linker, "/export:AdsDDCreateRefIntegrity62=_AdsDDCreateRefIntegrity62")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreateRefIntegrity62(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5, UNSIGNED8* a6, UNSIGNED16 a7, UNSIGNED16 a8, UNSIGNED8* a9, UNSIGNED8* a10) {
    UNSIGNED32 rc = oadsimpl_AdsDDCreateRefIntegrity62(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
    arc_log_rc("AdsDDCreateRefIntegrity62", rc, 11, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7, (uintptr_t)a8, (uintptr_t)a9, (uintptr_t)a10);
    return rc;
}

/* ---- AdsDDCreateTrigger ---- */
#define AdsDDCreateTrigger oadsimpl_AdsDDCreateTrigger
extern UNSIGNED32 AdsDDCreateTrigger(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3, UNSIGNED8* h4, UNSIGNED8* h5, UNSIGNED8* h6, UNSIGNED8* h7);
#undef AdsDDCreateTrigger
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreateTrigger=_AdsDDCreateTrigger")
#pragma comment(linker, "/export:AdsDDCreateTrigger=_AdsDDCreateTrigger")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreateTrigger(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5, UNSIGNED8* a6, UNSIGNED8* a7, UNSIGNED8* a8, UNSIGNED8* a9, UNSIGNED8* a10) {
    UNSIGNED32 rc = oadsimpl_AdsDDCreateTrigger(a0, a1, a2, a3, a4, a5, a6, a7);
    arc_log_rc("AdsDDCreateTrigger", rc, 11, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7, (uintptr_t)a8, (uintptr_t)a9, (uintptr_t)a10);
    return rc;
}

/* ---- AdsDDGetPermissions ---- */
#define AdsDDGetPermissions oadsimpl_AdsDDGetPermissions
extern UNSIGNED32 ENTRYPOINT AdsDDGetPermissions(ADSHANDLE hConnect, UNSIGNED8* pucGrantee, UNSIGNED16 usObjectType, UNSIGNED8* pucObjectName, UNSIGNED8* pucParentName, UNSIGNED16 usGetInherited, UNSIGNED32* pulPermissions);
#undef AdsDDGetPermissions
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetPermissions=_AdsDDGetPermissions")
#pragma comment(linker, "/export:AdsDDGetPermissions=_AdsDDGetPermissions")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetPermissions(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED16 a5, UNSIGNED32* a6) {
    UNSIGNED32 rc = oadsimpl_AdsDDGetPermissions(a0, a1, a2, a3, a4, a5, a6);
    arc_log_rc("AdsDDGetPermissions", rc, 7, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6);
    return rc;
}

/* ---- AdsDDGetTableProperty ---- */
#define AdsDDGetTableProperty oadsimpl_AdsDDGetTableProperty
extern UNSIGNED32 ENTRYPOINT AdsDDGetTableProperty(ADSHANDLE hConnect, UNSIGNED8* pucTable, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16* pusLen);
#undef AdsDDGetTableProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetTableProperty=_AdsDDGetTableProperty")
#pragma comment(linker, "/export:AdsDDGetTableProperty=_AdsDDGetTableProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetTableProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16* a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDGetTableProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDGetTableProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDGetTriggerProperty ---- */
#define AdsDDGetTriggerProperty oadsimpl_AdsDDGetTriggerProperty
extern UNSIGNED32 ENTRYPOINT AdsDDGetTriggerProperty(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16* pusLen);
#undef AdsDDGetTriggerProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetTriggerProperty=_AdsDDGetTriggerProperty")
#pragma comment(linker, "/export:AdsDDGetTriggerProperty=_AdsDDGetTriggerProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetTriggerProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16* a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDGetTriggerProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDGetTriggerProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDGetViewProperty ---- */
#define AdsDDGetViewProperty oadsimpl_AdsDDGetViewProperty
extern UNSIGNED32 ENTRYPOINT AdsDDGetViewProperty(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16* pusLen);
#undef AdsDDGetViewProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetViewProperty=_AdsDDGetViewProperty")
#pragma comment(linker, "/export:AdsDDGetViewProperty=_AdsDDGetViewProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetViewProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16* a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDGetViewProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDGetViewProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDGrantPermission ---- */
#define AdsDDGrantPermission oadsimpl_AdsDDGrantPermission
extern UNSIGNED32 ENTRYPOINT AdsDDGrantPermission(ADSHANDLE hConnect, UNSIGNED16 usObjectType, UNSIGNED8* pucObjectName, UNSIGNED8* pucParentName, UNSIGNED8* pucGrantee, UNSIGNED32 ulPermissions);
#undef AdsDDGrantPermission
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGrantPermission=_AdsDDGrantPermission")
#pragma comment(linker, "/export:AdsDDGrantPermission=_AdsDDGrantPermission")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGrantPermission(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED32 a5) {
    UNSIGNED32 rc = oadsimpl_AdsDDGrantPermission(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsDDGrantPermission", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsDDRevokePermission ---- */
#define AdsDDRevokePermission oadsimpl_AdsDDRevokePermission
extern UNSIGNED32 ENTRYPOINT AdsDDRevokePermission(ADSHANDLE hConnect, UNSIGNED16 usObjectType, UNSIGNED8* pucObjectName, UNSIGNED8* pucParentName, UNSIGNED8* pucGrantee, UNSIGNED32 ulPermissions);
#undef AdsDDRevokePermission
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDRevokePermission=_AdsDDRevokePermission")
#pragma comment(linker, "/export:AdsDDRevokePermission=_AdsDDRevokePermission")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDRevokePermission(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED32 a5) {
    UNSIGNED32 rc = oadsimpl_AdsDDRevokePermission(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsDDRevokePermission", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsDDSetTableProperty ---- */
#define AdsDDSetTableProperty oadsimpl_AdsDDSetTableProperty
extern UNSIGNED32 AdsDDSetTableProperty(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3, UNSIGNED8* h4);
#undef AdsDDSetTableProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetTableProperty=_AdsDDSetTableProperty")
#pragma comment(linker, "/export:AdsDDSetTableProperty=_AdsDDSetTableProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetTableProperty(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5, UNSIGNED8* a6) {
    UNSIGNED32 rc = oadsimpl_AdsDDSetTableProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDSetTableProperty", rc, 7, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6);
    return rc;
}

/* ---- AdsDDSetTriggerProperty ---- */
#define AdsDDSetTriggerProperty oadsimpl_AdsDDSetTriggerProperty
extern UNSIGNED32 ENTRYPOINT AdsDDSetTriggerProperty(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16 usLen);
#undef AdsDDSetTriggerProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetTriggerProperty=_AdsDDSetTriggerProperty")
#pragma comment(linker, "/export:AdsDDSetTriggerProperty=_AdsDDSetTriggerProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetTriggerProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16 a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDSetTriggerProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDSetTriggerProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDSetUserProperty ---- */
#define AdsDDSetUserProperty oadsimpl_AdsDDSetUserProperty
extern UNSIGNED32 ENTRYPOINT AdsDDSetUserProperty(ADSHANDLE hConnect, UNSIGNED8* pucUser, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16 usLen);
#undef AdsDDSetUserProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetUserProperty=_AdsDDSetUserProperty")
#pragma comment(linker, "/export:AdsDDSetUserProperty=_AdsDDSetUserProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetUserProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16 a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDSetUserProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDSetUserProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDSetViewProperty ---- */
#define AdsDDSetViewProperty oadsimpl_AdsDDSetViewProperty
extern UNSIGNED32 ENTRYPOINT AdsDDSetViewProperty(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16 usLen);
#undef AdsDDSetViewProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetViewProperty=_AdsDDSetViewProperty")
#pragma comment(linker, "/export:AdsDDSetViewProperty=_AdsDDSetViewProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetViewProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16 a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDSetViewProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDSetViewProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsEvalAOF100 ---- */
#define AdsEvalAOF100 oadsimpl_AdsEvalAOF100
extern UNSIGNED32 ENTRYPOINT AdsEvalAOF100(ADSHANDLE hTable, void* pvExpr, UNSIGNED32 ulOptions, UNSIGNED16* pusOptLevel);
#undef AdsEvalAOF100
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEvalAOF100=_AdsEvalAOF100")
#pragma comment(linker, "/export:AdsEvalAOF100=_AdsEvalAOF100")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEvalAOF100(ADSHANDLE a0, void* a1, UNSIGNED32 a2, UNSIGNED16* a3) {
    UNSIGNED32 rc = oadsimpl_AdsEvalAOF100(a0, a1, a2, a3);
    arc_log_rc("AdsEvalAOF100", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsFetchWhere ---- */
#define AdsFetchWhere oadsimpl_AdsFetchWhere
extern UNSIGNED32 ENTRYPOINT AdsFetchWhere(ADSHANDLE hTbl, UNSIGNED8* pszExpr, UNSIGNED8* pszCols, UNSIGNED32 ulMaxRows, UNSIGNED32 ulFlags, ADSHANDLE* phResult);
#undef AdsFetchWhere
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFetchWhere=_AdsFetchWhere")
#pragma comment(linker, "/export:AdsFetchWhere=_AdsFetchWhere")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFetchWhere(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32 a3, UNSIGNED32 a4, ADSHANDLE* a5) {
    UNSIGNED32 rc = oadsimpl_AdsFetchWhere(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsFetchWhere", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsFetchWhereApplyRow ---- */
#define AdsFetchWhereApplyRow oadsimpl_AdsFetchWhereApplyRow
extern UNSIGNED32 ENTRYPOINT AdsFetchWhereApplyRow(ADSHANDLE hRes, UNSIGNED32 ulRow, ADSHANDLE hTbl);
#undef AdsFetchWhereApplyRow
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFetchWhereApplyRow=_AdsFetchWhereApplyRow")
#pragma comment(linker, "/export:AdsFetchWhereApplyRow=_AdsFetchWhereApplyRow")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFetchWhereApplyRow(ADSHANDLE a0, UNSIGNED32 a1, ADSHANDLE a2) {
    UNSIGNED32 rc = oadsimpl_AdsFetchWhereApplyRow(a0, a1, a2);
    arc_log_rc("AdsFetchWhereApplyRow", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsFetchWhereClose ---- */
#define AdsFetchWhereClose oadsimpl_AdsFetchWhereClose
extern UNSIGNED32 ENTRYPOINT AdsFetchWhereClose(ADSHANDLE hRes);
#undef AdsFetchWhereClose
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFetchWhereClose=_AdsFetchWhereClose")
#pragma comment(linker, "/export:AdsFetchWhereClose=_AdsFetchWhereClose")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFetchWhereClose(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsFetchWhereClose(a0);
    arc_log_rc("AdsFetchWhereClose", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsFetchWhereEof ---- */
#define AdsFetchWhereEof oadsimpl_AdsFetchWhereEof
extern UNSIGNED32 ENTRYPOINT AdsFetchWhereEof(ADSHANDLE hRes, UNSIGNED16* pbEof);
#undef AdsFetchWhereEof
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFetchWhereEof=_AdsFetchWhereEof")
#pragma comment(linker, "/export:AdsFetchWhereEof=_AdsFetchWhereEof")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFetchWhereEof(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsFetchWhereEof(a0, a1);
    arc_log_rc("AdsFetchWhereEof", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsFetchWhereField ---- */
#define AdsFetchWhereField oadsimpl_AdsFetchWhereField
extern UNSIGNED32 ENTRYPOINT AdsFetchWhereField(ADSHANDLE hRes, UNSIGNED32 ulRow, UNSIGNED8* pszCol, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsFetchWhereField
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFetchWhereField=_AdsFetchWhereField")
#pragma comment(linker, "/export:AdsFetchWhereField=_AdsFetchWhereField")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFetchWhereField(ADSHANDLE a0, UNSIGNED32 a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED16* a4) {
    UNSIGNED32 rc = oadsimpl_AdsFetchWhereField(a0, a1, a2, a3, a4);
    arc_log_rc("AdsFetchWhereField", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsFetchWhereRecno ---- */
#define AdsFetchWhereRecno oadsimpl_AdsFetchWhereRecno
extern UNSIGNED32 ENTRYPOINT AdsFetchWhereRecno(ADSHANDLE hRes, UNSIGNED32 ulRow, UNSIGNED32* pulRec);
#undef AdsFetchWhereRecno
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFetchWhereRecno=_AdsFetchWhereRecno")
#pragma comment(linker, "/export:AdsFetchWhereRecno=_AdsFetchWhereRecno")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFetchWhereRecno(ADSHANDLE a0, UNSIGNED32 a1, UNSIGNED32* a2) {
    UNSIGNED32 rc = oadsimpl_AdsFetchWhereRecno(a0, a1, a2);
    arc_log_rc("AdsFetchWhereRecno", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsFetchWhereRows ---- */
#define AdsFetchWhereRows oadsimpl_AdsFetchWhereRows
extern UNSIGNED32 ENTRYPOINT AdsFetchWhereRows(ADSHANDLE hRes, UNSIGNED32* pulRows);
#undef AdsFetchWhereRows
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFetchWhereRows=_AdsFetchWhereRows")
#pragma comment(linker, "/export:AdsFetchWhereRows=_AdsFetchWhereRows")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFetchWhereRows(ADSHANDLE a0, UNSIGNED32* a1) {
    UNSIGNED32 rc = oadsimpl_AdsFetchWhereRows(a0, a1);
    arc_log_rc("AdsFetchWhereRows", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsFindFirstTable62 ---- */
#define AdsFindFirstTable62 oadsimpl_AdsFindFirstTable62
extern UNSIGNED32 ENTRYPOINT AdsFindFirstTable62(ADSHANDLE hConnect, UNSIGNED8* pucFileMask, UNSIGNED8* pucFirstDD, UNSIGNED16* pusDDLen, UNSIGNED8* pucFirstFile, UNSIGNED16* pusFileLen, ADSHANDLE* phFind);
#undef AdsFindFirstTable62
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFindFirstTable62=_AdsFindFirstTable62")
#pragma comment(linker, "/export:AdsFindFirstTable62=_AdsFindFirstTable62")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFindFirstTable62(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16* a3, UNSIGNED8* a4, UNSIGNED16* a5, ADSHANDLE* a6) {
    UNSIGNED32 rc = oadsimpl_AdsFindFirstTable62(a0, a1, a2, a3, a4, a5, a6);
    arc_log_rc("AdsFindFirstTable62", rc, 7, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6);
    return rc;
}

/* ---- AdsFindNextTable62 ---- */
#define AdsFindNextTable62 oadsimpl_AdsFindNextTable62
extern UNSIGNED32 ENTRYPOINT AdsFindNextTable62(ADSHANDLE hConnect, ADSHANDLE hFind, UNSIGNED8* pucDDName, UNSIGNED16* pusDDLen, UNSIGNED8* pucFileName, UNSIGNED16* pusFileLen);
#undef AdsFindNextTable62
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFindNextTable62=_AdsFindNextTable62")
#pragma comment(linker, "/export:AdsFindNextTable62=_AdsFindNextTable62")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFindNextTable62(ADSHANDLE a0, ADSHANDLE a1, UNSIGNED8* a2, UNSIGNED16* a3, UNSIGNED8* a4, UNSIGNED16* a5) {
    UNSIGNED32 rc = oadsimpl_AdsFindNextTable62(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsFindNextTable62", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsGetAOF100 ---- */
#define AdsGetAOF100 oadsimpl_AdsGetAOF100
extern UNSIGNED32 ENTRYPOINT AdsGetAOF100(ADSHANDLE hTable, void* pvFilter, UNSIGNED16* pusLen, UNSIGNED32 ulOptions);
#undef AdsGetAOF100
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetAOF100=_AdsGetAOF100")
#pragma comment(linker, "/export:AdsGetAOF100=_AdsGetAOF100")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetAOF100(ADSHANDLE a0, void* a1, UNSIGNED16* a2, UNSIGNED32 a3) {
    UNSIGNED32 rc = oadsimpl_AdsGetAOF100(a0, a1, a2, a3);
    arc_log_rc("AdsGetAOF100", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsGetAOFOptLevel100 ---- */
#define AdsGetAOFOptLevel100 oadsimpl_AdsGetAOFOptLevel100
extern UNSIGNED32 ENTRYPOINT AdsGetAOFOptLevel100(ADSHANDLE hTable, UNSIGNED16* pusLevel, void* pvBuf, UNSIGNED16* pusLen, UNSIGNED32 ulOptions);
#undef AdsGetAOFOptLevel100
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetAOFOptLevel100=_AdsGetAOFOptLevel100")
#pragma comment(linker, "/export:AdsGetAOFOptLevel100=_AdsGetAOFOptLevel100")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetAOFOptLevel100(ADSHANDLE a0, UNSIGNED16* a1, void* a2, UNSIGNED16* a3, UNSIGNED32 a4) {
    UNSIGNED32 rc = oadsimpl_AdsGetAOFOptLevel100(a0, a1, a2, a3, a4);
    arc_log_rc("AdsGetAOFOptLevel100", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsGetDecimals ---- */
#define AdsGetDecimals oadsimpl_AdsGetDecimals
extern UNSIGNED32 ENTRYPOINT AdsGetDecimals(UNSIGNED16* pusDecimals);
#undef AdsGetDecimals
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetDecimals=_AdsGetDecimals")
#pragma comment(linker, "/export:AdsGetDecimals=_AdsGetDecimals")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetDecimals(UNSIGNED16* a0) {
    UNSIGNED32 rc = oadsimpl_AdsGetDecimals(a0);
    arc_log_rc("AdsGetDecimals", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsGetTableHandle ---- */
#define AdsGetTableHandle oadsimpl_AdsGetTableHandle
extern UNSIGNED32 AdsGetTableHandle(UNSIGNED8* a0, UNSIGNED8* a1);
#undef AdsGetTableHandle
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableHandle=_AdsGetTableHandle")
#pragma comment(linker, "/export:AdsGetTableHandle=_AdsGetTableHandle")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableHandle(UNSIGNED8* a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetTableHandle(a0, a1);
    arc_log_rc("AdsGetTableHandle", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGotoBOF ---- */
#define AdsGotoBOF oadsimpl_AdsGotoBOF
extern UNSIGNED32 ENTRYPOINT AdsGotoBOF(ADSHANDLE hTable);
#undef AdsGotoBOF
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGotoBOF=_AdsGotoBOF")
#pragma comment(linker, "/export:AdsGotoBOF=_AdsGotoBOF")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGotoBOF(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsGotoBOF(a0);
    arc_log_rc("AdsGotoBOF", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsGotoEOF ---- */
#define AdsGotoEOF oadsimpl_AdsGotoEOF
extern UNSIGNED32 ENTRYPOINT AdsGotoEOF(ADSHANDLE hTable);
#undef AdsGotoEOF
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGotoEOF=_AdsGotoEOF")
#pragma comment(linker, "/export:AdsGotoEOF=_AdsGotoEOF")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGotoEOF(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsGotoEOF(a0);
    arc_log_rc("AdsGotoEOF", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsIsTableTransactionFree ---- */
#define AdsIsTableTransactionFree oadsimpl_AdsIsTableTransactionFree
extern UNSIGNED32 ENTRYPOINT AdsIsTableTransactionFree(ADSHANDLE hTable, UNSIGNED16* pusTransFree);
#undef AdsIsTableTransactionFree
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsTableTransactionFree=_AdsIsTableTransactionFree")
#pragma comment(linker, "/export:AdsIsTableTransactionFree=_AdsIsTableTransactionFree")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsTableTransactionFree(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsIsTableTransactionFree(a0, a1);
    arc_log_rc("AdsIsTableTransactionFree", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsMgKillUser90 ---- */
#define AdsMgKillUser90 oadsimpl_AdsMgKillUser90
extern UNSIGNED32 AdsMgKillUser90(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3);
#undef AdsMgKillUser90
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgKillUser90=_AdsMgKillUser90")
#pragma comment(linker, "/export:AdsMgKillUser90=_AdsMgKillUser90")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgKillUser90(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5) {
    UNSIGNED32 rc = oadsimpl_AdsMgKillUser90(a0, a1, a2, a3);
    arc_log_rc("AdsMgKillUser90", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsOpenTable101 ---- */
#define AdsOpenTable101 oadsimpl_AdsOpenTable101
extern UNSIGNED32 ENTRYPOINT AdsOpenTable101(ADSHANDLE hConnect, UNSIGNED8* pucName, ADSHANDLE* phTable);
#undef AdsOpenTable101
#pragma comment(linker, "/alternatename:_oadsimpl_AdsOpenTable101=_AdsOpenTable101")
#pragma comment(linker, "/export:AdsOpenTable101=_AdsOpenTable101")
__declspec(dllexport) UNSIGNED32 __stdcall AdsOpenTable101(ADSHANDLE a0, UNSIGNED8* a1, ADSHANDLE* a2) {
    UNSIGNED32 rc = oadsimpl_AdsOpenTable101(a0, a1, a2);
    arc_log_rc("AdsOpenTable101", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsOpenTable90 ---- */
#define AdsOpenTable90 oadsimpl_AdsOpenTable90
extern UNSIGNED32 ENTRYPOINT AdsOpenTable90(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucAlias, UNSIGNED16 usTableType, UNSIGNED16 usCharType, UNSIGNED16 usLockType, UNSIGNED16 usCheckRights, UNSIGNED32 ulOptions, UNSIGNED8* pucCollation, ADSHANDLE* phTable);
#undef AdsOpenTable90
#pragma comment(linker, "/alternatename:_oadsimpl_AdsOpenTable90=_AdsOpenTable90")
#pragma comment(linker, "/export:AdsOpenTable90=_AdsOpenTable90")
__declspec(dllexport) UNSIGNED32 __stdcall AdsOpenTable90(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, UNSIGNED16 a4, UNSIGNED16 a5, UNSIGNED16 a6, UNSIGNED32 a7, UNSIGNED8* a8, ADSHANDLE* a9) {
    UNSIGNED32 rc = oadsimpl_AdsOpenTable90(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9);
    arc_log_rc("AdsOpenTable90", rc, 10, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7, (uintptr_t)a8, (uintptr_t)a9);
    return rc;
}

/* ---- AdsRegisterCallbackFunction101 ---- */
#define AdsRegisterCallbackFunction101 oadsimpl_AdsRegisterCallbackFunction101
extern UNSIGNED32 ENTRYPOINT AdsRegisterCallbackFunction101(void* pCallback, SIGNED64 qCallbackID);
#undef AdsRegisterCallbackFunction101
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRegisterCallbackFunction101=_AdsRegisterCallbackFunction101")
#pragma comment(linker, "/export:AdsRegisterCallbackFunction101=_AdsRegisterCallbackFunction101")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRegisterCallbackFunction101(void* a0, SIGNED64 a1) {
    UNSIGNED32 rc = oadsimpl_AdsRegisterCallbackFunction101(a0, a1);
    arc_log_rc("AdsRegisterCallbackFunction101", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsRestructureTable120 ---- */
#define AdsRestructureTable120 oadsimpl_AdsRestructureTable120
extern UNSIGNED32 ENTRYPOINT AdsRestructureTable120(ADSHANDLE hConnect, UNSIGNED8* pucTableName, UNSIGNED8* pucPassword, UNSIGNED16 usTableType, UNSIGNED16 usCharType, UNSIGNED16 usLockType, UNSIGNED16 usCheckRights, UNSIGNED8* pucAddFields, UNSIGNED8* pucDeleteFields, UNSIGNED8* pucChangeFields, UNSIGNED8* pucCollation, UNSIGNED32 ulMemoBlockSize, UNSIGNED32 ulOptions);
#undef AdsRestructureTable120
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRestructureTable120=_AdsRestructureTable120")
#pragma comment(linker, "/export:AdsRestructureTable120=_AdsRestructureTable120")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRestructureTable120(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, UNSIGNED16 a4, UNSIGNED16 a5, UNSIGNED16 a6, UNSIGNED8* a7, UNSIGNED8* a8, UNSIGNED8* a9, UNSIGNED8* a10, UNSIGNED32 a11, UNSIGNED32 a12) {
    UNSIGNED32 rc = oadsimpl_AdsRestructureTable120(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
    arc_log_rc("AdsRestructureTable120", rc, 13, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7, (uintptr_t)a8, (uintptr_t)a9, (uintptr_t)a10, (uintptr_t)a11, (uintptr_t)a12);
    return rc;
}

/* ---- AdsRestructureTable90 ---- */
#define AdsRestructureTable90 oadsimpl_AdsRestructureTable90
extern UNSIGNED32 ENTRYPOINT AdsRestructureTable90(ADSHANDLE hConnect, UNSIGNED8* pucTableName, UNSIGNED8* pucPassword, UNSIGNED16 usTableType, UNSIGNED16 usCharType, UNSIGNED16 usLockType, UNSIGNED16 usCheckRights, UNSIGNED8* pucAddFields, UNSIGNED8* pucDeleteFields, UNSIGNED8* pucChangeFields, UNSIGNED8* pucCollation);
#undef AdsRestructureTable90
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRestructureTable90=_AdsRestructureTable90")
#pragma comment(linker, "/export:AdsRestructureTable90=_AdsRestructureTable90")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRestructureTable90(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, UNSIGNED16 a4, UNSIGNED16 a5, UNSIGNED16 a6, UNSIGNED8* a7, UNSIGNED8* a8, UNSIGNED8* a9, UNSIGNED8* a10) {
    UNSIGNED32 rc = oadsimpl_AdsRestructureTable90(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
    arc_log_rc("AdsRestructureTable90", rc, 11, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7, (uintptr_t)a8, (uintptr_t)a9, (uintptr_t)a10);
    return rc;
}

/* ---- AdsSetAOF100 ---- */
#define AdsSetAOF100 oadsimpl_AdsSetAOF100
extern UNSIGNED32 ENTRYPOINT AdsSetAOF100(ADSHANDLE hTable, void* pvFilter, UNSIGNED32 ulOptions);
#undef AdsSetAOF100
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetAOF100=_AdsSetAOF100")
#pragma comment(linker, "/export:AdsSetAOF100=_AdsSetAOF100")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetAOF100(ADSHANDLE a0, void* a1, UNSIGNED32 a2) {
    UNSIGNED32 rc = oadsimpl_AdsSetAOF100(a0, a1, a2);
    arc_log_rc("AdsSetAOF100", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsSetAutoCommit ---- */
#define AdsSetAutoCommit oadsimpl_AdsSetAutoCommit
extern UNSIGNED32 ENTRYPOINT AdsSetAutoCommit(ADSHANDLE hConnect, SIGNED32 nThreshold);
#undef AdsSetAutoCommit
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetAutoCommit=_AdsSetAutoCommit")
#pragma comment(linker, "/export:AdsSetAutoCommit=_AdsSetAutoCommit")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetAutoCommit(ADSHANDLE a0, SIGNED32 a1) {
    UNSIGNED32 rc = oadsimpl_AdsSetAutoCommit(a0, a1);
    arc_log_rc("AdsSetAutoCommit", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsSetDate ---- */
#define AdsSetDate oadsimpl_AdsSetDate
extern UNSIGNED32 ENTRYPOINT AdsSetDate(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED8* pucValue, UNSIGNED16 usLen);
#undef AdsSetDate
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetDate=_AdsSetDate")
#pragma comment(linker, "/export:AdsSetDate=_AdsSetDate")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetDate(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3) {
    UNSIGNED32 rc = oadsimpl_AdsSetDate(a0, a1, a2, a3);
    arc_log_rc("AdsSetDate", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsSetExact22 ---- */
#define AdsSetExact22 oadsimpl_AdsSetExact22
extern UNSIGNED32 ENTRYPOINT AdsSetExact22(ADSHANDLE hObj, UNSIGNED16 bIgnoreSpaces);
#undef AdsSetExact22
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetExact22=_AdsSetExact22")
#pragma comment(linker, "/export:AdsSetExact22=_AdsSetExact22")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetExact22(ADSHANDLE a0, UNSIGNED16 a1) {
    UNSIGNED32 rc = oadsimpl_AdsSetExact22(a0, a1);
    arc_log_rc("AdsSetExact22", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsSetFieldW ---- */
#define AdsSetFieldW oadsimpl_AdsSetFieldW
extern UNSIGNED32 ENTRYPOINT AdsSetFieldW(ADSHANDLE hObj, UNSIGNED8* pucFldId, UNSIGNED16* pwcBuf, UNSIGNED32 ulLen);
#undef AdsSetFieldW
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetFieldW=_AdsSetFieldW")
#pragma comment(linker, "/export:AdsSetFieldW=_AdsSetFieldW")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetFieldW(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2, UNSIGNED32 a3) {
    UNSIGNED32 rc = oadsimpl_AdsSetFieldW(a0, a1, a2, a3);
    arc_log_rc("AdsSetFieldW", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsSetFilter100 ---- */
#define AdsSetFilter100 oadsimpl_AdsSetFilter100
extern UNSIGNED32 ENTRYPOINT AdsSetFilter100(ADSHANDLE hTable, void* pvExpr, UNSIGNED32 ulOptions);
#undef AdsSetFilter100
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetFilter100=_AdsSetFilter100")
#pragma comment(linker, "/export:AdsSetFilter100=_AdsSetFilter100")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetFilter100(ADSHANDLE a0, void* a1, UNSIGNED32 a2) {
    UNSIGNED32 rc = oadsimpl_AdsSetFilter100(a0, a1, a2);
    arc_log_rc("AdsSetFilter100", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsSetLong ---- */
#define AdsSetLong oadsimpl_AdsSetLong
extern UNSIGNED32 ENTRYPOINT AdsSetLong(ADSHANDLE hTable, UNSIGNED8* pucField, SIGNED32 lVal);
#undef AdsSetLong
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetLong=_AdsSetLong")
#pragma comment(linker, "/export:AdsSetLong=_AdsSetLong")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetLong(ADSHANDLE a0, UNSIGNED8* a1, SIGNED32 a2) {
    UNSIGNED32 rc = oadsimpl_AdsSetLong(a0, a1, a2);
    arc_log_rc("AdsSetLong", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsSetProperty ---- */
#define AdsSetProperty oadsimpl_AdsSetProperty
extern UNSIGNED32 ENTRYPOINT AdsSetProperty(ADSHANDLE hObj, UNSIGNED32 ulOperation, UNSIGNED32* pulValue);
#undef AdsSetProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetProperty=_AdsSetProperty")
#pragma comment(linker, "/export:AdsSetProperty=_AdsSetProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetProperty(ADSHANDLE a0, UNSIGNED32 a1, UNSIGNED32* a2) {
    UNSIGNED32 rc = oadsimpl_AdsSetProperty(a0, a1, a2);
    arc_log_rc("AdsSetProperty", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsSetProperty90 ---- */
#define AdsSetProperty90 oadsimpl_AdsSetProperty90
extern UNSIGNED32 AdsSetProperty90(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2);
#undef AdsSetProperty90
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetProperty90=_AdsSetProperty90")
#pragma comment(linker, "/export:AdsSetProperty90=_AdsSetProperty90")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetProperty90(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsSetProperty90(a0, a1, a2);
    arc_log_rc("AdsSetProperty90", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsSetRightsChecking ---- */
#define AdsSetRightsChecking oadsimpl_AdsSetRightsChecking
extern UNSIGNED32 ENTRYPOINT AdsSetRightsChecking(UNSIGNED32 ulOptions);
#undef AdsSetRightsChecking
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetRightsChecking=_AdsSetRightsChecking")
#pragma comment(linker, "/export:AdsSetRightsChecking=_AdsSetRightsChecking")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetRightsChecking(UNSIGNED32 a0) {
    UNSIGNED32 rc = oadsimpl_AdsSetRightsChecking(a0);
    arc_log_rc("AdsSetRightsChecking", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsSetSQLTimeout ---- */
#define AdsSetSQLTimeout oadsimpl_AdsSetSQLTimeout
extern UNSIGNED32 ENTRYPOINT AdsSetSQLTimeout(ADSHANDLE hObj, UNSIGNED32 ulTimeout);
#undef AdsSetSQLTimeout
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetSQLTimeout=_AdsSetSQLTimeout")
#pragma comment(linker, "/export:AdsSetSQLTimeout=_AdsSetSQLTimeout")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetSQLTimeout(ADSHANDLE a0, UNSIGNED32 a1) {
    UNSIGNED32 rc = oadsimpl_AdsSetSQLTimeout(a0, a1);
    arc_log_rc("AdsSetSQLTimeout", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsSetTableTransactionFree ---- */
#define AdsSetTableTransactionFree oadsimpl_AdsSetTableTransactionFree
extern UNSIGNED32 ENTRYPOINT AdsSetTableTransactionFree(ADSHANDLE hTable, UNSIGNED16 usTransFree);
#undef AdsSetTableTransactionFree
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetTableTransactionFree=_AdsSetTableTransactionFree")
#pragma comment(linker, "/export:AdsSetTableTransactionFree=_AdsSetTableTransactionFree")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetTableTransactionFree(ADSHANDLE a0, UNSIGNED16 a1) {
    UNSIGNED32 rc = oadsimpl_AdsSetTableTransactionFree(a0, a1);
    arc_log_rc("AdsSetTableTransactionFree", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsSetTimeStampRaw ---- */
#define AdsSetTimeStampRaw oadsimpl_AdsSetTimeStampRaw
extern UNSIGNED32 ENTRYPOINT AdsSetTimeStampRaw(ADSHANDLE hObj, UNSIGNED8* pucFldId, UNSIGNED8* pucBuf, UNSIGNED32 ulLen);
#undef AdsSetTimeStampRaw
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetTimeStampRaw=_AdsSetTimeStampRaw")
#pragma comment(linker, "/export:AdsSetTimeStampRaw=_AdsSetTimeStampRaw")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetTimeStampRaw(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32 a3) {
    UNSIGNED32 rc = oadsimpl_AdsSetTimeStampRaw(a0, a1, a2, a3);
    arc_log_rc("AdsSetTimeStampRaw", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsStmtSetTableCharType ---- */
#define AdsStmtSetTableCharType oadsimpl_AdsStmtSetTableCharType
extern UNSIGNED32 ENTRYPOINT AdsStmtSetTableCharType(ADSHANDLE hStatement, UNSIGNED16 usCharType);
#undef AdsStmtSetTableCharType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStmtSetTableCharType=_AdsStmtSetTableCharType")
#pragma comment(linker, "/export:AdsStmtSetTableCharType=_AdsStmtSetTableCharType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStmtSetTableCharType(ADSHANDLE a0, UNSIGNED16 a1) {
    UNSIGNED32 rc = oadsimpl_AdsStmtSetTableCharType(a0, a1);
    arc_log_rc("AdsStmtSetTableCharType", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsStmtSetTableRights ---- */
#define AdsStmtSetTableRights oadsimpl_AdsStmtSetTableRights
extern UNSIGNED32 ENTRYPOINT AdsStmtSetTableRights(ADSHANDLE hStatement, UNSIGNED16 usCheckRights);
#undef AdsStmtSetTableRights
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStmtSetTableRights=_AdsStmtSetTableRights")
#pragma comment(linker, "/export:AdsStmtSetTableRights=_AdsStmtSetTableRights")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStmtSetTableRights(ADSHANDLE a0, UNSIGNED16 a1) {
    UNSIGNED32 rc = oadsimpl_AdsStmtSetTableRights(a0, a1);
    arc_log_rc("AdsStmtSetTableRights", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsTestLogin ---- */
#define AdsTestLogin oadsimpl_AdsTestLogin
extern UNSIGNED32 ENTRYPOINT AdsTestLogin(UNSIGNED8* pucServer, UNSIGNED16 usServerType, UNSIGNED8* pucUser, UNSIGNED8* pucPwd, UNSIGNED32 ulOptions);
#undef AdsTestLogin
#pragma comment(linker, "/alternatename:_oadsimpl_AdsTestLogin=_AdsTestLogin")
#pragma comment(linker, "/export:AdsTestLogin=_AdsTestLogin")
__declspec(dllexport) UNSIGNED32 __stdcall AdsTestLogin(UNSIGNED8* a0, UNSIGNED16 a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED32 a4) {
    UNSIGNED32 rc = oadsimpl_AdsTestLogin(a0, a1, a2, a3, a4);
    arc_log_rc("AdsTestLogin", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsStudioPort ---- */
#define AdsStudioPort oadsimpl_AdsStudioPort
extern UNSIGNED32 ENTRYPOINT AdsStudioPort(UNSIGNED16* pusPort);
#undef AdsStudioPort
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStudioPort=_AdsStudioPort")
#pragma comment(linker, "/export:AdsStudioPort=_AdsStudioPort")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStudioPort(UNSIGNED16* a0) {
    UNSIGNED32 rc = oadsimpl_AdsStudioPort(a0);
    arc_log_rc("AdsStudioPort", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsStudioStart ---- */
#define AdsStudioStart oadsimpl_AdsStudioStart
extern UNSIGNED32 ENTRYPOINT AdsStudioStart(UNSIGNED16 usPort, UNSIGNED8* pucDataDir);
#undef AdsStudioStart
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStudioStart=_AdsStudioStart")
#pragma comment(linker, "/export:AdsStudioStart=_AdsStudioStart")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStudioStart(UNSIGNED16 a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsStudioStart(a0, a1);
    arc_log_rc("AdsStudioStart", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsStudioStop ---- */
#define AdsStudioStop oadsimpl_AdsStudioStop
extern UNSIGNED32 ENTRYPOINT AdsStudioStop(void);
#undef AdsStudioStop
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStudioStop=_AdsStudioStop")
#pragma comment(linker, "/export:AdsStudioStop=_AdsStudioStop")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStudioStop(void) {
    UNSIGNED32 rc = oadsimpl_AdsStudioStop();
    arc_log_rc("AdsStudioStop", rc, 0);
    return rc;
}

/* ---- AdsTestRecLocks ---- */
#define AdsTestRecLocks oadsimpl_AdsTestRecLocks
extern UNSIGNED32 ENTRYPOINT AdsTestRecLocks(ADSHANDLE hTable);
#undef AdsTestRecLocks
#pragma comment(linker, "/alternatename:_oadsimpl_AdsTestRecLocks=_AdsTestRecLocks")
#pragma comment(linker, "/export:AdsTestRecLocks=_AdsTestRecLocks")
__declspec(dllexport) UNSIGNED32 __stdcall AdsTestRecLocks(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsTestRecLocks(a0);
    arc_log_rc("AdsTestRecLocks", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsThreadExit ---- */
#define AdsThreadExit oadsimpl_AdsThreadExit
extern UNSIGNED32 ENTRYPOINT AdsThreadExit(void);
#undef AdsThreadExit
#pragma comment(linker, "/alternatename:_oadsimpl_AdsThreadExit=_AdsThreadExit")
#pragma comment(linker, "/export:AdsThreadExit=_AdsThreadExit")
__declspec(dllexport) UNSIGNED32 __stdcall AdsThreadExit(void) {
    UNSIGNED32 rc = oadsimpl_AdsThreadExit();
    arc_log_rc("AdsThreadExit", rc, 0);
    return rc;
}

/* ---- AdsUnlockRecord ---- */
#define AdsUnlockRecord oadsimpl_AdsUnlockRecord
extern UNSIGNED32 ENTRYPOINT AdsUnlockRecord(ADSHANDLE hTable, UNSIGNED32 ulRecord);
#undef AdsUnlockRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsUnlockRecord=_AdsUnlockRecord")
#pragma comment(linker, "/export:AdsUnlockRecord=_AdsUnlockRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsUnlockRecord(ADSHANDLE a0, UNSIGNED32 a1) {
    UNSIGNED32 rc = oadsimpl_AdsUnlockRecord(a0, a1);
    arc_log_rc("AdsUnlockRecord", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsUnlockTable ---- */
#define AdsUnlockTable oadsimpl_AdsUnlockTable
extern UNSIGNED32 ENTRYPOINT AdsUnlockTable(ADSHANDLE hTable);
#undef AdsUnlockTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsUnlockTable=_AdsUnlockTable")
#pragma comment(linker, "/export:AdsUnlockTable=_AdsUnlockTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsUnlockTable(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsUnlockTable(a0);
    arc_log_rc("AdsUnlockTable", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsVerifySQL ---- */
#define AdsVerifySQL oadsimpl_AdsVerifySQL
extern UNSIGNED32 ENTRYPOINT AdsVerifySQL(ADSHANDLE hStatement, UNSIGNED8* pucSQL);
#undef AdsVerifySQL
#pragma comment(linker, "/alternatename:_oadsimpl_AdsVerifySQL=_AdsVerifySQL")
#pragma comment(linker, "/export:AdsVerifySQL=_AdsVerifySQL")
__declspec(dllexport) UNSIGNED32 __stdcall AdsVerifySQL(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsVerifySQL(a0, a1);
    arc_log_rc("AdsVerifySQL", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsVerifySQLW ---- */
#define AdsVerifySQLW oadsimpl_AdsVerifySQLW
extern UNSIGNED32 ENTRYPOINT AdsVerifySQLW(ADSHANDLE hStatement, UNSIGNED16* pwcSQL);
#undef AdsVerifySQLW
#pragma comment(linker, "/alternatename:_oadsimpl_AdsVerifySQLW=_AdsVerifySQLW")
#pragma comment(linker, "/export:AdsVerifySQLW=_AdsVerifySQLW")
__declspec(dllexport) UNSIGNED32 __stdcall AdsVerifySQLW(ADSHANDLE a0, UNSIGNED16* a1) {
    UNSIGNED32 rc = oadsimpl_AdsVerifySQLW(a0, a1);
    arc_log_rc("AdsVerifySQLW", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsWriteAllRecords ---- */
#define AdsWriteAllRecords oadsimpl_AdsWriteAllRecords
extern UNSIGNED32 ENTRYPOINT AdsWriteAllRecords(void);
#undef AdsWriteAllRecords
#pragma comment(linker, "/alternatename:_oadsimpl_AdsWriteAllRecords=_AdsWriteAllRecords")
#pragma comment(linker, "/export:AdsWriteAllRecords=_AdsWriteAllRecords")
__declspec(dllexport) UNSIGNED32 __stdcall AdsWriteAllRecords(void) {
    UNSIGNED32 rc = oadsimpl_AdsWriteAllRecords();
    arc_log_rc("AdsWriteAllRecords", rc, 0);
    return rc;
}

/* ---- AdsWriteRecord ---- */
#define AdsWriteRecord oadsimpl_AdsWriteRecord
extern UNSIGNED32 ENTRYPOINT AdsWriteRecord(ADSHANDLE hTable);
#undef AdsWriteRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsWriteRecord=_AdsWriteRecord")
#pragma comment(linker, "/export:AdsWriteRecord=_AdsWriteRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsWriteRecord(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsWriteRecord(a0);
    arc_log_rc("AdsWriteRecord", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsZapTable ---- */
#define AdsZapTable oadsimpl_AdsZapTable
extern UNSIGNED32 ENTRYPOINT AdsZapTable(ADSHANDLE hTable);
#undef AdsZapTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsZapTable=_AdsZapTable")
#pragma comment(linker, "/export:AdsZapTable=_AdsZapTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsZapTable(ADSHANDLE a0) {
    UNSIGNED32 rc = oadsimpl_AdsZapTable(a0);
    arc_log_rc("AdsZapTable", rc, 1, (uintptr_t)a0);
    return rc;
}


/* ---- AdsAccessVfpSystemField ---- */
#define AdsAccessVfpSystemField oadsimpl_AdsAccessVfpSystemField
extern UNSIGNED32 AdsAccessVfpSystemField(void);
#undef AdsAccessVfpSystemField
#pragma comment(linker, "/alternatename:_oadsimpl_AdsAccessVfpSystemField=_AdsAccessVfpSystemField")
#pragma comment(linker, "/export:AdsAccessVfpSystemField=_AdsAccessVfpSystemField")
__declspec(dllexport) UNSIGNED32 __stdcall AdsAccessVfpSystemField(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4) {
    UNSIGNED32 rc = oadsimpl_AdsAccessVfpSystemField();
    arc_log_rc("AdsAccessVfpSystemField", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsBuildRawKey100 ---- */
#define AdsBuildRawKey100 oadsimpl_AdsBuildRawKey100
extern UNSIGNED32 AdsBuildRawKey100(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3);
#undef AdsBuildRawKey100
#pragma comment(linker, "/alternatename:_oadsimpl_AdsBuildRawKey100=_AdsBuildRawKey100")
#pragma comment(linker, "/export:AdsBuildRawKey100=_AdsBuildRawKey100")
__declspec(dllexport) UNSIGNED32 __stdcall AdsBuildRawKey100(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsBuildRawKey100(a0, a1, a2, a3);
    arc_log_rc("AdsBuildRawKey100", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsConvertDateToJulian ---- */
#define AdsConvertDateToJulian oadsimpl_AdsConvertDateToJulian
extern UNSIGNED32 AdsConvertDateToJulian(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3);
#undef AdsConvertDateToJulian
#pragma comment(linker, "/alternatename:_oadsimpl_AdsConvertDateToJulian=_AdsConvertDateToJulian")
#pragma comment(linker, "/export:AdsConvertDateToJulian=_AdsConvertDateToJulian")
__declspec(dllexport) UNSIGNED32 __stdcall AdsConvertDateToJulian(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsConvertDateToJulian(a0, a1, a2, a3);
    arc_log_rc("AdsConvertDateToJulian", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsConvertJulianToString ---- */
#define AdsConvertJulianToString oadsimpl_AdsConvertJulianToString
extern UNSIGNED32 AdsConvertJulianToString(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3);
#undef AdsConvertJulianToString
#pragma comment(linker, "/alternatename:_oadsimpl_AdsConvertJulianToString=_AdsConvertJulianToString")
#pragma comment(linker, "/export:AdsConvertJulianToString=_AdsConvertJulianToString")
__declspec(dllexport) UNSIGNED32 __stdcall AdsConvertJulianToString(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsConvertJulianToString(a0, a1, a2, a3);
    arc_log_rc("AdsConvertJulianToString", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsConvertStringToJulian ---- */
#define AdsConvertStringToJulian oadsimpl_AdsConvertStringToJulian
extern UNSIGNED32 AdsConvertStringToJulian(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3);
#undef AdsConvertStringToJulian
#pragma comment(linker, "/alternatename:_oadsimpl_AdsConvertStringToJulian=_AdsConvertStringToJulian")
#pragma comment(linker, "/export:AdsConvertStringToJulian=_AdsConvertStringToJulian")
__declspec(dllexport) UNSIGNED32 __stdcall AdsConvertStringToJulian(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsConvertStringToJulian(a0, a1, a2, a3);
    arc_log_rc("AdsConvertStringToJulian", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsDDAddView100 ---- */
#define AdsDDAddView100 oadsimpl_AdsDDAddView100
extern UNSIGNED32 AdsDDAddView100(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3);
#undef AdsDDAddView100
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDAddView100=_AdsDDAddView100")
#pragma comment(linker, "/export:AdsDDAddView100=_AdsDDAddView100")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDAddView100(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsDDAddView100(a0, a1, a2, a3);
    arc_log_rc("AdsDDAddView100", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsDDCreate101 ---- */
#define AdsDDCreate101 oadsimpl_AdsDDCreate101
extern UNSIGNED32 AdsDDCreate101(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2);
#undef AdsDDCreate101
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreate101=_AdsDDCreate101")
#pragma comment(linker, "/export:AdsDDCreate101=_AdsDDCreate101")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreate101(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsDDCreate101(a0, a1, a2);
    arc_log_rc("AdsDDCreate101", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsDDCreatePublication ---- */
#define AdsDDCreatePublication oadsimpl_AdsDDCreatePublication
extern UNSIGNED32 AdsDDCreatePublication(void);
#undef AdsDDCreatePublication
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreatePublication=_AdsDDCreatePublication")
#pragma comment(linker, "/export:AdsDDCreatePublication=_AdsDDCreatePublication")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreatePublication(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsDDCreatePublication();
    arc_log_rc("AdsDDCreatePublication", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsDDCreateSubscription ---- */
#define AdsDDCreateSubscription oadsimpl_AdsDDCreateSubscription
extern UNSIGNED32 AdsDDCreateSubscription(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5, UNSIGNED8* a6, UNSIGNED8* a7, UNSIGNED8* a8, UNSIGNED8* a9);
#undef AdsDDCreateSubscription
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreateSubscription=_AdsDDCreateSubscription")
#pragma comment(linker, "/export:AdsDDCreateSubscription=_AdsDDCreateSubscription")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreateSubscription(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5, UNSIGNED8* a6, UNSIGNED8* a7, UNSIGNED8* a8, UNSIGNED8* a9) {
    UNSIGNED32 rc = oadsimpl_AdsDDCreateSubscription(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9);
    arc_log_rc("AdsDDCreateSubscription", rc, 10, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7, (uintptr_t)a8, (uintptr_t)a9);
    return rc;
}

/* ---- AdsDDCreateTrigger100 ---- */
#define AdsDDCreateTrigger100 oadsimpl_AdsDDCreateTrigger100
extern UNSIGNED32 AdsDDCreateTrigger100(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3, UNSIGNED8* h4, UNSIGNED8* h5, UNSIGNED8* h6, UNSIGNED8* h7, UNSIGNED8* h8);
#undef AdsDDCreateTrigger100
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreateTrigger100=_AdsDDCreateTrigger100")
#pragma comment(linker, "/export:AdsDDCreateTrigger100=_AdsDDCreateTrigger100")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreateTrigger100(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5, UNSIGNED8* a6, UNSIGNED8* a7, UNSIGNED8* a8, UNSIGNED8* a9, UNSIGNED8* a10) {
    UNSIGNED32 rc = oadsimpl_AdsDDCreateTrigger100(a0, a1, a2, a3, a4, a5, a6, a7, a8);
    arc_log_rc("AdsDDCreateTrigger100", rc, 11, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7, (uintptr_t)a8, (uintptr_t)a9, (uintptr_t)a10);
    return rc;
}

/* ---- AdsDDDeleteArticle ---- */
#define AdsDDDeleteArticle oadsimpl_AdsDDDeleteArticle
extern UNSIGNED32 AdsDDDeleteArticle(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2);
#undef AdsDDDeleteArticle
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDDeleteArticle=_AdsDDDeleteArticle")
#pragma comment(linker, "/export:AdsDDDeleteArticle=_AdsDDDeleteArticle")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDDeleteArticle(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsDDDeleteArticle(a0, a1, a2);
    arc_log_rc("AdsDDDeleteArticle", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsDDDeleteIndex ---- */
#define AdsDDDeleteIndex oadsimpl_AdsDDDeleteIndex
extern UNSIGNED32 AdsDDDeleteIndex(void);
#undef AdsDDDeleteIndex
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDDeleteIndex=_AdsDDDeleteIndex")
#pragma comment(linker, "/export:AdsDDDeleteIndex=_AdsDDDeleteIndex")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDDeleteIndex(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsDDDeleteIndex();
    arc_log_rc("AdsDDDeleteIndex", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsDDDeletePublication ---- */
#define AdsDDDeletePublication oadsimpl_AdsDDDeletePublication
extern UNSIGNED32 AdsDDDeletePublication(UNSIGNED8* a0, UNSIGNED8* a1);
#undef AdsDDDeletePublication
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDDeletePublication=_AdsDDDeletePublication")
#pragma comment(linker, "/export:AdsDDDeletePublication=_AdsDDDeletePublication")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDDeletePublication(UNSIGNED8* a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsDDDeletePublication(a0, a1);
    arc_log_rc("AdsDDDeletePublication", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsDDDeleteSubscription ---- */
#define AdsDDDeleteSubscription oadsimpl_AdsDDDeleteSubscription
extern UNSIGNED32 AdsDDDeleteSubscription(UNSIGNED8* a0, UNSIGNED8* a1);
#undef AdsDDDeleteSubscription
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDDeleteSubscription=_AdsDDDeleteSubscription")
#pragma comment(linker, "/export:AdsDDDeleteSubscription=_AdsDDDeleteSubscription")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDDeleteSubscription(UNSIGNED8* a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsDDDeleteSubscription(a0, a1);
    arc_log_rc("AdsDDDeleteSubscription", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsDDDeleteUserGroup ---- */
#define AdsDDDeleteUserGroup oadsimpl_AdsDDDeleteUserGroup
extern UNSIGNED32 AdsDDDeleteUserGroup(UNSIGNED8* a0, UNSIGNED8* a1);
#undef AdsDDDeleteUserGroup
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDDeleteUserGroup=_AdsDDDeleteUserGroup")
#pragma comment(linker, "/export:AdsDDDeleteUserGroup=_AdsDDDeleteUserGroup")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDDeleteUserGroup(UNSIGNED8* a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsDDDeleteUserGroup(a0, a1);
    arc_log_rc("AdsDDDeleteUserGroup", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsDDGetArticleProperty ---- */
#define AdsDDGetArticleProperty oadsimpl_AdsDDGetArticleProperty
extern UNSIGNED32 AdsDDGetArticleProperty(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5);
#undef AdsDDGetArticleProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetArticleProperty=_AdsDDGetArticleProperty")
#pragma comment(linker, "/export:AdsDDGetArticleProperty=_AdsDDGetArticleProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetArticleProperty(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5) {
    UNSIGNED32 rc = oadsimpl_AdsDDGetArticleProperty(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsDDGetArticleProperty", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsDDGetIndexFileProperty ---- */
#define AdsDDGetIndexFileProperty oadsimpl_AdsDDGetIndexFileProperty
extern UNSIGNED32 AdsDDGetIndexFileProperty(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5);
#undef AdsDDGetIndexFileProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetIndexFileProperty=_AdsDDGetIndexFileProperty")
#pragma comment(linker, "/export:AdsDDGetIndexFileProperty=_AdsDDGetIndexFileProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetIndexFileProperty(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5) {
    UNSIGNED32 rc = oadsimpl_AdsDDGetIndexFileProperty(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsDDGetIndexFileProperty", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsDDGetPublicationProperty ---- */
#define AdsDDGetPublicationProperty oadsimpl_AdsDDGetPublicationProperty
extern UNSIGNED32 AdsDDGetPublicationProperty(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4);
#undef AdsDDGetPublicationProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetPublicationProperty=_AdsDDGetPublicationProperty")
#pragma comment(linker, "/export:AdsDDGetPublicationProperty=_AdsDDGetPublicationProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetPublicationProperty(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDGetPublicationProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDGetPublicationProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDMoveObjectFile ---- */
#define AdsDDMoveObjectFile oadsimpl_AdsDDMoveObjectFile
extern UNSIGNED32 AdsDDMoveObjectFile(void);
#undef AdsDDMoveObjectFile
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDMoveObjectFile=_AdsDDMoveObjectFile")
#pragma comment(linker, "/export:AdsDDMoveObjectFile=_AdsDDMoveObjectFile")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDMoveObjectFile(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5, UNSIGNED8* a6) {
    UNSIGNED32 rc = oadsimpl_AdsDDMoveObjectFile();
    arc_log_rc("AdsDDMoveObjectFile", rc, 7, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6);
    return rc;
}

/* ---- AdsDDRemoveView ---- */
#define AdsDDRemoveView oadsimpl_AdsDDRemoveView
extern UNSIGNED32 AdsDDRemoveView(UNSIGNED8* a0, UNSIGNED8* a1);
#undef AdsDDRemoveView
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDRemoveView=_AdsDDRemoveView")
#pragma comment(linker, "/export:AdsDDRemoveView=_AdsDDRemoveView")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDRemoveView(UNSIGNED8* a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsDDRemoveView(a0, a1);
    arc_log_rc("AdsDDRemoveView", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsDDRenameObject ---- */
#define AdsDDRenameObject oadsimpl_AdsDDRenameObject
extern UNSIGNED32 AdsDDRenameObject(void);
#undef AdsDDRenameObject
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDRenameObject=_AdsDDRenameObject")
#pragma comment(linker, "/export:AdsDDRenameObject=_AdsDDRenameObject")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDRenameObject(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDRenameObject();
    arc_log_rc("AdsDDRenameObject", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDSetArticleProperty ---- */
#define AdsDDSetArticleProperty oadsimpl_AdsDDSetArticleProperty
extern UNSIGNED32 AdsDDSetArticleProperty(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5);
#undef AdsDDSetArticleProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetArticleProperty=_AdsDDSetArticleProperty")
#pragma comment(linker, "/export:AdsDDSetArticleProperty=_AdsDDSetArticleProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetArticleProperty(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5) {
    UNSIGNED32 rc = oadsimpl_AdsDDSetArticleProperty(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsDDSetArticleProperty", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsDDSetObjectAccessRights ---- */
#define AdsDDSetObjectAccessRights oadsimpl_AdsDDSetObjectAccessRights
extern UNSIGNED32 AdsDDSetObjectAccessRights(void);
#undef AdsDDSetObjectAccessRights
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetObjectAccessRights=_AdsDDSetObjectAccessRights")
#pragma comment(linker, "/export:AdsDDSetObjectAccessRights=_AdsDDSetObjectAccessRights")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetObjectAccessRights(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsDDSetObjectAccessRights();
    arc_log_rc("AdsDDSetObjectAccessRights", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsDDSetPublicationProperty ---- */
#define AdsDDSetPublicationProperty oadsimpl_AdsDDSetPublicationProperty
extern UNSIGNED32 AdsDDSetPublicationProperty(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4);
#undef AdsDDSetPublicationProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetPublicationProperty=_AdsDDSetPublicationProperty")
#pragma comment(linker, "/export:AdsDDSetPublicationProperty=_AdsDDSetPublicationProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetPublicationProperty(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDSetPublicationProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDSetPublicationProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDSetSubscriptionProperty ---- */
#define AdsDDSetSubscriptionProperty oadsimpl_AdsDDSetSubscriptionProperty
extern UNSIGNED32 AdsDDSetSubscriptionProperty(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3, UNSIGNED8* h4);
#undef AdsDDSetSubscriptionProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetSubscriptionProperty=_AdsDDSetSubscriptionProperty")
#pragma comment(linker, "/export:AdsDDSetSubscriptionProperty=_AdsDDSetSubscriptionProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetSubscriptionProperty(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDSetSubscriptionProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDSetSubscriptionProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDSetUserGroupProperty ---- */
#define AdsDDSetUserGroupProperty oadsimpl_AdsDDSetUserGroupProperty
extern UNSIGNED32 AdsDDSetUserGroupProperty(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3, UNSIGNED8* h4);
#undef AdsDDSetUserGroupProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetUserGroupProperty=_AdsDDSetUserGroupProperty")
#pragma comment(linker, "/export:AdsDDSetUserGroupProperty=_AdsDDSetUserGroupProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetUserGroupProperty(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDSetUserGroupProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDSetUserGroupProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDeleteTable ---- */
#define AdsDeleteTable oadsimpl_AdsDeleteTable
extern UNSIGNED32 AdsDeleteTable(void);
#undef AdsDeleteTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDeleteTable=_AdsDeleteTable")
#pragma comment(linker, "/export:AdsDeleteTable=_AdsDeleteTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDeleteTable(UNSIGNED8* a0) {
    UNSIGNED32 rc = oadsimpl_AdsDeleteTable();
    arc_log_rc("AdsDeleteTable", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsExpressionLongToShort90 ---- */
#define AdsExpressionLongToShort90 oadsimpl_AdsExpressionLongToShort90
extern UNSIGNED32 AdsExpressionLongToShort90(void);
#undef AdsExpressionLongToShort90
#pragma comment(linker, "/alternatename:_oadsimpl_AdsExpressionLongToShort90=_AdsExpressionLongToShort90")
#pragma comment(linker, "/export:AdsExpressionLongToShort90=_AdsExpressionLongToShort90")
__declspec(dllexport) UNSIGNED32 __stdcall AdsExpressionLongToShort90(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsExpressionLongToShort90();
    arc_log_rc("AdsExpressionLongToShort90", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsExpressionShortToLong90 ---- */
#define AdsExpressionShortToLong90 oadsimpl_AdsExpressionShortToLong90
extern UNSIGNED32 AdsExpressionShortToLong90(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3);
#undef AdsExpressionShortToLong90
#pragma comment(linker, "/alternatename:_oadsimpl_AdsExpressionShortToLong90=_AdsExpressionShortToLong90")
#pragma comment(linker, "/export:AdsExpressionShortToLong90=_AdsExpressionShortToLong90")
__declspec(dllexport) UNSIGNED32 __stdcall AdsExpressionShortToLong90(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsExpressionShortToLong90(a0, a1, a2, a3);
    arc_log_rc("AdsExpressionShortToLong90", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsGetActiveLinkInfo ---- */
#define AdsGetActiveLinkInfo oadsimpl_AdsGetActiveLinkInfo
extern UNSIGNED32 AdsGetActiveLinkInfo(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3);
#undef AdsGetActiveLinkInfo
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetActiveLinkInfo=_AdsGetActiveLinkInfo")
#pragma comment(linker, "/export:AdsGetActiveLinkInfo=_AdsGetActiveLinkInfo")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetActiveLinkInfo(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsGetActiveLinkInfo(a0, a1, a2, a3);
    arc_log_rc("AdsGetActiveLinkInfo", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsGetBookmarkLength ---- */
#define AdsGetBookmarkLength oadsimpl_AdsGetBookmarkLength
extern UNSIGNED32 AdsGetBookmarkLength(UNSIGNED8* a0, UNSIGNED8* a1);
#undef AdsGetBookmarkLength
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetBookmarkLength=_AdsGetBookmarkLength")
#pragma comment(linker, "/export:AdsGetBookmarkLength=_AdsGetBookmarkLength")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetBookmarkLength(UNSIGNED8* a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetBookmarkLength(a0, a1);
    arc_log_rc("AdsGetBookmarkLength", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetCollation ---- */
#define AdsGetCollation oadsimpl_AdsGetCollation
extern UNSIGNED32 AdsGetCollation(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2);
#undef AdsGetCollation
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetCollation=_AdsGetCollation")
#pragma comment(linker, "/export:AdsGetCollation=_AdsGetCollation")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetCollation(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetCollation(a0, a1, a2);
    arc_log_rc("AdsGetCollation", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetColumnPermissions ---- */
#define AdsGetColumnPermissions oadsimpl_AdsGetColumnPermissions
extern UNSIGNED32 AdsGetColumnPermissions(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2);
#undef AdsGetColumnPermissions
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetColumnPermissions=_AdsGetColumnPermissions")
#pragma comment(linker, "/export:AdsGetColumnPermissions=_AdsGetColumnPermissions")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetColumnPermissions(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetColumnPermissions(a0, a1, a2);
    arc_log_rc("AdsGetColumnPermissions", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetConnectionPath ---- */
#define AdsGetConnectionPath oadsimpl_AdsGetConnectionPath
extern UNSIGNED32 AdsGetConnectionPath(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2);
#undef AdsGetConnectionPath
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetConnectionPath=_AdsGetConnectionPath")
#pragma comment(linker, "/export:AdsGetConnectionPath=_AdsGetConnectionPath")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetConnectionPath(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetConnectionPath(a0, a1, a2);
    arc_log_rc("AdsGetConnectionPath", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetConnectionProperty ---- */
#define AdsGetConnectionProperty oadsimpl_AdsGetConnectionProperty
extern UNSIGNED32 AdsGetConnectionProperty(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3);
#undef AdsGetConnectionProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetConnectionProperty=_AdsGetConnectionProperty")
#pragma comment(linker, "/export:AdsGetConnectionProperty=_AdsGetConnectionProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetConnectionProperty(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsGetConnectionProperty(a0, a1, a2, a3);
    arc_log_rc("AdsGetConnectionProperty", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsGetDataLength ---- */
#define AdsGetDataLength oadsimpl_AdsGetDataLength
extern UNSIGNED32 AdsGetDataLength(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3);
#undef AdsGetDataLength
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetDataLength=_AdsGetDataLength")
#pragma comment(linker, "/export:AdsGetDataLength=_AdsGetDataLength")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetDataLength(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsGetDataLength(a0, a1, a2, a3);
    arc_log_rc("AdsGetDataLength", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsGetFTSIndexInfoW ---- */
#define AdsGetFTSIndexInfoW oadsimpl_AdsGetFTSIndexInfoW
extern UNSIGNED32 AdsGetFTSIndexInfoW(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5, UNSIGNED8* a6, UNSIGNED8* a7, UNSIGNED8* a8, UNSIGNED8* a9, UNSIGNED8* a10, UNSIGNED8* a11, UNSIGNED8* a12);
#undef AdsGetFTSIndexInfoW
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFTSIndexInfoW=_AdsGetFTSIndexInfoW")
#pragma comment(linker, "/export:AdsGetFTSIndexInfoW=_AdsGetFTSIndexInfoW")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFTSIndexInfoW(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5, UNSIGNED8* a6, UNSIGNED8* a7, UNSIGNED8* a8, UNSIGNED8* a9, UNSIGNED8* a10, UNSIGNED8* a11, UNSIGNED8* a12) {
    UNSIGNED32 rc = oadsimpl_AdsGetFTSIndexInfoW(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);
    arc_log_rc("AdsGetFTSIndexInfoW", rc, 13, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5, (uintptr_t)a6, (uintptr_t)a7, (uintptr_t)a8, (uintptr_t)a9, (uintptr_t)a10, (uintptr_t)a11, (uintptr_t)a12);
    return rc;
}

/* ---- AdsGetIndexCollation ---- */
#define AdsGetIndexCollation oadsimpl_AdsGetIndexCollation
extern UNSIGNED32 AdsGetIndexCollation(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2);
#undef AdsGetIndexCollation
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetIndexCollation=_AdsGetIndexCollation")
#pragma comment(linker, "/export:AdsGetIndexCollation=_AdsGetIndexCollation")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetIndexCollation(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetIndexCollation(a0, a1, a2);
    arc_log_rc("AdsGetIndexCollation", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetIndexFlags ---- */
#define AdsGetIndexFlags oadsimpl_AdsGetIndexFlags
extern UNSIGNED32 AdsGetIndexFlags(UNSIGNED8* h0, UNSIGNED8* h1);
#undef AdsGetIndexFlags
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetIndexFlags=_AdsGetIndexFlags")
#pragma comment(linker, "/export:AdsGetIndexFlags=_AdsGetIndexFlags")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetIndexFlags(UNSIGNED8* a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetIndexFlags(a0, a1);
    arc_log_rc("AdsGetIndexFlags", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetIndexPageSize ---- */
#define AdsGetIndexPageSize oadsimpl_AdsGetIndexPageSize
extern UNSIGNED32 AdsGetIndexPageSize(void);
#undef AdsGetIndexPageSize
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetIndexPageSize=_AdsGetIndexPageSize")
#pragma comment(linker, "/export:AdsGetIndexPageSize=_AdsGetIndexPageSize")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetIndexPageSize(UNSIGNED8* a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetIndexPageSize();
    arc_log_rc("AdsGetIndexPageSize", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetKeyColumn ---- */
#define AdsGetKeyColumn oadsimpl_AdsGetKeyColumn
extern UNSIGNED32 AdsGetKeyColumn(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2);
#undef AdsGetKeyColumn
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetKeyColumn=_AdsGetKeyColumn")
#pragma comment(linker, "/export:AdsGetKeyColumn=_AdsGetKeyColumn")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetKeyColumn(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetKeyColumn(a0, a1, a2);
    arc_log_rc("AdsGetKeyColumn", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetNullRecord ---- */
#define AdsGetNullRecord oadsimpl_AdsGetNullRecord
extern UNSIGNED32 AdsGetNullRecord(void);
#undef AdsGetNullRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetNullRecord=_AdsGetNullRecord")
#pragma comment(linker, "/export:AdsGetNullRecord=_AdsGetNullRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetNullRecord(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetNullRecord();
    arc_log_rc("AdsGetNullRecord", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetNumFTSIndexes ---- */
#define AdsGetNumFTSIndexes oadsimpl_AdsGetNumFTSIndexes
extern UNSIGNED32 AdsGetNumFTSIndexes(UNSIGNED8* a0, UNSIGNED8* a1);
#undef AdsGetNumFTSIndexes
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetNumFTSIndexes=_AdsGetNumFTSIndexes")
#pragma comment(linker, "/export:AdsGetNumFTSIndexes=_AdsGetNumFTSIndexes")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetNumFTSIndexes(UNSIGNED8* a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetNumFTSIndexes(a0, a1);
    arc_log_rc("AdsGetNumFTSIndexes", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetNumSegments ---- */
#define AdsGetNumSegments oadsimpl_AdsGetNumSegments
extern UNSIGNED32 AdsGetNumSegments(void);
#undef AdsGetNumSegments
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetNumSegments=_AdsGetNumSegments")
#pragma comment(linker, "/export:AdsGetNumSegments=_AdsGetNumSegments")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetNumSegments(UNSIGNED8* a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetNumSegments();
    arc_log_rc("AdsGetNumSegments", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGetPreparedFields ---- */
#define AdsGetPreparedFields oadsimpl_AdsGetPreparedFields
extern UNSIGNED32 AdsGetPreparedFields(void);
#undef AdsGetPreparedFields
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetPreparedFields=_AdsGetPreparedFields")
#pragma comment(linker, "/export:AdsGetPreparedFields=_AdsGetPreparedFields")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetPreparedFields(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsGetPreparedFields();
    arc_log_rc("AdsGetPreparedFields", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsGetSQLStmtParams ---- */
#define AdsGetSQLStmtParams oadsimpl_AdsGetSQLStmtParams
extern UNSIGNED32 AdsGetSQLStmtParams(void);
#undef AdsGetSQLStmtParams
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetSQLStmtParams=_AdsGetSQLStmtParams")
#pragma comment(linker, "/export:AdsGetSQLStmtParams=_AdsGetSQLStmtParams")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetSQLStmtParams(UNSIGNED8* a0) {
    UNSIGNED32 rc = oadsimpl_AdsGetSQLStmtParams();
    arc_log_rc("AdsGetSQLStmtParams", rc, 1, (uintptr_t)a0);
    return rc;
}

/* ---- AdsGetSegmentFieldname ---- */
#define AdsGetSegmentFieldname oadsimpl_AdsGetSegmentFieldname
extern UNSIGNED32 AdsGetSegmentFieldname(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3);
#undef AdsGetSegmentFieldname
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetSegmentFieldname=_AdsGetSegmentFieldname")
#pragma comment(linker, "/export:AdsGetSegmentFieldname=_AdsGetSegmentFieldname")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetSegmentFieldname(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsGetSegmentFieldname(a0, a1, a2, a3);
    arc_log_rc("AdsGetSegmentFieldname", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsGetTableCollation ---- */
#define AdsGetTableCollation oadsimpl_AdsGetTableCollation
extern UNSIGNED32 AdsGetTableCollation(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2);
#undef AdsGetTableCollation
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableCollation=_AdsGetTableCollation")
#pragma comment(linker, "/export:AdsGetTableCollation=_AdsGetTableCollation")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableCollation(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsGetTableCollation(a0, a1, a2);
    arc_log_rc("AdsGetTableCollation", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsGetTableCreationString ---- */
#define AdsGetTableCreationString oadsimpl_AdsGetTableCreationString
extern UNSIGNED32 AdsGetTableCreationString(void);
#undef AdsGetTableCreationString
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableCreationString=_AdsGetTableCreationString")
#pragma comment(linker, "/export:AdsGetTableCreationString=_AdsGetTableCreationString")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableCreationString(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsGetTableCreationString();
    arc_log_rc("AdsGetTableCreationString", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsGetTransactionCount ---- */
#define AdsGetTransactionCount oadsimpl_AdsGetTransactionCount
extern UNSIGNED32 AdsGetTransactionCount(UNSIGNED8* a0, UNSIGNED8* a1);
#undef AdsGetTransactionCount
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTransactionCount=_AdsGetTransactionCount")
#pragma comment(linker, "/export:AdsGetTransactionCount=_AdsGetTransactionCount")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTransactionCount(UNSIGNED8* a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGetTransactionCount(a0, a1);
    arc_log_rc("AdsGetTransactionCount", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsGotoBookmark ---- */
#define AdsGotoBookmark oadsimpl_AdsGotoBookmark
extern UNSIGNED32 AdsGotoBookmark(UNSIGNED8* h0, UNSIGNED8* h1);
#undef AdsGotoBookmark
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGotoBookmark=_AdsGotoBookmark")
#pragma comment(linker, "/export:AdsGotoBookmark=_AdsGotoBookmark")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGotoBookmark(UNSIGNED8* a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsGotoBookmark(a0, a1);
    arc_log_rc("AdsGotoBookmark", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsIsIndexCandidate ---- */
#define AdsIsIndexCandidate oadsimpl_AdsIsIndexCandidate
extern UNSIGNED32 AdsIsIndexCandidate(UNSIGNED8* h0, UNSIGNED8* h1);
#undef AdsIsIndexCandidate
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsIndexCandidate=_AdsIsIndexCandidate")
#pragma comment(linker, "/export:AdsIsIndexCandidate=_AdsIsIndexCandidate")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsIndexCandidate(UNSIGNED8* a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsIsIndexCandidate(a0, a1);
    arc_log_rc("AdsIsIndexCandidate", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsIsIndexCompound ---- */
#define AdsIsIndexCompound oadsimpl_AdsIsIndexCompound
extern UNSIGNED32 AdsIsIndexCompound(UNSIGNED8* h0, UNSIGNED8* h1);
#undef AdsIsIndexCompound
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsIndexCompound=_AdsIsIndexCompound")
#pragma comment(linker, "/export:AdsIsIndexCompound=_AdsIsIndexCompound")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsIndexCompound(UNSIGNED8* a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsIsIndexCompound(a0, a1);
    arc_log_rc("AdsIsIndexCompound", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsIsIndexPrimaryKey ---- */
#define AdsIsIndexPrimaryKey oadsimpl_AdsIsIndexPrimaryKey
extern UNSIGNED32 AdsIsIndexPrimaryKey(UNSIGNED8* h0, UNSIGNED8* h1);
#undef AdsIsIndexPrimaryKey
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsIndexPrimaryKey=_AdsIsIndexPrimaryKey")
#pragma comment(linker, "/export:AdsIsIndexPrimaryKey=_AdsIsIndexPrimaryKey")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsIndexPrimaryKey(UNSIGNED8* a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsIsIndexPrimaryKey(a0, a1);
    arc_log_rc("AdsIsIndexPrimaryKey", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsPrepareSQLNow ---- */
#define AdsPrepareSQLNow oadsimpl_AdsPrepareSQLNow
extern UNSIGNED32 AdsPrepareSQLNow(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3);
#undef AdsPrepareSQLNow
#pragma comment(linker, "/alternatename:_oadsimpl_AdsPrepareSQLNow=_AdsPrepareSQLNow")
#pragma comment(linker, "/export:AdsPrepareSQLNow=_AdsPrepareSQLNow")
__declspec(dllexport) UNSIGNED32 __stdcall AdsPrepareSQLNow(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsPrepareSQLNow(a0, a1, a2, a3);
    arc_log_rc("AdsPrepareSQLNow", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsSetLastError ---- */
#define AdsSetLastError oadsimpl_AdsSetLastError
extern UNSIGNED32 AdsSetLastError(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3);
#undef AdsSetLastError
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetLastError=_AdsSetLastError")
#pragma comment(linker, "/export:AdsSetLastError=_AdsSetLastError")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetLastError(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsSetLastError(a0, a1, a2, a3);
    arc_log_rc("AdsSetLastError", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsSqlPeekStatement ---- */
#define AdsSqlPeekStatement oadsimpl_AdsSqlPeekStatement
extern UNSIGNED32 AdsSqlPeekStatement(UNSIGNED8* a0, UNSIGNED8* a1);
#undef AdsSqlPeekStatement
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSqlPeekStatement=_AdsSqlPeekStatement")
#pragma comment(linker, "/export:AdsSqlPeekStatement=_AdsSqlPeekStatement")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSqlPeekStatement(UNSIGNED8* a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsSqlPeekStatement(a0, a1);
    arc_log_rc("AdsSqlPeekStatement", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsStepIndexKey ---- */
#define AdsStepIndexKey oadsimpl_AdsStepIndexKey
extern UNSIGNED32 AdsStepIndexKey(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3);
#undef AdsStepIndexKey
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStepIndexKey=_AdsStepIndexKey")
#pragma comment(linker, "/export:AdsStepIndexKey=_AdsStepIndexKey")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStepIndexKey(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    UNSIGNED32 rc = oadsimpl_AdsStepIndexKey(a0, a1, a2, a3);
    arc_log_rc("AdsStepIndexKey", rc, 4, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3);
    return rc;
}

/* ---- AdsStmtConstrainUpdates ---- */
#define AdsStmtConstrainUpdates oadsimpl_AdsStmtConstrainUpdates
extern UNSIGNED32 AdsStmtConstrainUpdates(UNSIGNED8* h0, UNSIGNED8* h1);
#undef AdsStmtConstrainUpdates
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStmtConstrainUpdates=_AdsStmtConstrainUpdates")
#pragma comment(linker, "/export:AdsStmtConstrainUpdates=_AdsStmtConstrainUpdates")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStmtConstrainUpdates(UNSIGNED8* a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsStmtConstrainUpdates(a0, a1);
    arc_log_rc("AdsStmtConstrainUpdates", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsStmtReadAllColumns ---- */
#define AdsStmtReadAllColumns oadsimpl_AdsStmtReadAllColumns
extern UNSIGNED32 AdsStmtReadAllColumns(UNSIGNED8* h0, UNSIGNED8* h1);
#undef AdsStmtReadAllColumns
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStmtReadAllColumns=_AdsStmtReadAllColumns")
#pragma comment(linker, "/export:AdsStmtReadAllColumns=_AdsStmtReadAllColumns")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStmtReadAllColumns(UNSIGNED8* a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsStmtReadAllColumns(a0, a1);
    arc_log_rc("AdsStmtReadAllColumns", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsBuildRawKey ---- */
#define AdsBuildRawKey oadsimpl_AdsBuildRawKey
extern UNSIGNED32 AdsBuildRawKey(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2);
#undef AdsBuildRawKey
#pragma comment(linker, "/alternatename:_oadsimpl_AdsBuildRawKey=_AdsBuildRawKey")
#pragma comment(linker, "/export:AdsBuildRawKey=_AdsBuildRawKey")
__declspec(dllexport) UNSIGNED32 __stdcall AdsBuildRawKey(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsBuildRawKey(a0, a1, a2);
    arc_log_rc("AdsBuildRawKey", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsClearRecordBuffer ---- */
#define AdsClearRecordBuffer oadsimpl_AdsClearRecordBuffer
extern UNSIGNED32 AdsClearRecordBuffer(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2);
#undef AdsClearRecordBuffer
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearRecordBuffer=_AdsClearRecordBuffer")
#pragma comment(linker, "/export:AdsClearRecordBuffer=_AdsClearRecordBuffer")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearRecordBuffer(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsClearRecordBuffer(a0, a1, a2);
    arc_log_rc("AdsClearRecordBuffer", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsCompareBookmarks ---- */
#define AdsCompareBookmarks oadsimpl_AdsCompareBookmarks
extern UNSIGNED32 AdsCompareBookmarks(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2);
#undef AdsCompareBookmarks
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCompareBookmarks=_AdsCompareBookmarks")
#pragma comment(linker, "/export:AdsCompareBookmarks=_AdsCompareBookmarks")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCompareBookmarks(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsCompareBookmarks(a0, a1, a2);
    arc_log_rc("AdsCompareBookmarks", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsDDCreateArticle100 ---- */
#define AdsDDCreateArticle100 oadsimpl_AdsDDCreateArticle100
extern UNSIGNED32 AdsDDCreateArticle100(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3, UNSIGNED8* h4, UNSIGNED8* h5);
#undef AdsDDCreateArticle100
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreateArticle100=_AdsDDCreateArticle100")
#pragma comment(linker, "/export:AdsDDCreateArticle100=_AdsDDCreateArticle100")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreateArticle100(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5) {
    UNSIGNED32 rc = oadsimpl_AdsDDCreateArticle100(a0, a1, a2, a3, a4, a5);
    arc_log_rc("AdsDDCreateArticle100", rc, 6, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4, (uintptr_t)a5);
    return rc;
}

/* ---- AdsDDCreateUserGroup ---- */
#define AdsDDCreateUserGroup oadsimpl_AdsDDCreateUserGroup
extern UNSIGNED32 AdsDDCreateUserGroup(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2);
#undef AdsDDCreateUserGroup
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreateUserGroup=_AdsDDCreateUserGroup")
#pragma comment(linker, "/export:AdsDDCreateUserGroup=_AdsDDCreateUserGroup")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreateUserGroup(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    UNSIGNED32 rc = oadsimpl_AdsDDCreateUserGroup(a0, a1, a2);
    arc_log_rc("AdsDDCreateUserGroup", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsDDGetLinkProperty ---- */
#define AdsDDGetLinkProperty oadsimpl_AdsDDGetLinkProperty
extern UNSIGNED32 AdsDDGetLinkProperty(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3, UNSIGNED8* h4);
#undef AdsDDGetLinkProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetLinkProperty=_AdsDDGetLinkProperty")
#pragma comment(linker, "/export:AdsDDGetLinkProperty=_AdsDDGetLinkProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetLinkProperty(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDGetLinkProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDGetLinkProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDGetSubscriptionProperty ---- */
#define AdsDDGetSubscriptionProperty oadsimpl_AdsDDGetSubscriptionProperty
extern UNSIGNED32 AdsDDGetSubscriptionProperty(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3, UNSIGNED8* h4);
#undef AdsDDGetSubscriptionProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetSubscriptionProperty=_AdsDDGetSubscriptionProperty")
#pragma comment(linker, "/export:AdsDDGetSubscriptionProperty=_AdsDDGetSubscriptionProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetSubscriptionProperty(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDGetSubscriptionProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDGetSubscriptionProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsDDGetUserGroupProperty ---- */
#define AdsDDGetUserGroupProperty oadsimpl_AdsDDGetUserGroupProperty
extern UNSIGNED32 AdsDDGetUserGroupProperty(UNSIGNED8* h0, UNSIGNED8* h1, UNSIGNED8* h2, UNSIGNED8* h3, UNSIGNED8* h4);
#undef AdsDDGetUserGroupProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetUserGroupProperty=_AdsDDGetUserGroupProperty")
#pragma comment(linker, "/export:AdsDDGetUserGroupProperty=_AdsDDGetUserGroupProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetUserGroupProperty(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4) {
    UNSIGNED32 rc = oadsimpl_AdsDDGetUserGroupProperty(a0, a1, a2, a3, a4);
    arc_log_rc("AdsDDGetUserGroupProperty", rc, 5, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)a4);
    return rc;
}

/* ---- AdsMutexCreate ---- */
#define AdsMutexCreate oadsimpl_AdsMutexCreate
extern UNSIGNED32 AdsMutexCreate(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsMutexCreate
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMutexCreate=_AdsMutexCreate")
#pragma comment(linker, "/export:AdsMutexCreate=_AdsMutexCreate")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMutexCreate(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsMutexCreate(a0, a1);
    arc_log_rc("AdsMutexCreate", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsMutexLock ---- */
#define AdsMutexLock oadsimpl_AdsMutexLock
extern UNSIGNED32 AdsMutexLock(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED32 ulTimeOut);
#undef AdsMutexLock
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMutexLock=_AdsMutexLock")
#pragma comment(linker, "/export:AdsMutexLock=_AdsMutexLock")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMutexLock(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32 a2) {
    UNSIGNED32 rc = oadsimpl_AdsMutexLock(a0, a1, a2);
    arc_log_rc("AdsMutexLock", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsMutexTryLock ---- */
#define AdsMutexTryLock oadsimpl_AdsMutexTryLock
extern UNSIGNED32 AdsMutexTryLock(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16* pbLocked);
#undef AdsMutexTryLock
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMutexTryLock=_AdsMutexTryLock")
#pragma comment(linker, "/export:AdsMutexTryLock=_AdsMutexTryLock")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMutexTryLock(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    UNSIGNED32 rc = oadsimpl_AdsMutexTryLock(a0, a1, a2);
    arc_log_rc("AdsMutexTryLock", rc, 3, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)a2);
    return rc;
}

/* ---- AdsMutexUnlock ---- */
#define AdsMutexUnlock oadsimpl_AdsMutexUnlock
extern UNSIGNED32 AdsMutexUnlock(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsMutexUnlock
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMutexUnlock=_AdsMutexUnlock")
#pragma comment(linker, "/export:AdsMutexUnlock=_AdsMutexUnlock")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMutexUnlock(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsMutexUnlock(a0, a1);
    arc_log_rc("AdsMutexUnlock", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}

/* ---- AdsMutexDestroy ---- */
#define AdsMutexDestroy oadsimpl_AdsMutexDestroy
extern UNSIGNED32 AdsMutexDestroy(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsMutexDestroy
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMutexDestroy=_AdsMutexDestroy")
#pragma comment(linker, "/export:AdsMutexDestroy=_AdsMutexDestroy")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMutexDestroy(ADSHANDLE a0, UNSIGNED8* a1) {
    UNSIGNED32 rc = oadsimpl_AdsMutexDestroy(a0, a1);
    arc_log_rc("AdsMutexDestroy", rc, 2, (uintptr_t)a0, (uintptr_t)a1);
    return rc;
}
