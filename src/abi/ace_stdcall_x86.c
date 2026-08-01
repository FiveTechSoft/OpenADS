/* GENERATED — do not edit by hand.
 * x86-only __stdcall aliases of the ACE entry points, so ace32.dll
 * exports the AdsXxx@N decorated names 32-bit Harbour rddads imports.
 * The plain __cdecl exports in ace_exports.cpp are untouched. */
#include <stddef.h>
#include <stdint.h>

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
__declspec(dllexport) UNSIGNED32 __stdcall AdsAddCustomKey(ADSHANDLE a0) {
    return oadsimpl_AdsAddCustomKey(a0);
}

/* ---- AdsAppendRecord ---- */
#define AdsAppendRecord oadsimpl_AdsAppendRecord
extern UNSIGNED32 ENTRYPOINT AdsAppendRecord(ADSHANDLE hTable);
#undef AdsAppendRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsAppendRecord=_AdsAppendRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsAppendRecord(ADSHANDLE a0) {
    return oadsimpl_AdsAppendRecord(a0);
}

/* ---- AdsApplicationExit ---- */
#define AdsApplicationExit oadsimpl_AdsApplicationExit
extern UNSIGNED32 ENTRYPOINT AdsApplicationExit(void);
#undef AdsApplicationExit
#pragma comment(linker, "/alternatename:_oadsimpl_AdsApplicationExit=_AdsApplicationExit")
__declspec(dllexport) UNSIGNED32 __stdcall AdsApplicationExit(void) {
    return oadsimpl_AdsApplicationExit();
}

/* ---- AdsAtBOF ---- */
#define AdsAtBOF oadsimpl_AdsAtBOF
extern UNSIGNED32 ENTRYPOINT AdsAtBOF(ADSHANDLE hTable, UNSIGNED16* pbAtBegin);
#undef AdsAtBOF
#pragma comment(linker, "/alternatename:_oadsimpl_AdsAtBOF=_AdsAtBOF")
__declspec(dllexport) UNSIGNED32 __stdcall AdsAtBOF(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsAtBOF(a0, a1);
}

/* ---- AdsAtEOF ---- */
#define AdsAtEOF oadsimpl_AdsAtEOF
extern UNSIGNED32 ENTRYPOINT AdsAtEOF(ADSHANDLE hTable, UNSIGNED16* pbAtEnd);
#undef AdsAtEOF
#pragma comment(linker, "/alternatename:_oadsimpl_AdsAtEOF=_AdsAtEOF")
__declspec(dllexport) UNSIGNED32 __stdcall AdsAtEOF(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsAtEOF(a0, a1);
}

/* ---- AdsBeginTransaction ---- */
#define AdsBeginTransaction oadsimpl_AdsBeginTransaction
extern UNSIGNED32 ENTRYPOINT AdsBeginTransaction(ADSHANDLE hConnect);
#undef AdsBeginTransaction
#pragma comment(linker, "/alternatename:_oadsimpl_AdsBeginTransaction=_AdsBeginTransaction")
__declspec(dllexport) UNSIGNED32 __stdcall AdsBeginTransaction(ADSHANDLE a0) {
    return oadsimpl_AdsBeginTransaction(a0);
}

/* ---- AdsBinaryToFile ---- */
#define AdsBinaryToFile oadsimpl_AdsBinaryToFile
extern UNSIGNED32 ENTRYPOINT AdsBinaryToFile(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED8* pucPath);
#undef AdsBinaryToFile
#pragma comment(linker, "/alternatename:_oadsimpl_AdsBinaryToFile=_AdsBinaryToFile")
__declspec(dllexport) UNSIGNED32 __stdcall AdsBinaryToFile(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    return oadsimpl_AdsBinaryToFile(a0, a1, a2);
}

/* ---- AdsBinaryToFileW ---- */
#define AdsBinaryToFileW oadsimpl_AdsBinaryToFileW
extern UNSIGNED32 ENTRYPOINT AdsBinaryToFileW(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pwcPath);
#undef AdsBinaryToFileW
#pragma comment(linker, "/alternatename:_oadsimpl_AdsBinaryToFileW=_AdsBinaryToFileW")
__declspec(dllexport) UNSIGNED32 __stdcall AdsBinaryToFileW(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsBinaryToFileW(a0, a1, a2);
}

/* ---- AdsCacheOpenCursors ---- */
#define AdsCacheOpenCursors oadsimpl_AdsCacheOpenCursors
extern UNSIGNED32 ENTRYPOINT AdsCacheOpenCursors(UNSIGNED16 usCacheCount);
#undef AdsCacheOpenCursors
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCacheOpenCursors=_AdsCacheOpenCursors")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCacheOpenCursors(UNSIGNED16 a0) {
    return oadsimpl_AdsCacheOpenCursors(a0);
}

/* ---- AdsCacheOpenTables ---- */
#define AdsCacheOpenTables oadsimpl_AdsCacheOpenTables
extern UNSIGNED32 ENTRYPOINT AdsCacheOpenTables(UNSIGNED16 usCacheCount);
#undef AdsCacheOpenTables
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCacheOpenTables=_AdsCacheOpenTables")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCacheOpenTables(UNSIGNED16 a0) {
    return oadsimpl_AdsCacheOpenTables(a0);
}

/* ---- AdsCacheRecords ---- */
#define AdsCacheRecords oadsimpl_AdsCacheRecords
extern UNSIGNED32 ENTRYPOINT AdsCacheRecords(ADSHANDLE hTable, UNSIGNED16 usRecCount);
#undef AdsCacheRecords
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCacheRecords=_AdsCacheRecords")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCacheRecords(ADSHANDLE a0, UNSIGNED16 a1) {
    return oadsimpl_AdsCacheRecords(a0, a1);
}

/* ---- AdsCancelUpdate ---- */
#define AdsCancelUpdate oadsimpl_AdsCancelUpdate
extern UNSIGNED32 ENTRYPOINT AdsCancelUpdate(ADSHANDLE hTable);
#undef AdsCancelUpdate
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCancelUpdate=_AdsCancelUpdate")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCancelUpdate(ADSHANDLE a0) {
    return oadsimpl_AdsCancelUpdate(a0);
}

/* ---- AdsCancelUpdate90 ---- */
#define AdsCancelUpdate90 oadsimpl_AdsCancelUpdate90
extern UNSIGNED32 ENTRYPOINT AdsCancelUpdate90(ADSHANDLE hTable, UNSIGNED32 ulOptions);
#undef AdsCancelUpdate90
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCancelUpdate90=_AdsCancelUpdate90")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCancelUpdate90(ADSHANDLE a0, UNSIGNED32 a1) {
    return oadsimpl_AdsCancelUpdate90(a0, a1);
}

/* ---- AdsCheckExistence ---- */
#define AdsCheckExistence oadsimpl_AdsCheckExistence
extern UNSIGNED32 ENTRYPOINT AdsCheckExistence(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16* pbExists);
#undef AdsCheckExistence
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCheckExistence=_AdsCheckExistence")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCheckExistence(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsCheckExistence(a0, a1, a2);
}

/* ---- AdsClearAOF ---- */
#define AdsClearAOF oadsimpl_AdsClearAOF
extern UNSIGNED32 ENTRYPOINT AdsClearAOF(ADSHANDLE hTable);
#undef AdsClearAOF
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearAOF=_AdsClearAOF")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearAOF(ADSHANDLE a0) {
    return oadsimpl_AdsClearAOF(a0);
}

/* ---- AdsClearAllScopes ---- */
#define AdsClearAllScopes oadsimpl_AdsClearAllScopes
extern UNSIGNED32 ENTRYPOINT AdsClearAllScopes(ADSHANDLE hTable);
#undef AdsClearAllScopes
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearAllScopes=_AdsClearAllScopes")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearAllScopes(ADSHANDLE a0) {
    return oadsimpl_AdsClearAllScopes(a0);
}

/* ---- AdsClearCallbackFunction ---- */
#define AdsClearCallbackFunction oadsimpl_AdsClearCallbackFunction
extern UNSIGNED32 ENTRYPOINT AdsClearCallbackFunction(void);
#undef AdsClearCallbackFunction
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearCallbackFunction=_AdsClearCallbackFunction")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearCallbackFunction(void) {
    return oadsimpl_AdsClearCallbackFunction();
}

/* ---- AdsClearDefault ---- */
#define AdsClearDefault oadsimpl_AdsClearDefault
extern UNSIGNED32 ENTRYPOINT AdsClearDefault(void);
#undef AdsClearDefault
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearDefault=_AdsClearDefault")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearDefault(void) {
    return oadsimpl_AdsClearDefault();
}

/* ---- AdsClearFilter ---- */
#define AdsClearFilter oadsimpl_AdsClearFilter
extern UNSIGNED32 ENTRYPOINT AdsClearFilter(ADSHANDLE hTable);
#undef AdsClearFilter
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearFilter=_AdsClearFilter")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearFilter(ADSHANDLE a0) {
    return oadsimpl_AdsClearFilter(a0);
}

/* ---- AdsClearProgressCallback ---- */
#define AdsClearProgressCallback oadsimpl_AdsClearProgressCallback
extern UNSIGNED32 ENTRYPOINT AdsClearProgressCallback(void);
#undef AdsClearProgressCallback
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearProgressCallback=_AdsClearProgressCallback")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearProgressCallback(void) {
    return oadsimpl_AdsClearProgressCallback();
}

/* ---- AdsClearRelation ---- */
#define AdsClearRelation oadsimpl_AdsClearRelation
extern UNSIGNED32 ENTRYPOINT AdsClearRelation(ADSHANDLE hTable);
#undef AdsClearRelation
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearRelation=_AdsClearRelation")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearRelation(ADSHANDLE a0) {
    return oadsimpl_AdsClearRelation(a0);
}

/* ---- AdsClearSQLAbortFunc ---- */
#define AdsClearSQLAbortFunc oadsimpl_AdsClearSQLAbortFunc
extern UNSIGNED32 ENTRYPOINT AdsClearSQLAbortFunc(void);
#undef AdsClearSQLAbortFunc
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearSQLAbortFunc=_AdsClearSQLAbortFunc")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearSQLAbortFunc(void) {
    return oadsimpl_AdsClearSQLAbortFunc();
}

/* ---- AdsClearSQLParams ---- */
#define AdsClearSQLParams oadsimpl_AdsClearSQLParams
extern UNSIGNED32 ENTRYPOINT AdsClearSQLParams(ADSHANDLE hStatement);
#undef AdsClearSQLParams
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearSQLParams=_AdsClearSQLParams")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearSQLParams(ADSHANDLE a0) {
    return oadsimpl_AdsClearSQLParams(a0);
}

/* ---- AdsClearScope ---- */
#define AdsClearScope oadsimpl_AdsClearScope
extern UNSIGNED32 ENTRYPOINT AdsClearScope(ADSHANDLE hIndex, UNSIGNED16 usScope);
#undef AdsClearScope
#pragma comment(linker, "/alternatename:_oadsimpl_AdsClearScope=_AdsClearScope")
__declspec(dllexport) UNSIGNED32 __stdcall AdsClearScope(ADSHANDLE a0, UNSIGNED16 a1) {
    return oadsimpl_AdsClearScope(a0, a1);
}

/* ---- AdsCloneTable ---- */
#define AdsCloneTable oadsimpl_AdsCloneTable
extern UNSIGNED32 ENTRYPOINT AdsCloneTable(ADSHANDLE hTable, ADSHANDLE* phClone);
#undef AdsCloneTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCloneTable=_AdsCloneTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCloneTable(ADSHANDLE a0, ADSHANDLE* a1) {
    return oadsimpl_AdsCloneTable(a0, a1);
}

/* ---- AdsCloseAllIndexes ---- */
#define AdsCloseAllIndexes oadsimpl_AdsCloseAllIndexes
extern UNSIGNED32 ENTRYPOINT AdsCloseAllIndexes(ADSHANDLE hTable);
#undef AdsCloseAllIndexes
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCloseAllIndexes=_AdsCloseAllIndexes")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCloseAllIndexes(ADSHANDLE a0) {
    return oadsimpl_AdsCloseAllIndexes(a0);
}

/* ---- AdsCloseAllTables ---- */
#define AdsCloseAllTables oadsimpl_AdsCloseAllTables
extern UNSIGNED32 ENTRYPOINT AdsCloseAllTables(void);
#undef AdsCloseAllTables
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCloseAllTables=_AdsCloseAllTables")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCloseAllTables(void) {
    return oadsimpl_AdsCloseAllTables();
}

/* ---- AdsCloseCachedTables ---- */
#define AdsCloseCachedTables oadsimpl_AdsCloseCachedTables
extern UNSIGNED32 ENTRYPOINT AdsCloseCachedTables(ADSHANDLE hConnect);
#undef AdsCloseCachedTables
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCloseCachedTables=_AdsCloseCachedTables")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCloseCachedTables(ADSHANDLE a0) {
    return oadsimpl_AdsCloseCachedTables(a0);
}

/* ---- AdsCloseIndex ---- */
#define AdsCloseIndex oadsimpl_AdsCloseIndex
extern UNSIGNED32 ENTRYPOINT AdsCloseIndex(ADSHANDLE hIndex);
#undef AdsCloseIndex
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCloseIndex=_AdsCloseIndex")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCloseIndex(ADSHANDLE a0) {
    return oadsimpl_AdsCloseIndex(a0);
}

/* ---- AdsCloseSQLStatement ---- */
#define AdsCloseSQLStatement oadsimpl_AdsCloseSQLStatement
extern UNSIGNED32 ENTRYPOINT AdsCloseSQLStatement(ADSHANDLE hStatement);
#undef AdsCloseSQLStatement
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCloseSQLStatement=_AdsCloseSQLStatement")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCloseSQLStatement(ADSHANDLE a0) {
    return oadsimpl_AdsCloseSQLStatement(a0);
}

/* ---- AdsCloseTable ---- */
#define AdsCloseTable oadsimpl_AdsCloseTable
extern UNSIGNED32 ENTRYPOINT AdsCloseTable(ADSHANDLE hTable);
#undef AdsCloseTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCloseTable=_AdsCloseTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCloseTable(ADSHANDLE a0) {
    return oadsimpl_AdsCloseTable(a0);
}

/* ---- AdsCommitTransaction ---- */
#define AdsCommitTransaction oadsimpl_AdsCommitTransaction
extern UNSIGNED32 ENTRYPOINT AdsCommitTransaction(ADSHANDLE hConnect);
#undef AdsCommitTransaction
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCommitTransaction=_AdsCommitTransaction")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCommitTransaction(ADSHANDLE a0) {
    return oadsimpl_AdsCommitTransaction(a0);
}

/* ---- AdsConnect ---- */
#define AdsConnect oadsimpl_AdsConnect
extern UNSIGNED32 ENTRYPOINT AdsConnect(UNSIGNED8* pucServer, ADSHANDLE* phConnect);
#undef AdsConnect
#pragma comment(linker, "/alternatename:_oadsimpl_AdsConnect=_AdsConnect")
__declspec(dllexport) UNSIGNED32 __stdcall AdsConnect(UNSIGNED8* a0, ADSHANDLE* a1) {
    return oadsimpl_AdsConnect(a0, a1);
}

/* ---- AdsConnect26 ---- */
#define AdsConnect26 oadsimpl_AdsConnect26
extern UNSIGNED32 ENTRYPOINT AdsConnect26(UNSIGNED8* pucServer, UNSIGNED16 usServerType, ADSHANDLE* phConnect);
#undef AdsConnect26
#pragma comment(linker, "/alternatename:_oadsimpl_AdsConnect26=_AdsConnect26")
__declspec(dllexport) UNSIGNED32 __stdcall AdsConnect26(UNSIGNED8* a0, UNSIGNED16 a1, ADSHANDLE* a2) {
    return oadsimpl_AdsConnect26(a0, a1, a2);
}

/* ---- AdsConnect60 ---- */
#define AdsConnect60 oadsimpl_AdsConnect60
extern UNSIGNED32 ENTRYPOINT AdsConnect60(UNSIGNED8* pucServer, UNSIGNED16 usServerType, UNSIGNED8* pucUserName, UNSIGNED8* pucPassword, UNSIGNED32 ulOptions, ADSHANDLE* phConnect);
#undef AdsConnect60
#pragma comment(linker, "/alternatename:_oadsimpl_AdsConnect60=_AdsConnect60")
__declspec(dllexport) UNSIGNED32 __stdcall AdsConnect60(UNSIGNED8* a0, UNSIGNED16 a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED32 a4, ADSHANDLE* a5) {
    return oadsimpl_AdsConnect60(a0, a1, a2, a3, a4, a5);
}

/* ---- AdsContinue ---- */
#define AdsContinue oadsimpl_AdsContinue
extern UNSIGNED32 ENTRYPOINT AdsContinue(ADSHANDLE hTable, UNSIGNED16* pbFound);
#undef AdsContinue
#pragma comment(linker, "/alternatename:_oadsimpl_AdsContinue=_AdsContinue")
__declspec(dllexport) UNSIGNED32 __stdcall AdsContinue(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsContinue(a0, a1);
}

/* ---- AdsConvertAnsiToOem ---- */
#define AdsConvertAnsiToOem oadsimpl_AdsConvertAnsiToOem
extern UNSIGNED32 ENTRYPOINT AdsConvertAnsiToOem(UNSIGNED8* pucBuf, UNSIGNED32* pulLen);
#undef AdsConvertAnsiToOem
#pragma comment(linker, "/alternatename:_oadsimpl_AdsConvertAnsiToOem=_AdsConvertAnsiToOem")
__declspec(dllexport) UNSIGNED32 __stdcall AdsConvertAnsiToOem(UNSIGNED8* a0, UNSIGNED32* a1) {
    return oadsimpl_AdsConvertAnsiToOem(a0, a1);
}

/* ---- AdsConvertOemToAnsi ---- */
#define AdsConvertOemToAnsi oadsimpl_AdsConvertOemToAnsi
extern UNSIGNED32 ENTRYPOINT AdsConvertOemToAnsi(UNSIGNED8* pucBuf, UNSIGNED32* pulLen);
#undef AdsConvertOemToAnsi
#pragma comment(linker, "/alternatename:_oadsimpl_AdsConvertOemToAnsi=_AdsConvertOemToAnsi")
__declspec(dllexport) UNSIGNED32 __stdcall AdsConvertOemToAnsi(UNSIGNED8* a0, UNSIGNED32* a1) {
    return oadsimpl_AdsConvertOemToAnsi(a0, a1);
}

/* ---- AdsConvertTable ---- */
#define AdsConvertTable oadsimpl_AdsConvertTable
extern UNSIGNED32 ENTRYPOINT AdsConvertTable(ADSHANDLE hHandle, UNSIGNED16 usFilterOption, UNSIGNED8* pucFile, UNSIGNED16 usTargetType);
#undef AdsConvertTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsConvertTable=_AdsConvertTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsConvertTable(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED8* a2, UNSIGNED16 a3) {
    return oadsimpl_AdsConvertTable(a0, a1, a2, a3);
}

/* ---- AdsCopyTable ---- */
#define AdsCopyTable oadsimpl_AdsCopyTable
extern UNSIGNED32 ENTRYPOINT AdsCopyTable(ADSHANDLE hHandle, UNSIGNED16 usFilterOption, UNSIGNED8* pucFile);
#undef AdsCopyTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCopyTable=_AdsCopyTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCopyTable(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED8* a2) {
    return oadsimpl_AdsCopyTable(a0, a1, a2);
}

/* ---- AdsCopyTableContent ---- */
#define AdsCopyTableContent oadsimpl_AdsCopyTableContent
extern UNSIGNED32 ENTRYPOINT AdsCopyTableContent(ADSHANDLE hSrc, ADSHANDLE hDst);
#undef AdsCopyTableContent
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCopyTableContent=_AdsCopyTableContent")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCopyTableContent(ADSHANDLE a0, ADSHANDLE a1) {
    return oadsimpl_AdsCopyTableContent(a0, a1);
}

/* ---- AdsCopyTableContents ---- */
#define AdsCopyTableContents oadsimpl_AdsCopyTableContents
extern UNSIGNED32 ENTRYPOINT AdsCopyTableContents(ADSHANDLE hSrc, ADSHANDLE hDst, UNSIGNED16 usFilterOption);
#undef AdsCopyTableContents
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCopyTableContents=_AdsCopyTableContents")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCopyTableContents(ADSHANDLE a0, ADSHANDLE a1, UNSIGNED16 a2) {
    return oadsimpl_AdsCopyTableContents(a0, a1, a2);
}

/* ---- AdsCopyTableStructure ---- */
#define AdsCopyTableStructure oadsimpl_AdsCopyTableStructure
extern UNSIGNED32 ENTRYPOINT AdsCopyTableStructure(ADSHANDLE hTable, UNSIGNED8* pucFile);
#undef AdsCopyTableStructure
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCopyTableStructure=_AdsCopyTableStructure")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCopyTableStructure(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsCopyTableStructure(a0, a1);
}

/* ---- AdsCreateFTSIndex ---- */
#define AdsCreateFTSIndex oadsimpl_AdsCreateFTSIndex
extern UNSIGNED32 ENTRYPOINT AdsCreateFTSIndex(ADSHANDLE hTable, UNSIGNED8* pucFileName, UNSIGNED8* pucTag, UNSIGNED8* pucField, UNSIGNED32 ulPageSize, UNSIGNED32 ulMinWordLen, UNSIGNED32 ulMaxWordLen, UNSIGNED16 usUseDefaultDelim, UNSIGNED8* pucDelimiters, UNSIGNED16 usUseDefaultNoise, UNSIGNED8* pucNoiseWords, UNSIGNED16 usUseDefaultDrop, UNSIGNED8* pucDropChars, UNSIGNED16 usUseDefaultConditionals, UNSIGNED8* pucConditionalChars, UNSIGNED8* pucReserved1, UNSIGNED8* pucReserved2, UNSIGNED32 ulOptions);
#undef AdsCreateFTSIndex
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCreateFTSIndex=_AdsCreateFTSIndex")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCreateFTSIndex(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED32 a4, UNSIGNED32 a5, UNSIGNED32 a6, UNSIGNED16 a7, UNSIGNED8* a8, UNSIGNED16 a9, UNSIGNED8* a10, UNSIGNED16 a11, UNSIGNED8* a12, UNSIGNED16 a13, UNSIGNED8* a14, UNSIGNED8* a15, UNSIGNED8* a16, UNSIGNED32 a17) {
    return oadsimpl_AdsCreateFTSIndex(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17);
}

/* ---- AdsCreateIndex61 ---- */
#define AdsCreateIndex61 oadsimpl_AdsCreateIndex61
extern UNSIGNED32 ENTRYPOINT AdsCreateIndex61(ADSHANDLE hTable, UNSIGNED8* pucFileName, UNSIGNED8* pucIndexName, UNSIGNED8* pucExpr, UNSIGNED8* pucCondition, UNSIGNED8* pucKeyFilter, UNSIGNED32 ulOptions, UNSIGNED16 usPageSize, ADSHANDLE* phIndex);
#undef AdsCreateIndex61
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCreateIndex61=_AdsCreateIndex61")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCreateIndex61(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5, UNSIGNED32 a6, UNSIGNED16 a7, ADSHANDLE* a8) {
    return oadsimpl_AdsCreateIndex61(a0, a1, a2, a3, a4, a5, a6, a7, a8);
}

/* ---- AdsCreateSQLStatement ---- */
#define AdsCreateSQLStatement oadsimpl_AdsCreateSQLStatement
extern UNSIGNED32 ENTRYPOINT AdsCreateSQLStatement(ADSHANDLE hConnect, ADSHANDLE* phStatement);
#undef AdsCreateSQLStatement
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCreateSQLStatement=_AdsCreateSQLStatement")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCreateSQLStatement(ADSHANDLE a0, ADSHANDLE* a1) {
    return oadsimpl_AdsCreateSQLStatement(a0, a1);
}

/* ---- AdsCreateSavepoint ---- */
#define AdsCreateSavepoint oadsimpl_AdsCreateSavepoint
extern UNSIGNED32 ENTRYPOINT AdsCreateSavepoint(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED32 ulOptions);
#undef AdsCreateSavepoint
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCreateSavepoint=_AdsCreateSavepoint")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCreateSavepoint(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32 a2) {
    return oadsimpl_AdsCreateSavepoint(a0, a1, a2);
}

/* ---- AdsCreateTable ---- */
#define AdsCreateTable oadsimpl_AdsCreateTable
extern UNSIGNED32 ENTRYPOINT AdsCreateTable(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucAlias, UNSIGNED16 usTableType, UNSIGNED16 usCharType, UNSIGNED16 usLockType, UNSIGNED16 usCheckRights, UNSIGNED16 usMemoBlockSize, UNSIGNED8* pucFields, ADSHANDLE* phTable);
#undef AdsCreateTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCreateTable=_AdsCreateTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCreateTable(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, UNSIGNED16 a4, UNSIGNED16 a5, UNSIGNED16 a6, UNSIGNED16 a7, UNSIGNED8* a8, ADSHANDLE* a9) {
    return oadsimpl_AdsCreateTable(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9);
}

/* ---- AdsCustomizeAOF ---- */
#define AdsCustomizeAOF oadsimpl_AdsCustomizeAOF
extern UNSIGNED32 ENTRYPOINT AdsCustomizeAOF(ADSHANDLE hTable, UNSIGNED32 ulNumRecords, UNSIGNED32* pulRecords, UNSIGNED16 usOption);
#undef AdsCustomizeAOF
#pragma comment(linker, "/alternatename:_oadsimpl_AdsCustomizeAOF=_AdsCustomizeAOF")
__declspec(dllexport) UNSIGNED32 __stdcall AdsCustomizeAOF(ADSHANDLE a0, UNSIGNED32 a1, UNSIGNED32* a2, UNSIGNED16 a3) {
    return oadsimpl_AdsCustomizeAOF(a0, a1, a2, a3);
}

/* ---- AdsDDAddIndexFile ---- */
#define AdsDDAddIndexFile oadsimpl_AdsDDAddIndexFile
extern UNSIGNED32 ENTRYPOINT AdsDDAddIndexFile(ADSHANDLE hConnect, UNSIGNED8* pucTable, UNSIGNED8* pucIndex, UNSIGNED8* pucComment);
#undef AdsDDAddIndexFile
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDAddIndexFile=_AdsDDAddIndexFile")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDAddIndexFile(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    return oadsimpl_AdsDDAddIndexFile(a0, a1, a2, a3);
}

/* ---- AdsDDAddProcedure ---- */
#define AdsDDAddProcedure oadsimpl_AdsDDAddProcedure
extern UNSIGNED32 ENTRYPOINT AdsDDAddProcedure(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucContainer, UNSIGNED8* pucProcName, UNSIGNED32 ulInvokeOption, UNSIGNED8* pucInParams, UNSIGNED8* pucOutParams, UNSIGNED8* pucComments);
#undef AdsDDAddProcedure
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDAddProcedure=_AdsDDAddProcedure")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDAddProcedure(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED32 a4, UNSIGNED8* a5, UNSIGNED8* a6, UNSIGNED8* a7) {
    return oadsimpl_AdsDDAddProcedure(a0, a1, a2, a3, a4, a5, a6, a7);
}

/* ---- AdsDDAddTable ---- */
#define AdsDDAddTable oadsimpl_AdsDDAddTable
extern UNSIGNED32 ENTRYPOINT AdsDDAddTable(ADSHANDLE hConnect, UNSIGNED8* pucAlias, UNSIGNED8* pucTablePath, UNSIGNED16 usFileType, UNSIGNED16 usCharType, UNSIGNED8* pucIndexPath, UNSIGNED8* pucComment);
#undef AdsDDAddTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDAddTable=_AdsDDAddTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDAddTable(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, UNSIGNED16 a4, UNSIGNED8* a5, UNSIGNED8* a6) {
    return oadsimpl_AdsDDAddTable(a0, a1, a2, a3, a4, a5, a6);
}

/* ---- AdsDDAddTable90 ---- */
#define AdsDDAddTable90 oadsimpl_AdsDDAddTable90
extern UNSIGNED32 ENTRYPOINT AdsDDAddTable90(ADSHANDLE hConnect, UNSIGNED8* pucAlias, UNSIGNED8* pucTablePath, UNSIGNED16 usTableType, UNSIGNED16 usCharType, UNSIGNED8* pucIndexPath, UNSIGNED8* pucComment, UNSIGNED8* pucCollation);
#undef AdsDDAddTable90
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDAddTable90=_AdsDDAddTable90")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDAddTable90(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, UNSIGNED16 a4, UNSIGNED8* a5, UNSIGNED8* a6, UNSIGNED8* a7) {
    return oadsimpl_AdsDDAddTable90(a0, a1, a2, a3, a4, a5, a6, a7);
}

/* ---- AdsDDAddUserToGroup ---- */
#define AdsDDAddUserToGroup oadsimpl_AdsDDAddUserToGroup
extern UNSIGNED32 ENTRYPOINT AdsDDAddUserToGroup(ADSHANDLE hConnect, UNSIGNED8* pucGroup, UNSIGNED8* pucUser);
#undef AdsDDAddUserToGroup
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDAddUserToGroup=_AdsDDAddUserToGroup")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDAddUserToGroup(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    return oadsimpl_AdsDDAddUserToGroup(a0, a1, a2);
}

/* ---- AdsDDCreate ---- */
#define AdsDDCreate oadsimpl_AdsDDCreate
extern UNSIGNED32 ENTRYPOINT AdsDDCreate(UNSIGNED8* pucDictionary, UNSIGNED16 bEncrypt, UNSIGNED8* pucAdminPassword, ADSHANDLE* phConnect);
#undef AdsDDCreate
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreate=_AdsDDCreate")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreate(UNSIGNED8* a0, UNSIGNED16 a1, UNSIGNED8* a2, ADSHANDLE* a3) {
    return oadsimpl_AdsDDCreate(a0, a1, a2, a3);
}

/* ---- AdsDDCreateLink ---- */
#define AdsDDCreateLink oadsimpl_AdsDDCreateLink
extern UNSIGNED32 ENTRYPOINT AdsDDCreateLink(ADSHANDLE hConnect, UNSIGNED8* pucLink, UNSIGNED8* pucPath, UNSIGNED8* pucUser, UNSIGNED8* pucPwd, UNSIGNED16 usOptions);
#undef AdsDDCreateLink
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreateLink=_AdsDDCreateLink")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreateLink(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED16 a5) {
    return oadsimpl_AdsDDCreateLink(a0, a1, a2, a3, a4, a5);
}

/* ---- AdsDDCreateProcedure ---- */
#define AdsDDCreateProcedure oadsimpl_AdsDDCreateProcedure
extern UNSIGNED32 ENTRYPOINT AdsDDCreateProcedure(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucContainer, UNSIGNED8* pucProcName, UNSIGNED32 ulInvokeOption, UNSIGNED8* pucInParams, UNSIGNED8* pucOutParams, UNSIGNED8* pucComments);
#undef AdsDDCreateProcedure
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreateProcedure=_AdsDDCreateProcedure")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreateProcedure(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED32 a4, UNSIGNED8* a5, UNSIGNED8* a6, UNSIGNED8* a7) {
    return oadsimpl_AdsDDCreateProcedure(a0, a1, a2, a3, a4, a5, a6, a7);
}

/* ---- AdsDDCreateRefIntegrity ---- */
#define AdsDDCreateRefIntegrity oadsimpl_AdsDDCreateRefIntegrity
extern UNSIGNED32 ENTRYPOINT AdsDDCreateRefIntegrity(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucFail, UNSIGNED8* pucParent, UNSIGNED8* pucParentTag, UNSIGNED8* pucChild, UNSIGNED8* pucChildTag, UNSIGNED16 usUpdate, UNSIGNED16 usDelete);
#undef AdsDDCreateRefIntegrity
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreateRefIntegrity=_AdsDDCreateRefIntegrity")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreateRefIntegrity(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED8* a5, UNSIGNED8* a6, UNSIGNED16 a7, UNSIGNED16 a8) {
    return oadsimpl_AdsDDCreateRefIntegrity(a0, a1, a2, a3, a4, a5, a6, a7, a8);
}

/* ---- AdsDDCreateUser ---- */
#define AdsDDCreateUser oadsimpl_AdsDDCreateUser
extern UNSIGNED32 ENTRYPOINT AdsDDCreateUser(ADSHANDLE hConnect, UNSIGNED8* pucGroup, UNSIGNED8* pucUser, UNSIGNED8* pucPwd, UNSIGNED8* pucComment);
#undef AdsDDCreateUser
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreateUser=_AdsDDCreateUser")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreateUser(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4) {
    return oadsimpl_AdsDDCreateUser(a0, a1, a2, a3, a4);
}

/* ---- AdsDDCreateView ---- */
#define AdsDDCreateView oadsimpl_AdsDDCreateView
extern UNSIGNED32 ENTRYPOINT AdsDDCreateView(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucComments, UNSIGNED8* pucSQL);
#undef AdsDDCreateView
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDCreateView=_AdsDDCreateView")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDCreateView(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3) {
    return oadsimpl_AdsDDCreateView(a0, a1, a2, a3);
}

/* ---- AdsDDDeleteUser ---- */
#define AdsDDDeleteUser oadsimpl_AdsDDDeleteUser
extern UNSIGNED32 ENTRYPOINT AdsDDDeleteUser(ADSHANDLE hConnect, UNSIGNED8* pucUser);
#undef AdsDDDeleteUser
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDDeleteUser=_AdsDDDeleteUser")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDDeleteUser(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsDDDeleteUser(a0, a1);
}

/* ---- AdsDDDropFunction ---- */
#define AdsDDDropFunction oadsimpl_AdsDDDropFunction
extern UNSIGNED32 ENTRYPOINT AdsDDDropFunction(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsDDDropFunction
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDDropFunction=_AdsDDDropFunction")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDDropFunction(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsDDDropFunction(a0, a1);
}

/* ---- AdsDDDropLink ---- */
#define AdsDDDropLink oadsimpl_AdsDDDropLink
extern UNSIGNED32 ENTRYPOINT AdsDDDropLink(ADSHANDLE hConnect, UNSIGNED8* pucLink, UNSIGNED16 usOptions);
#undef AdsDDDropLink
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDDropLink=_AdsDDDropLink")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDDropLink(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2) {
    return oadsimpl_AdsDDDropLink(a0, a1, a2);
}

/* ---- AdsDDDropProcedure ---- */
#define AdsDDDropProcedure oadsimpl_AdsDDDropProcedure
extern UNSIGNED32 ENTRYPOINT AdsDDDropProcedure(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsDDDropProcedure
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDDropProcedure=_AdsDDDropProcedure")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDDropProcedure(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsDDDropProcedure(a0, a1);
}

/* ---- AdsDDDropTrigger ---- */
#define AdsDDDropTrigger oadsimpl_AdsDDDropTrigger
extern UNSIGNED32 ENTRYPOINT AdsDDDropTrigger(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsDDDropTrigger
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDDropTrigger=_AdsDDDropTrigger")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDDropTrigger(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsDDDropTrigger(a0, a1);
}

/* ---- AdsDDDropView ---- */
#define AdsDDDropView oadsimpl_AdsDDDropView
extern UNSIGNED32 ENTRYPOINT AdsDDDropView(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsDDDropView
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDDropView=_AdsDDDropView")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDDropView(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsDDDropView(a0, a1);
}

/* ---- AdsDDFindClose ---- */
#define AdsDDFindClose oadsimpl_AdsDDFindClose
extern UNSIGNED32 ENTRYPOINT AdsDDFindClose(ADSHANDLE hObject, ADSHANDLE hFindHandle);
#undef AdsDDFindClose
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDFindClose=_AdsDDFindClose")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDFindClose(ADSHANDLE a0, ADSHANDLE a1) {
    return oadsimpl_AdsDDFindClose(a0, a1);
}

/* ---- AdsDDFindFirstObject ---- */
#define AdsDDFindFirstObject oadsimpl_AdsDDFindFirstObject
extern UNSIGNED32 ENTRYPOINT AdsDDFindFirstObject(ADSHANDLE hObject, UNSIGNED16 usFindObjectType, UNSIGNED8* pucParentName, UNSIGNED8* pucObjectName, UNSIGNED16* pusObjectNameLen, ADSHANDLE* phFindHandle);
#undef AdsDDFindFirstObject
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDFindFirstObject=_AdsDDFindFirstObject")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDFindFirstObject(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED16* a4, ADSHANDLE* a5) {
    return oadsimpl_AdsDDFindFirstObject(a0, a1, a2, a3, a4, a5);
}

/* ---- AdsDDFindNextObject ---- */
#define AdsDDFindNextObject oadsimpl_AdsDDFindNextObject
extern UNSIGNED32 ENTRYPOINT AdsDDFindNextObject(ADSHANDLE hObject, ADSHANDLE hFindHandle, UNSIGNED8* pucObjectName, UNSIGNED16* pusObjectNameLen);
#undef AdsDDFindNextObject
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDFindNextObject=_AdsDDFindNextObject")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDFindNextObject(ADSHANDLE a0, ADSHANDLE a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    return oadsimpl_AdsDDFindNextObject(a0, a1, a2, a3);
}

/* ---- AdsDDGetDatabaseProperty ---- */
#define AdsDDGetDatabaseProperty oadsimpl_AdsDDGetDatabaseProperty
extern UNSIGNED32 ENTRYPOINT AdsDDGetDatabaseProperty(ADSHANDLE hConnect, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16* pusLen);
#undef AdsDDGetDatabaseProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetDatabaseProperty=_AdsDDGetDatabaseProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetDatabaseProperty(ADSHANDLE a0, UNSIGNED16 a1, void* a2, UNSIGNED16* a3) {
    return oadsimpl_AdsDDGetDatabaseProperty(a0, a1, a2, a3);
}

/* ---- AdsDDGetFieldProperty ---- */
#define AdsDDGetFieldProperty oadsimpl_AdsDDGetFieldProperty
extern UNSIGNED32 ENTRYPOINT AdsDDGetFieldProperty(ADSHANDLE hConnect, UNSIGNED8* pucTable, UNSIGNED8* pucField, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16* pusLen);
#undef AdsDDGetFieldProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetFieldProperty=_AdsDDGetFieldProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetFieldProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, void* a4, UNSIGNED16* a5) {
    return oadsimpl_AdsDDGetFieldProperty(a0, a1, a2, a3, a4, a5);
}

/* ---- AdsDDGetFunctionProperty ---- */
#define AdsDDGetFunctionProperty oadsimpl_AdsDDGetFunctionProperty
extern UNSIGNED32 ENTRYPOINT AdsDDGetFunctionProperty(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usPropertyID, void* pvProperty, UNSIGNED16* pusPropertyLen);
#undef AdsDDGetFunctionProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetFunctionProperty=_AdsDDGetFunctionProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetFunctionProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16* a4) {
    return oadsimpl_AdsDDGetFunctionProperty(a0, a1, a2, a3, a4);
}

/* ---- AdsDDGetIndexProperty ---- */
#define AdsDDGetIndexProperty oadsimpl_AdsDDGetIndexProperty
extern UNSIGNED32 ENTRYPOINT AdsDDGetIndexProperty(ADSHANDLE hConnect, UNSIGNED8* pucTable, UNSIGNED8* pucIndex, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16* pusLen);
#undef AdsDDGetIndexProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetIndexProperty=_AdsDDGetIndexProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetIndexProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, void* a4, UNSIGNED16* a5) {
    return oadsimpl_AdsDDGetIndexProperty(a0, a1, a2, a3, a4, a5);
}

/* ---- AdsDDGetProcProperty ---- */
#define AdsDDGetProcProperty oadsimpl_AdsDDGetProcProperty
extern UNSIGNED32 ENTRYPOINT AdsDDGetProcProperty(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16* pusLen);
#undef AdsDDGetProcProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetProcProperty=_AdsDDGetProcProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetProcProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16* a4) {
    return oadsimpl_AdsDDGetProcProperty(a0, a1, a2, a3, a4);
}

/* ---- AdsDDGetProcedureProperty ---- */
#define AdsDDGetProcedureProperty oadsimpl_AdsDDGetProcedureProperty
extern UNSIGNED32 ENTRYPOINT AdsDDGetProcedureProperty(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16* pusLen);
#undef AdsDDGetProcedureProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetProcedureProperty=_AdsDDGetProcedureProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetProcedureProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16* a4) {
    return oadsimpl_AdsDDGetProcedureProperty(a0, a1, a2, a3, a4);
}

/* ---- AdsDDGetRefIntegrityProperty ---- */
#define AdsDDGetRefIntegrityProperty oadsimpl_AdsDDGetRefIntegrityProperty
extern UNSIGNED32 ENTRYPOINT AdsDDGetRefIntegrityProperty(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16* pusLen);
#undef AdsDDGetRefIntegrityProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetRefIntegrityProperty=_AdsDDGetRefIntegrityProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetRefIntegrityProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16* a4) {
    return oadsimpl_AdsDDGetRefIntegrityProperty(a0, a1, a2, a3, a4);
}

/* ---- AdsDDGetUserProperty ---- */
#define AdsDDGetUserProperty oadsimpl_AdsDDGetUserProperty
extern UNSIGNED32 ENTRYPOINT AdsDDGetUserProperty(ADSHANDLE hConnect, UNSIGNED8* pucUser, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16* pusLen);
#undef AdsDDGetUserProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetUserProperty=_AdsDDGetUserProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetUserProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16* a4) {
    return oadsimpl_AdsDDGetUserProperty(a0, a1, a2, a3, a4);
}

/* ---- AdsDDGetUserTableRights ---- */
#define AdsDDGetUserTableRights oadsimpl_AdsDDGetUserTableRights
extern UNSIGNED32 ENTRYPOINT AdsDDGetUserTableRights(ADSHANDLE hConnect, UNSIGNED8* pucTable, UNSIGNED8* pucUser, UNSIGNED32* pulLevel);
#undef AdsDDGetUserTableRights
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDGetUserTableRights=_AdsDDGetUserTableRights")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDGetUserTableRights(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32* a3) {
    return oadsimpl_AdsDDGetUserTableRights(a0, a1, a2, a3);
}

/* ---- AdsDDModifyLink ---- */
#define AdsDDModifyLink oadsimpl_AdsDDModifyLink
extern UNSIGNED32 ENTRYPOINT AdsDDModifyLink(ADSHANDLE hConnect, UNSIGNED8* pucLink, UNSIGNED8* pucPath, UNSIGNED8* pucUser, UNSIGNED8* pucPwd, UNSIGNED16 usOptions);
#undef AdsDDModifyLink
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDModifyLink=_AdsDDModifyLink")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDModifyLink(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED8* a4, UNSIGNED16 a5) {
    return oadsimpl_AdsDDModifyLink(a0, a1, a2, a3, a4, a5);
}

/* ---- AdsDDRemoveIndexFile ---- */
#define AdsDDRemoveIndexFile oadsimpl_AdsDDRemoveIndexFile
extern UNSIGNED32 ENTRYPOINT AdsDDRemoveIndexFile(ADSHANDLE hConnect, UNSIGNED8* pucTable, UNSIGNED8* pucIndex, UNSIGNED16 usOptions);
#undef AdsDDRemoveIndexFile
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDRemoveIndexFile=_AdsDDRemoveIndexFile")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDRemoveIndexFile(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3) {
    return oadsimpl_AdsDDRemoveIndexFile(a0, a1, a2, a3);
}

/* ---- AdsDDRemoveProcedure ---- */
#define AdsDDRemoveProcedure oadsimpl_AdsDDRemoveProcedure
extern UNSIGNED32 ENTRYPOINT AdsDDRemoveProcedure(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsDDRemoveProcedure
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDRemoveProcedure=_AdsDDRemoveProcedure")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDRemoveProcedure(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsDDRemoveProcedure(a0, a1);
}

/* ---- AdsDDRemoveRefIntegrity ---- */
#define AdsDDRemoveRefIntegrity oadsimpl_AdsDDRemoveRefIntegrity
extern UNSIGNED32 ENTRYPOINT AdsDDRemoveRefIntegrity(ADSHANDLE hConnect, UNSIGNED8* pucRI);
#undef AdsDDRemoveRefIntegrity
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDRemoveRefIntegrity=_AdsDDRemoveRefIntegrity")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDRemoveRefIntegrity(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsDDRemoveRefIntegrity(a0, a1);
}

/* ---- AdsDDRemoveTable ---- */
#define AdsDDRemoveTable oadsimpl_AdsDDRemoveTable
extern UNSIGNED32 ENTRYPOINT AdsDDRemoveTable(ADSHANDLE hConnect, UNSIGNED8* pucAlias, UNSIGNED16 usDeleteFiles);
#undef AdsDDRemoveTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDRemoveTable=_AdsDDRemoveTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDRemoveTable(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2) {
    return oadsimpl_AdsDDRemoveTable(a0, a1, a2);
}

/* ---- AdsDDRemoveTrigger ---- */
#define AdsDDRemoveTrigger oadsimpl_AdsDDRemoveTrigger
extern UNSIGNED32 ENTRYPOINT AdsDDRemoveTrigger(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsDDRemoveTrigger
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDRemoveTrigger=_AdsDDRemoveTrigger")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDRemoveTrigger(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsDDRemoveTrigger(a0, a1);
}

/* ---- AdsDDRemoveUserFromGroup ---- */
#define AdsDDRemoveUserFromGroup oadsimpl_AdsDDRemoveUserFromGroup
extern UNSIGNED32 ENTRYPOINT AdsDDRemoveUserFromGroup(ADSHANDLE hConnect, UNSIGNED8* pucGroup, UNSIGNED8* pucUser);
#undef AdsDDRemoveUserFromGroup
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDRemoveUserFromGroup=_AdsDDRemoveUserFromGroup")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDRemoveUserFromGroup(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    return oadsimpl_AdsDDRemoveUserFromGroup(a0, a1, a2);
}

/* ---- AdsDDSetDatabaseProperty ---- */
#define AdsDDSetDatabaseProperty oadsimpl_AdsDDSetDatabaseProperty
extern UNSIGNED32 ENTRYPOINT AdsDDSetDatabaseProperty(ADSHANDLE hConnect, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16 usLen);
#undef AdsDDSetDatabaseProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetDatabaseProperty=_AdsDDSetDatabaseProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetDatabaseProperty(ADSHANDLE a0, UNSIGNED16 a1, void* a2, UNSIGNED16 a3) {
    return oadsimpl_AdsDDSetDatabaseProperty(a0, a1, a2, a3);
}

/* ---- AdsDDSetFieldProperty ---- */
#define AdsDDSetFieldProperty oadsimpl_AdsDDSetFieldProperty
extern UNSIGNED32 ENTRYPOINT AdsDDSetFieldProperty(ADSHANDLE hConnect, UNSIGNED8* pucTable, UNSIGNED8* pucField, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16 usLen);
#undef AdsDDSetFieldProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetFieldProperty=_AdsDDSetFieldProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetFieldProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, void* a4, UNSIGNED16 a5) {
    return oadsimpl_AdsDDSetFieldProperty(a0, a1, a2, a3, a4, a5);
}

/* ---- AdsDDSetFunctionProperty ---- */
#define AdsDDSetFunctionProperty oadsimpl_AdsDDSetFunctionProperty
extern UNSIGNED32 ENTRYPOINT AdsDDSetFunctionProperty(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usPropertyID, void* pvProperty, UNSIGNED16 usPropertyLen);
#undef AdsDDSetFunctionProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetFunctionProperty=_AdsDDSetFunctionProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetFunctionProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16 a4) {
    return oadsimpl_AdsDDSetFunctionProperty(a0, a1, a2, a3, a4);
}

/* ---- AdsDDSetIndexProperty ---- */
#define AdsDDSetIndexProperty oadsimpl_AdsDDSetIndexProperty
extern UNSIGNED32 ENTRYPOINT AdsDDSetIndexProperty(ADSHANDLE hConnect, UNSIGNED8* pucTable, UNSIGNED8* pucIndex, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16 usLen);
#undef AdsDDSetIndexProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetIndexProperty=_AdsDDSetIndexProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetIndexProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, void* a4, UNSIGNED16 a5) {
    return oadsimpl_AdsDDSetIndexProperty(a0, a1, a2, a3, a4, a5);
}

/* ---- AdsDDSetProcProperty ---- */
#define AdsDDSetProcProperty oadsimpl_AdsDDSetProcProperty
extern UNSIGNED32 ENTRYPOINT AdsDDSetProcProperty(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16 usLen);
#undef AdsDDSetProcProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetProcProperty=_AdsDDSetProcProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetProcProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16 a4) {
    return oadsimpl_AdsDDSetProcProperty(a0, a1, a2, a3, a4);
}

/* ---- AdsDDSetProcedureProperty ---- */
#define AdsDDSetProcedureProperty oadsimpl_AdsDDSetProcedureProperty
extern UNSIGNED32 ENTRYPOINT AdsDDSetProcedureProperty(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16 usLen);
#undef AdsDDSetProcedureProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetProcedureProperty=_AdsDDSetProcedureProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetProcedureProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16 a4) {
    return oadsimpl_AdsDDSetProcedureProperty(a0, a1, a2, a3, a4);
}

/* ---- AdsDDSetRefIntegrityProperty ---- */
#define AdsDDSetRefIntegrityProperty oadsimpl_AdsDDSetRefIntegrityProperty
extern UNSIGNED32 ENTRYPOINT AdsDDSetRefIntegrityProperty(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usProp, void* pvBuf, UNSIGNED16 usLen);
#undef AdsDDSetRefIntegrityProperty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetRefIntegrityProperty=_AdsDDSetRefIntegrityProperty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetRefIntegrityProperty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16 a4) {
    return oadsimpl_AdsDDSetRefIntegrityProperty(a0, a1, a2, a3, a4);
}

/* ---- AdsDDSetUserTableRights ---- */
#define AdsDDSetUserTableRights oadsimpl_AdsDDSetUserTableRights
extern UNSIGNED32 ENTRYPOINT AdsDDSetUserTableRights(ADSHANDLE hConnect, UNSIGNED8* pucTable, UNSIGNED8* pucUser, UNSIGNED32 ulLevel);
#undef AdsDDSetUserTableRights
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDDSetUserTableRights=_AdsDDSetUserTableRights")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDDSetUserTableRights(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32 a3) {
    return oadsimpl_AdsDDSetUserTableRights(a0, a1, a2, a3);
}

/* ---- AdsData ---- */
#define AdsData oadsimpl_AdsData
extern UNSIGNED32 ENTRYPOINT AdsData(UNSIGNED16 usFlag, void* pvData);
#undef AdsData
#pragma comment(linker, "/alternatename:_oadsimpl_AdsData=_AdsData")
__declspec(dllexport) UNSIGNED32 __stdcall AdsData(UNSIGNED16 a0, void* a1) {
    return oadsimpl_AdsData(a0, a1);
}

/* ---- AdsDecryptRecord ---- */
#define AdsDecryptRecord oadsimpl_AdsDecryptRecord
extern UNSIGNED32 ENTRYPOINT AdsDecryptRecord(ADSHANDLE hTable);
#undef AdsDecryptRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDecryptRecord=_AdsDecryptRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDecryptRecord(ADSHANDLE a0) {
    return oadsimpl_AdsDecryptRecord(a0);
}

/* ---- AdsDecryptTable ---- */
#define AdsDecryptTable oadsimpl_AdsDecryptTable
extern UNSIGNED32 ENTRYPOINT AdsDecryptTable(ADSHANDLE hTable);
#undef AdsDecryptTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDecryptTable=_AdsDecryptTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDecryptTable(ADSHANDLE a0) {
    return oadsimpl_AdsDecryptTable(a0);
}

/* ---- AdsDeleteCustomKey ---- */
#define AdsDeleteCustomKey oadsimpl_AdsDeleteCustomKey
extern UNSIGNED32 ENTRYPOINT AdsDeleteCustomKey(ADSHANDLE hIndex);
#undef AdsDeleteCustomKey
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDeleteCustomKey=_AdsDeleteCustomKey")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDeleteCustomKey(ADSHANDLE a0) {
    return oadsimpl_AdsDeleteCustomKey(a0);
}

/* ---- AdsDeleteFile ---- */
#define AdsDeleteFile oadsimpl_AdsDeleteFile
extern UNSIGNED32 ENTRYPOINT AdsDeleteFile(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsDeleteFile
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDeleteFile=_AdsDeleteFile")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDeleteFile(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsDeleteFile(a0, a1);
}

/* ---- AdsDeleteIndex ---- */
#define AdsDeleteIndex oadsimpl_AdsDeleteIndex
extern UNSIGNED32 ENTRYPOINT AdsDeleteIndex(ADSHANDLE hIndex);
#undef AdsDeleteIndex
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDeleteIndex=_AdsDeleteIndex")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDeleteIndex(ADSHANDLE a0) {
    return oadsimpl_AdsDeleteIndex(a0);
}

/* ---- AdsDeleteRecord ---- */
#define AdsDeleteRecord oadsimpl_AdsDeleteRecord
extern UNSIGNED32 ENTRYPOINT AdsDeleteRecord(ADSHANDLE hTable);
#undef AdsDeleteRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDeleteRecord=_AdsDeleteRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDeleteRecord(ADSHANDLE a0) {
    return oadsimpl_AdsDeleteRecord(a0);
}

/* ---- AdsDirExist ---- */
#define AdsDirExist oadsimpl_AdsDirExist
extern UNSIGNED32 ENTRYPOINT AdsDirExist(ADSHANDLE hConnect, UNSIGNED8* pucPath, UNSIGNED16* pbExists);
#undef AdsDirExist
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDirExist=_AdsDirExist")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDirExist(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsDirExist(a0, a1, a2);
}

/* ---- AdsDirMake ---- */
#define AdsDirMake oadsimpl_AdsDirMake
extern UNSIGNED32 ENTRYPOINT AdsDirMake(ADSHANDLE hConnect, UNSIGNED8* pucPath);
#undef AdsDirMake
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDirMake=_AdsDirMake")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDirMake(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsDirMake(a0, a1);
}

/* ---- AdsDirRemove ---- */
#define AdsDirRemove oadsimpl_AdsDirRemove
extern UNSIGNED32 ENTRYPOINT AdsDirRemove(ADSHANDLE hConnect, UNSIGNED8* pucPath);
#undef AdsDirRemove
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDirRemove=_AdsDirRemove")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDirRemove(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsDirRemove(a0, a1);
}

/* ---- AdsDirectory ---- */
#define AdsDirectory oadsimpl_AdsDirectory
extern UNSIGNED32 ENTRYPOINT AdsDirectory(ADSHANDLE hConnect, UNSIGNED8* pucMask, UNSIGNED16 usAttr, UNSIGNED8* pucBuffer, UNSIGNED32* pulBufLen);
#undef AdsDirectory
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDirectory=_AdsDirectory")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDirectory(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, UNSIGNED8* a3, UNSIGNED32* a4) {
    return oadsimpl_AdsDirectory(a0, a1, a2, a3, a4);
}

/* ---- AdsDisableAutoIncEnforcement ---- */
#define AdsDisableAutoIncEnforcement oadsimpl_AdsDisableAutoIncEnforcement
extern UNSIGNED32 ENTRYPOINT AdsDisableAutoIncEnforcement(ADSHANDLE hConnect);
#undef AdsDisableAutoIncEnforcement
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDisableAutoIncEnforcement=_AdsDisableAutoIncEnforcement")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDisableAutoIncEnforcement(ADSHANDLE a0) {
    return oadsimpl_AdsDisableAutoIncEnforcement(a0);
}

/* ---- AdsDisableEncryption ---- */
#define AdsDisableEncryption oadsimpl_AdsDisableEncryption
extern UNSIGNED32 ENTRYPOINT AdsDisableEncryption(ADSHANDLE hConnect);
#undef AdsDisableEncryption
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDisableEncryption=_AdsDisableEncryption")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDisableEncryption(ADSHANDLE a0) {
    return oadsimpl_AdsDisableEncryption(a0);
}

/* ---- AdsDisableLocalConnections ---- */
#define AdsDisableLocalConnections oadsimpl_AdsDisableLocalConnections
extern UNSIGNED32 ENTRYPOINT AdsDisableLocalConnections(void);
#undef AdsDisableLocalConnections
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDisableLocalConnections=_AdsDisableLocalConnections")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDisableLocalConnections(void) {
    return oadsimpl_AdsDisableLocalConnections();
}

/* ---- AdsDisableRI ---- */
#define AdsDisableRI oadsimpl_AdsDisableRI
extern UNSIGNED32 ENTRYPOINT AdsDisableRI(ADSHANDLE hConnect);
#undef AdsDisableRI
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDisableRI=_AdsDisableRI")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDisableRI(ADSHANDLE a0) {
    return oadsimpl_AdsDisableRI(a0);
}

/* ---- AdsDisableUniqueEnforcement ---- */
#define AdsDisableUniqueEnforcement oadsimpl_AdsDisableUniqueEnforcement
extern UNSIGNED32 ENTRYPOINT AdsDisableUniqueEnforcement(ADSHANDLE hConnect);
#undef AdsDisableUniqueEnforcement
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDisableUniqueEnforcement=_AdsDisableUniqueEnforcement")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDisableUniqueEnforcement(ADSHANDLE a0) {
    return oadsimpl_AdsDisableUniqueEnforcement(a0);
}

/* ---- AdsDisconnect ---- */
#define AdsDisconnect oadsimpl_AdsDisconnect
extern UNSIGNED32 ENTRYPOINT AdsDisconnect(ADSHANDLE hConnect);
#undef AdsDisconnect
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDisconnect=_AdsDisconnect")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDisconnect(ADSHANDLE a0) {
    return oadsimpl_AdsDisconnect(a0);
}

/* ---- AdsDropTable ---- */
#define AdsDropTable oadsimpl_AdsDropTable
extern UNSIGNED32 ENTRYPOINT AdsDropTable(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usDeleteFiles);
#undef AdsDropTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsDropTable=_AdsDropTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsDropTable(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2) {
    return oadsimpl_AdsDropTable(a0, a1, a2);
}

/* ---- AdsEnableAutoIncEnforcement ---- */
#define AdsEnableAutoIncEnforcement oadsimpl_AdsEnableAutoIncEnforcement
extern UNSIGNED32 ENTRYPOINT AdsEnableAutoIncEnforcement(ADSHANDLE hConnect);
#undef AdsEnableAutoIncEnforcement
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEnableAutoIncEnforcement=_AdsEnableAutoIncEnforcement")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEnableAutoIncEnforcement(ADSHANDLE a0) {
    return oadsimpl_AdsEnableAutoIncEnforcement(a0);
}

/* ---- AdsEnableEncryption ---- */
#define AdsEnableEncryption oadsimpl_AdsEnableEncryption
extern UNSIGNED32 ENTRYPOINT AdsEnableEncryption(ADSHANDLE hConnect, UNSIGNED8* pucPassword);
#undef AdsEnableEncryption
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEnableEncryption=_AdsEnableEncryption")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEnableEncryption(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsEnableEncryption(a0, a1);
}

/* ---- AdsEnableRI ---- */
#define AdsEnableRI oadsimpl_AdsEnableRI
extern UNSIGNED32 ENTRYPOINT AdsEnableRI(ADSHANDLE hConnect);
#undef AdsEnableRI
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEnableRI=_AdsEnableRI")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEnableRI(ADSHANDLE a0) {
    return oadsimpl_AdsEnableRI(a0);
}

/* ---- AdsEnableUniqueEnforcement ---- */
#define AdsEnableUniqueEnforcement oadsimpl_AdsEnableUniqueEnforcement
extern UNSIGNED32 ENTRYPOINT AdsEnableUniqueEnforcement(ADSHANDLE hConnect);
#undef AdsEnableUniqueEnforcement
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEnableUniqueEnforcement=_AdsEnableUniqueEnforcement")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEnableUniqueEnforcement(ADSHANDLE a0) {
    return oadsimpl_AdsEnableUniqueEnforcement(a0);
}

/* ---- AdsEncryptRecord ---- */
#define AdsEncryptRecord oadsimpl_AdsEncryptRecord
extern UNSIGNED32 ENTRYPOINT AdsEncryptRecord(ADSHANDLE hTable);
#undef AdsEncryptRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEncryptRecord=_AdsEncryptRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEncryptRecord(ADSHANDLE a0) {
    return oadsimpl_AdsEncryptRecord(a0);
}

/* ---- AdsEncryptTable ---- */
#define AdsEncryptTable oadsimpl_AdsEncryptTable
extern UNSIGNED32 ENTRYPOINT AdsEncryptTable(ADSHANDLE hTable);
#undef AdsEncryptTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEncryptTable=_AdsEncryptTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEncryptTable(ADSHANDLE a0) {
    return oadsimpl_AdsEncryptTable(a0);
}

/* ---- AdsEvalAOF ---- */
#define AdsEvalAOF oadsimpl_AdsEvalAOF
extern UNSIGNED32 ENTRYPOINT AdsEvalAOF(ADSHANDLE hTable, UNSIGNED8* pucExpr, UNSIGNED16* pusOptLevel);
#undef AdsEvalAOF
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEvalAOF=_AdsEvalAOF")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEvalAOF(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsEvalAOF(a0, a1, a2);
}

/* ---- AdsEvalLogicalExpr ---- */
#define AdsEvalLogicalExpr oadsimpl_AdsEvalLogicalExpr
extern UNSIGNED32 ENTRYPOINT AdsEvalLogicalExpr(ADSHANDLE hTable, UNSIGNED8* pucExpr, UNSIGNED16* pbResult);
#undef AdsEvalLogicalExpr
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEvalLogicalExpr=_AdsEvalLogicalExpr")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEvalLogicalExpr(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsEvalLogicalExpr(a0, a1, a2);
}

/* ---- AdsEvalNumericExpr ---- */
#define AdsEvalNumericExpr oadsimpl_AdsEvalNumericExpr
extern UNSIGNED32 ENTRYPOINT AdsEvalNumericExpr(ADSHANDLE hTable, UNSIGNED8* pucExpr, double* pdResult);
#undef AdsEvalNumericExpr
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEvalNumericExpr=_AdsEvalNumericExpr")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEvalNumericExpr(ADSHANDLE a0, UNSIGNED8* a1, double* a2) {
    return oadsimpl_AdsEvalNumericExpr(a0, a1, a2);
}

/* ---- AdsEvalStringExpr ---- */
#define AdsEvalStringExpr oadsimpl_AdsEvalStringExpr
extern UNSIGNED32 ENTRYPOINT AdsEvalStringExpr(ADSHANDLE hTable, UNSIGNED8* pucExpr, UNSIGNED8* pucResult, UNSIGNED16* pusLen);
#undef AdsEvalStringExpr
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEvalStringExpr=_AdsEvalStringExpr")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEvalStringExpr(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    return oadsimpl_AdsEvalStringExpr(a0, a1, a2, a3);
}

/* ---- AdsEvalTestExpr ---- */
#define AdsEvalTestExpr oadsimpl_AdsEvalTestExpr
extern UNSIGNED32 ENTRYPOINT AdsEvalTestExpr(ADSHANDLE hTable, UNSIGNED8* pucExpr, UNSIGNED16* pusType);
#undef AdsEvalTestExpr
#pragma comment(linker, "/alternatename:_oadsimpl_AdsEvalTestExpr=_AdsEvalTestExpr")
__declspec(dllexport) UNSIGNED32 __stdcall AdsEvalTestExpr(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsEvalTestExpr(a0, a1, a2);
}

/* ---- AdsExecuteSQL ---- */
#define AdsExecuteSQL oadsimpl_AdsExecuteSQL
extern UNSIGNED32 ENTRYPOINT AdsExecuteSQL(ADSHANDLE hStatement, ADSHANDLE* phCursor);
#undef AdsExecuteSQL
#pragma comment(linker, "/alternatename:_oadsimpl_AdsExecuteSQL=_AdsExecuteSQL")
__declspec(dllexport) UNSIGNED32 __stdcall AdsExecuteSQL(ADSHANDLE a0, ADSHANDLE* a1) {
    return oadsimpl_AdsExecuteSQL(a0, a1);
}

/* ---- AdsExecuteSQLDirect ---- */
#define AdsExecuteSQLDirect oadsimpl_AdsExecuteSQLDirect
extern UNSIGNED32 ENTRYPOINT AdsExecuteSQLDirect(ADSHANDLE hStatement, UNSIGNED8* pucSQL, ADSHANDLE* phCursor);
#undef AdsExecuteSQLDirect
#pragma comment(linker, "/alternatename:_oadsimpl_AdsExecuteSQLDirect=_AdsExecuteSQLDirect")
__declspec(dllexport) UNSIGNED32 __stdcall AdsExecuteSQLDirect(ADSHANDLE a0, UNSIGNED8* a1, ADSHANDLE* a2) {
    return oadsimpl_AdsExecuteSQLDirect(a0, a1, a2);
}

/* ---- AdsExecuteSQLDirectW ---- */
#define AdsExecuteSQLDirectW oadsimpl_AdsExecuteSQLDirectW
extern UNSIGNED32 ENTRYPOINT AdsExecuteSQLDirectW(ADSHANDLE hStatement, UNSIGNED16* pwcSQL, ADSHANDLE* phCursor);
#undef AdsExecuteSQLDirectW
#pragma comment(linker, "/alternatename:_oadsimpl_AdsExecuteSQLDirectW=_AdsExecuteSQLDirectW")
__declspec(dllexport) UNSIGNED32 __stdcall AdsExecuteSQLDirectW(ADSHANDLE a0, UNSIGNED16* a1, ADSHANDLE* a2) {
    return oadsimpl_AdsExecuteSQLDirectW(a0, a1, a2);
}

/* ---- AdsExtractKey ---- */
#define AdsExtractKey oadsimpl_AdsExtractKey
extern UNSIGNED32 ENTRYPOINT AdsExtractKey(ADSHANDLE hIndex, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsExtractKey
#pragma comment(linker, "/alternatename:_oadsimpl_AdsExtractKey=_AdsExtractKey")
__declspec(dllexport) UNSIGNED32 __stdcall AdsExtractKey(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsExtractKey(a0, a1, a2);
}

/* ---- AdsFClose ---- */
#define AdsFClose oadsimpl_AdsFClose
extern UNSIGNED32 ENTRYPOINT AdsFClose(ADSHANDLE hFile);
#undef AdsFClose
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFClose=_AdsFClose")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFClose(ADSHANDLE a0) {
    return oadsimpl_AdsFClose(a0);
}

/* ---- AdsFCreate ---- */
#define AdsFCreate oadsimpl_AdsFCreate
extern UNSIGNED32 ENTRYPOINT AdsFCreate(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usAttribute, ADSHANDLE* phFile);
#undef AdsFCreate
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFCreate=_AdsFCreate")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFCreate(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, ADSHANDLE* a3) {
    return oadsimpl_AdsFCreate(a0, a1, a2, a3);
}

/* ---- AdsFOpen ---- */
#define AdsFOpen oadsimpl_AdsFOpen
extern UNSIGNED32 ENTRYPOINT AdsFOpen(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED16 usMode, ADSHANDLE* phFile);
#undef AdsFOpen
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFOpen=_AdsFOpen")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFOpen(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, ADSHANDLE* a3) {
    return oadsimpl_AdsFOpen(a0, a1, a2, a3);
}

/* ---- AdsFRead ---- */
#define AdsFRead oadsimpl_AdsFRead
extern UNSIGNED32 ENTRYPOINT AdsFRead(ADSHANDLE hFile, void* pBuf, UNSIGNED32 ulLen, UNSIGNED32* pulRead);
#undef AdsFRead
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFRead=_AdsFRead")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFRead(ADSHANDLE a0, void* a1, UNSIGNED32 a2, UNSIGNED32* a3) {
    return oadsimpl_AdsFRead(a0, a1, a2, a3);
}

/* ---- AdsFSeek ---- */
#define AdsFSeek oadsimpl_AdsFSeek
extern UNSIGNED32 ENTRYPOINT AdsFSeek(ADSHANDLE hFile, SIGNED32 lOffset, UNSIGNED16 usOrigin, UNSIGNED32* pulPos);
#undef AdsFSeek
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFSeek=_AdsFSeek")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFSeek(ADSHANDLE a0, SIGNED32 a1, UNSIGNED16 a2, UNSIGNED32* a3) {
    return oadsimpl_AdsFSeek(a0, a1, a2, a3);
}

/* ---- AdsFTSSearch ---- */
#define AdsFTSSearch oadsimpl_AdsFTSSearch
extern UNSIGNED32 ENTRYPOINT AdsFTSSearch(ADSHANDLE hConnect, UNSIGNED8* pucFile, UNSIGNED8* pucQuery, UNSIGNED32* paRecnos, UNSIGNED32* pulCount);
#undef AdsFTSSearch
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFTSSearch=_AdsFTSSearch")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFTSSearch(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32* a3, UNSIGNED32* a4) {
    return oadsimpl_AdsFTSSearch(a0, a1, a2, a3, a4);
}

/* ---- AdsFWrite ---- */
#define AdsFWrite oadsimpl_AdsFWrite
extern UNSIGNED32 ENTRYPOINT AdsFWrite(ADSHANDLE hFile, const void* pBuf, UNSIGNED32 ulLen, UNSIGNED32* pulWritten);
#undef AdsFWrite
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFWrite=_AdsFWrite")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFWrite(ADSHANDLE a0, const void* a1, UNSIGNED32 a2, UNSIGNED32* a3) {
    return oadsimpl_AdsFWrite(a0, a1, a2, a3);
}

/* ---- AdsFailedTransactionRecovery ---- */
#define AdsFailedTransactionRecovery oadsimpl_AdsFailedTransactionRecovery
extern UNSIGNED32 ENTRYPOINT AdsFailedTransactionRecovery(UNSIGNED8* pucServer);
#undef AdsFailedTransactionRecovery
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFailedTransactionRecovery=_AdsFailedTransactionRecovery")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFailedTransactionRecovery(UNSIGNED8* a0) {
    return oadsimpl_AdsFailedTransactionRecovery(a0);
}

/* ---- AdsFileToBinary ---- */
#define AdsFileToBinary oadsimpl_AdsFileToBinary
extern UNSIGNED32 ENTRYPOINT AdsFileToBinary(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16 usType, UNSIGNED8* pucPath);
#undef AdsFileToBinary
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFileToBinary=_AdsFileToBinary")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFileToBinary(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, UNSIGNED8* a3) {
    return oadsimpl_AdsFileToBinary(a0, a1, a2, a3);
}

/* ---- AdsFileToBinaryW ---- */
#define AdsFileToBinaryW oadsimpl_AdsFileToBinaryW
extern UNSIGNED32 ENTRYPOINT AdsFileToBinaryW(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16 usType, UNSIGNED16* pwcPath);
#undef AdsFileToBinaryW
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFileToBinaryW=_AdsFileToBinaryW")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFileToBinaryW(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, UNSIGNED16* a3) {
    return oadsimpl_AdsFileToBinaryW(a0, a1, a2, a3);
}

/* ---- AdsFilterOption ---- */
#define AdsFilterOption oadsimpl_AdsFilterOption
extern UNSIGNED32 ENTRYPOINT AdsFilterOption(ADSHANDLE hTable, UNSIGNED16 usOption, UNSIGNED16* pusValue);
#undef AdsFilterOption
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFilterOption=_AdsFilterOption")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFilterOption(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED16* a2) {
    return oadsimpl_AdsFilterOption(a0, a1, a2);
}

/* ---- AdsFindClose ---- */
#define AdsFindClose oadsimpl_AdsFindClose
extern UNSIGNED32 ENTRYPOINT AdsFindClose(ADSHANDLE hConnect, ADSHANDLE hFind);
#undef AdsFindClose
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFindClose=_AdsFindClose")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFindClose(ADSHANDLE a0, ADSHANDLE a1) {
    return oadsimpl_AdsFindClose(a0, a1);
}

/* ---- AdsFindConnection ---- */
#define AdsFindConnection oadsimpl_AdsFindConnection
extern UNSIGNED32 ENTRYPOINT AdsFindConnection(UNSIGNED8* pucServerName, ADSHANDLE* phConnect);
#undef AdsFindConnection
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFindConnection=_AdsFindConnection")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFindConnection(UNSIGNED8* a0, ADSHANDLE* a1) {
    return oadsimpl_AdsFindConnection(a0, a1);
}

/* ---- AdsFindConnection25 ---- */
#define AdsFindConnection25 oadsimpl_AdsFindConnection25
extern UNSIGNED32 ENTRYPOINT AdsFindConnection25(UNSIGNED8* pucFullPath, ADSHANDLE* phConnect);
#undef AdsFindConnection25
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFindConnection25=_AdsFindConnection25")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFindConnection25(UNSIGNED8* a0, ADSHANDLE* a1) {
    return oadsimpl_AdsFindConnection25(a0, a1);
}

/* ---- AdsFindFirstTable ---- */
#define AdsFindFirstTable oadsimpl_AdsFindFirstTable
extern UNSIGNED32 ENTRYPOINT AdsFindFirstTable(ADSHANDLE hConnect, UNSIGNED8* pucMask, UNSIGNED8* pucFileName, UNSIGNED16* pusFileNameLen, ADSHANDLE* phFind);
#undef AdsFindFirstTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFindFirstTable=_AdsFindFirstTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFindFirstTable(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16* a3, ADSHANDLE* a4) {
    return oadsimpl_AdsFindFirstTable(a0, a1, a2, a3, a4);
}

/* ---- AdsFindNextTable ---- */
#define AdsFindNextTable oadsimpl_AdsFindNextTable
extern UNSIGNED32 ENTRYPOINT AdsFindNextTable(ADSHANDLE hConnect, ADSHANDLE hFind, UNSIGNED8* pucFileName, UNSIGNED16* pusFileNameLen);
#undef AdsFindNextTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFindNextTable=_AdsFindNextTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFindNextTable(ADSHANDLE a0, ADSHANDLE a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    return oadsimpl_AdsFindNextTable(a0, a1, a2, a3);
}

/* ---- AdsFlushFileBuffers ---- */
#define AdsFlushFileBuffers oadsimpl_AdsFlushFileBuffers
extern UNSIGNED32 ENTRYPOINT AdsFlushFileBuffers(ADSHANDLE hTable);
#undef AdsFlushFileBuffers
#pragma comment(linker, "/alternatename:_oadsimpl_AdsFlushFileBuffers=_AdsFlushFileBuffers")
__declspec(dllexport) UNSIGNED32 __stdcall AdsFlushFileBuffers(ADSHANDLE a0) {
    return oadsimpl_AdsFlushFileBuffers(a0);
}

/* ---- AdsGetAOF ---- */
#define AdsGetAOF oadsimpl_AdsGetAOF
extern UNSIGNED32 ENTRYPOINT AdsGetAOF(ADSHANDLE hTable, UNSIGNED8* pucFilter, UNSIGNED16* pusLen);
#undef AdsGetAOF
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetAOF=_AdsGetAOF")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetAOF(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsGetAOF(a0, a1, a2);
}

/* ---- AdsGetAOFOptLevel ---- */
#define AdsGetAOFOptLevel oadsimpl_AdsGetAOFOptLevel
extern UNSIGNED32 ENTRYPOINT AdsGetAOFOptLevel(ADSHANDLE hTable, UNSIGNED16* pusLevel, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetAOFOptLevel
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetAOFOptLevel=_AdsGetAOFOptLevel")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetAOFOptLevel(ADSHANDLE a0, UNSIGNED16* a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    return oadsimpl_AdsGetAOFOptLevel(a0, a1, a2, a3);
}

/* ---- AdsGetAllIndexes ---- */
#define AdsGetAllIndexes oadsimpl_AdsGetAllIndexes
extern UNSIGNED32 ENTRYPOINT AdsGetAllIndexes(ADSHANDLE hTable, ADSHANDLE* ahIndex, UNSIGNED16* pusArrayLen);
#undef AdsGetAllIndexes
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetAllIndexes=_AdsGetAllIndexes")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetAllIndexes(ADSHANDLE a0, ADSHANDLE* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsGetAllIndexes(a0, a1, a2);
}

/* ---- AdsGetAllLocks ---- */
#define AdsGetAllLocks oadsimpl_AdsGetAllLocks
extern UNSIGNED32 ENTRYPOINT AdsGetAllLocks(ADSHANDLE hTable, UNSIGNED32* paRecnos, UNSIGNED16* pusCount);
#undef AdsGetAllLocks
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetAllLocks=_AdsGetAllLocks")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetAllLocks(ADSHANDLE a0, UNSIGNED32* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsGetAllLocks(a0, a1, a2);
}

/* ---- AdsGetAllTables ---- */
#define AdsGetAllTables oadsimpl_AdsGetAllTables
extern UNSIGNED32 ENTRYPOINT AdsGetAllTables(ADSHANDLE hConnect, ADSHANDLE* ahTable, UNSIGNED16* pusArrayLen);
#undef AdsGetAllTables
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetAllTables=_AdsGetAllTables")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetAllTables(ADSHANDLE a0, ADSHANDLE* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsGetAllTables(a0, a1, a2);
}

/* ---- AdsGetBinary ---- */
#define AdsGetBinary oadsimpl_AdsGetBinary
extern UNSIGNED32 ENTRYPOINT AdsGetBinary(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED32 ulOffset, UNSIGNED8* pucBuf, UNSIGNED32* pulLen);
#undef AdsGetBinary
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetBinary=_AdsGetBinary")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetBinary(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32 a2, UNSIGNED8* a3, UNSIGNED32* a4) {
    return oadsimpl_AdsGetBinary(a0, a1, a2, a3, a4);
}

/* ---- AdsGetBinaryLength ---- */
#define AdsGetBinaryLength oadsimpl_AdsGetBinaryLength
extern UNSIGNED32 ENTRYPOINT AdsGetBinaryLength(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED32* pulLength);
#undef AdsGetBinaryLength
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetBinaryLength=_AdsGetBinaryLength")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetBinaryLength(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32* a2) {
    return oadsimpl_AdsGetBinaryLength(a0, a1, a2);
}

/* ---- AdsGetBookmark ---- */
#define AdsGetBookmark oadsimpl_AdsGetBookmark
extern UNSIGNED32 ENTRYPOINT AdsGetBookmark(ADSHANDLE hTable, ADSHANDLE* phBookmark);
#undef AdsGetBookmark
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetBookmark=_AdsGetBookmark")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetBookmark(ADSHANDLE a0, ADSHANDLE* a1) {
    return oadsimpl_AdsGetBookmark(a0, a1);
}

/* ---- AdsGetBookmark60 ---- */
#define AdsGetBookmark60 oadsimpl_AdsGetBookmark60
extern UNSIGNED32 ENTRYPOINT AdsGetBookmark60(ADSHANDLE hObj, UNSIGNED8* pucBookmark, UNSIGNED32* pulLength);
#undef AdsGetBookmark60
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetBookmark60=_AdsGetBookmark60")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetBookmark60(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32* a2) {
    return oadsimpl_AdsGetBookmark60(a0, a1, a2);
}

/* ---- AdsGetConnectionType ---- */
#define AdsGetConnectionType oadsimpl_AdsGetConnectionType
extern UNSIGNED32 ENTRYPOINT AdsGetConnectionType(ADSHANDLE hConnect, UNSIGNED16* pusType);
#undef AdsGetConnectionType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetConnectionType=_AdsGetConnectionType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetConnectionType(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsGetConnectionType(a0, a1);
}

/* ---- AdsGetDate ---- */
#define AdsGetDate oadsimpl_AdsGetDate
extern UNSIGNED32 ENTRYPOINT AdsGetDate(ADSHANDLE hObj, UNSIGNED8* pucFldId, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetDate
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetDate=_AdsGetDate")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetDate(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    return oadsimpl_AdsGetDate(a0, a1, a2, a3);
}

/* ---- AdsGetDateFormat ---- */
#define AdsGetDateFormat oadsimpl_AdsGetDateFormat
extern UNSIGNED32 ENTRYPOINT AdsGetDateFormat(UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetDateFormat
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetDateFormat=_AdsGetDateFormat")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetDateFormat(UNSIGNED8* a0, UNSIGNED16* a1) {
    return oadsimpl_AdsGetDateFormat(a0, a1);
}

/* ---- AdsGetDateFormat60 ---- */
#define AdsGetDateFormat60 oadsimpl_AdsGetDateFormat60
extern UNSIGNED32 ENTRYPOINT AdsGetDateFormat60(ADSHANDLE hConnect, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetDateFormat60
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetDateFormat60=_AdsGetDateFormat60")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetDateFormat60(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsGetDateFormat60(a0, a1, a2);
}

/* ---- AdsGetDefault ---- */
#define AdsGetDefault oadsimpl_AdsGetDefault
extern UNSIGNED32 ENTRYPOINT AdsGetDefault(UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetDefault
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetDefault=_AdsGetDefault")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetDefault(UNSIGNED8* a0, UNSIGNED16* a1) {
    return oadsimpl_AdsGetDefault(a0, a1);
}

/* ---- AdsGetDeleted ---- */
#define AdsGetDeleted oadsimpl_AdsGetDeleted
extern UNSIGNED32 ENTRYPOINT AdsGetDeleted(UNSIGNED16* pbShow);
#undef AdsGetDeleted
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetDeleted=_AdsGetDeleted")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetDeleted(UNSIGNED16* a0) {
    return oadsimpl_AdsGetDeleted(a0);
}

/* ---- AdsGetDouble ---- */
#define AdsGetDouble oadsimpl_AdsGetDouble
extern UNSIGNED32 ENTRYPOINT AdsGetDouble(ADSHANDLE hTable, UNSIGNED8* pucField, double* pdValue);
#undef AdsGetDouble
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetDouble=_AdsGetDouble")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetDouble(ADSHANDLE a0, UNSIGNED8* a1, double* a2) {
    return oadsimpl_AdsGetDouble(a0, a1, a2);
}

/* ---- AdsGetEpoch ---- */
#define AdsGetEpoch oadsimpl_AdsGetEpoch
extern UNSIGNED32 ENTRYPOINT AdsGetEpoch(UNSIGNED16* pusEpoch);
#undef AdsGetEpoch
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetEpoch=_AdsGetEpoch")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetEpoch(UNSIGNED16* a0) {
    return oadsimpl_AdsGetEpoch(a0);
}

/* ---- AdsGetErrorString ---- */
#define AdsGetErrorString oadsimpl_AdsGetErrorString
extern UNSIGNED32 ENTRYPOINT AdsGetErrorString(UNSIGNED32 ulErr, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetErrorString
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetErrorString=_AdsGetErrorString")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetErrorString(UNSIGNED32 a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsGetErrorString(a0, a1, a2);
}

/* ---- AdsGetExact ---- */
#define AdsGetExact oadsimpl_AdsGetExact
extern UNSIGNED32 ENTRYPOINT AdsGetExact(UNSIGNED16* pbExact);
#undef AdsGetExact
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetExact=_AdsGetExact")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetExact(UNSIGNED16* a0) {
    return oadsimpl_AdsGetExact(a0);
}

/* ---- AdsGetExact22 ---- */
#define AdsGetExact22 oadsimpl_AdsGetExact22
extern UNSIGNED32 ENTRYPOINT AdsGetExact22(ADSHANDLE hObj, UNSIGNED16* pbExact);
#undef AdsGetExact22
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetExact22=_AdsGetExact22")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetExact22(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsGetExact22(a0, a1);
}

/* ---- AdsGetFTSIndexes ---- */
#define AdsGetFTSIndexes oadsimpl_AdsGetFTSIndexes
extern UNSIGNED32 ENTRYPOINT AdsGetFTSIndexes(ADSHANDLE hTable, ADSHANDLE* ahIndex, UNSIGNED16* pusArrayLen);
#undef AdsGetFTSIndexes
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFTSIndexes=_AdsGetFTSIndexes")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFTSIndexes(ADSHANDLE a0, ADSHANDLE* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsGetFTSIndexes(a0, a1, a2);
}

/* ---- AdsGetField ---- */
#define AdsGetField oadsimpl_AdsGetField
extern UNSIGNED32 ENTRYPOINT AdsGetField(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED8* pucBuf, UNSIGNED32* pulLen, UNSIGNED16 usOption);
#undef AdsGetField
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetField=_AdsGetField")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetField(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32* a3, UNSIGNED16 a4) {
    return oadsimpl_AdsGetField(a0, a1, a2, a3, a4);
}

/* ---- AdsGetFieldDecimals ---- */
#define AdsGetFieldDecimals oadsimpl_AdsGetFieldDecimals
extern UNSIGNED32 ENTRYPOINT AdsGetFieldDecimals(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pusDec);
#undef AdsGetFieldDecimals
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFieldDecimals=_AdsGetFieldDecimals")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFieldDecimals(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsGetFieldDecimals(a0, a1, a2);
}

/* ---- AdsGetFieldLength ---- */
#define AdsGetFieldLength oadsimpl_AdsGetFieldLength
extern UNSIGNED32 ENTRYPOINT AdsGetFieldLength(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED32* pulLen);
#undef AdsGetFieldLength
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFieldLength=_AdsGetFieldLength")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFieldLength(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32* a2) {
    return oadsimpl_AdsGetFieldLength(a0, a1, a2);
}

/* ---- AdsGetFieldLength100 ---- */
#define AdsGetFieldLength100 oadsimpl_AdsGetFieldLength100
extern UNSIGNED32 ENTRYPOINT AdsGetFieldLength100(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED32 ulOptions, UNSIGNED32* pulLen);
#undef AdsGetFieldLength100
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFieldLength100=_AdsGetFieldLength100")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFieldLength100(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32 a2, UNSIGNED32* a3) {
    return oadsimpl_AdsGetFieldLength100(a0, a1, a2, a3);
}

/* ---- AdsGetFieldName ---- */
#define AdsGetFieldName oadsimpl_AdsGetFieldName
extern UNSIGNED32 ENTRYPOINT AdsGetFieldName(ADSHANDLE hTable, UNSIGNED16 usFieldNum, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetFieldName
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFieldName=_AdsGetFieldName")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFieldName(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    return oadsimpl_AdsGetFieldName(a0, a1, a2, a3);
}

/* ---- AdsGetFieldNum ---- */
#define AdsGetFieldNum oadsimpl_AdsGetFieldNum
extern UNSIGNED32 ENTRYPOINT AdsGetFieldNum(ADSHANDLE hTable, UNSIGNED8* pucFldName, UNSIGNED16* pusNum);
#undef AdsGetFieldNum
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFieldNum=_AdsGetFieldNum")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFieldNum(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsGetFieldNum(a0, a1, a2);
}

/* ---- AdsGetFieldOffset ---- */
#define AdsGetFieldOffset oadsimpl_AdsGetFieldOffset
extern UNSIGNED32 ENTRYPOINT AdsGetFieldOffset(ADSHANDLE hTable, UNSIGNED8* pucFldName, UNSIGNED32* pulOffset);
#undef AdsGetFieldOffset
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFieldOffset=_AdsGetFieldOffset")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFieldOffset(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32* a2) {
    return oadsimpl_AdsGetFieldOffset(a0, a1, a2);
}

/* ---- AdsGetFieldRaw ---- */
#define AdsGetFieldRaw oadsimpl_AdsGetFieldRaw
extern UNSIGNED32 ENTRYPOINT AdsGetFieldRaw(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED8* pucBuf, UNSIGNED32* pulLen);
#undef AdsGetFieldRaw
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFieldRaw=_AdsGetFieldRaw")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFieldRaw(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32* a3) {
    return oadsimpl_AdsGetFieldRaw(a0, a1, a2, a3);
}

/* ---- AdsGetFieldType ---- */
#define AdsGetFieldType oadsimpl_AdsGetFieldType
extern UNSIGNED32 ENTRYPOINT AdsGetFieldType(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pusType);
#undef AdsGetFieldType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFieldType=_AdsGetFieldType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFieldType(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsGetFieldType(a0, a1, a2);
}

/* ---- AdsGetFieldW ---- */
#define AdsGetFieldW oadsimpl_AdsGetFieldW
extern UNSIGNED32 ENTRYPOINT AdsGetFieldW(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pucBufW, UNSIGNED32* pulLenW, UNSIGNED16 usOption);
#undef AdsGetFieldW
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFieldW=_AdsGetFieldW")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFieldW(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2, UNSIGNED32* a3, UNSIGNED16 a4) {
    return oadsimpl_AdsGetFieldW(a0, a1, a2, a3, a4);
}

/* ---- AdsGetFileDate ---- */
#define AdsGetFileDate oadsimpl_AdsGetFileDate
extern UNSIGNED32 ENTRYPOINT AdsGetFileDate(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucDate, UNSIGNED16* pusLen);
#undef AdsGetFileDate
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFileDate=_AdsGetFileDate")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFileDate(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    return oadsimpl_AdsGetFileDate(a0, a1, a2, a3);
}

/* ---- AdsGetFileSize ---- */
#define AdsGetFileSize oadsimpl_AdsGetFileSize
extern UNSIGNED32 ENTRYPOINT AdsGetFileSize(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED32* pulSize);
#undef AdsGetFileSize
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFileSize=_AdsGetFileSize")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFileSize(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32* a2) {
    return oadsimpl_AdsGetFileSize(a0, a1, a2);
}

/* ---- AdsGetFileTime ---- */
#define AdsGetFileTime oadsimpl_AdsGetFileTime
extern UNSIGNED32 ENTRYPOINT AdsGetFileTime(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucTime, UNSIGNED16* pusLen);
#undef AdsGetFileTime
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFileTime=_AdsGetFileTime")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFileTime(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    return oadsimpl_AdsGetFileTime(a0, a1, a2, a3);
}

/* ---- AdsGetFilter ---- */
#define AdsGetFilter oadsimpl_AdsGetFilter
extern UNSIGNED32 ENTRYPOINT AdsGetFilter(ADSHANDLE hTable, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetFilter
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetFilter=_AdsGetFilter")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetFilter(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsGetFilter(a0, a1, a2);
}

/* ---- AdsGetHandleType ---- */
#define AdsGetHandleType oadsimpl_AdsGetHandleType
extern UNSIGNED32 ENTRYPOINT AdsGetHandleType(ADSHANDLE hAny, UNSIGNED16* pusType);
#undef AdsGetHandleType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetHandleType=_AdsGetHandleType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetHandleType(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsGetHandleType(a0, a1);
}

/* ---- AdsGetIndexCondition ---- */
#define AdsGetIndexCondition oadsimpl_AdsGetIndexCondition
extern UNSIGNED32 ENTRYPOINT AdsGetIndexCondition(ADSHANDLE hIndex, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetIndexCondition
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetIndexCondition=_AdsGetIndexCondition")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetIndexCondition(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsGetIndexCondition(a0, a1, a2);
}

/* ---- AdsGetIndexExpr ---- */
#define AdsGetIndexExpr oadsimpl_AdsGetIndexExpr
extern UNSIGNED32 ENTRYPOINT AdsGetIndexExpr(ADSHANDLE hIndex, UNSIGNED8* pucBuf, UNSIGNED16* pusBufLen);
#undef AdsGetIndexExpr
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetIndexExpr=_AdsGetIndexExpr")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetIndexExpr(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsGetIndexExpr(a0, a1, a2);
}

/* ---- AdsGetIndexFilename ---- */
#define AdsGetIndexFilename oadsimpl_AdsGetIndexFilename
extern UNSIGNED32 ENTRYPOINT AdsGetIndexFilename(ADSHANDLE hIndex, UNSIGNED16 usOption, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetIndexFilename
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetIndexFilename=_AdsGetIndexFilename")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetIndexFilename(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    return oadsimpl_AdsGetIndexFilename(a0, a1, a2, a3);
}

/* ---- AdsGetIndexHandle ---- */
#define AdsGetIndexHandle oadsimpl_AdsGetIndexHandle
extern UNSIGNED32 ENTRYPOINT AdsGetIndexHandle(ADSHANDLE hTable, UNSIGNED8* pucName, ADSHANDLE* phIndex);
#undef AdsGetIndexHandle
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetIndexHandle=_AdsGetIndexHandle")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetIndexHandle(ADSHANDLE a0, UNSIGNED8* a1, ADSHANDLE* a2) {
    return oadsimpl_AdsGetIndexHandle(a0, a1, a2);
}

/* ---- AdsGetIndexHandleByOrder ---- */
#define AdsGetIndexHandleByOrder oadsimpl_AdsGetIndexHandleByOrder
extern UNSIGNED32 ENTRYPOINT AdsGetIndexHandleByOrder(ADSHANDLE hTable, UNSIGNED16 usOrder, ADSHANDLE* phIndex);
#undef AdsGetIndexHandleByOrder
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetIndexHandleByOrder=_AdsGetIndexHandleByOrder")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetIndexHandleByOrder(ADSHANDLE a0, UNSIGNED16 a1, ADSHANDLE* a2) {
    return oadsimpl_AdsGetIndexHandleByOrder(a0, a1, a2);
}

/* ---- AdsGetIndexName ---- */
#define AdsGetIndexName oadsimpl_AdsGetIndexName
extern UNSIGNED32 ENTRYPOINT AdsGetIndexName(ADSHANDLE hIndex, UNSIGNED8* pucBuf, UNSIGNED16* pusBufLen);
#undef AdsGetIndexName
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetIndexName=_AdsGetIndexName")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetIndexName(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsGetIndexName(a0, a1, a2);
}

/* ---- AdsGetIndexOrderByHandle ---- */
#define AdsGetIndexOrderByHandle oadsimpl_AdsGetIndexOrderByHandle
extern UNSIGNED32 ENTRYPOINT AdsGetIndexOrderByHandle(ADSHANDLE hIndex, UNSIGNED16* pusOrder);
#undef AdsGetIndexOrderByHandle
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetIndexOrderByHandle=_AdsGetIndexOrderByHandle")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetIndexOrderByHandle(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsGetIndexOrderByHandle(a0, a1);
}

/* ---- AdsGetJulian ---- */
#define AdsGetJulian oadsimpl_AdsGetJulian
extern UNSIGNED32 ENTRYPOINT AdsGetJulian(ADSHANDLE hTable, UNSIGNED8* pucField, SIGNED32* plJulian);
#undef AdsGetJulian
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetJulian=_AdsGetJulian")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetJulian(ADSHANDLE a0, UNSIGNED8* a1, SIGNED32* a2) {
    return oadsimpl_AdsGetJulian(a0, a1, a2);
}

/* ---- AdsGetKeyCount ---- */
#define AdsGetKeyCount oadsimpl_AdsGetKeyCount
extern UNSIGNED32 ENTRYPOINT AdsGetKeyCount(ADSHANDLE hIndex, UNSIGNED16 usFilterOption, UNSIGNED32* pulCount);
#undef AdsGetKeyCount
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetKeyCount=_AdsGetKeyCount")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetKeyCount(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED32* a2) {
    return oadsimpl_AdsGetKeyCount(a0, a1, a2);
}

/* ---- AdsGetKeyLength ---- */
#define AdsGetKeyLength oadsimpl_AdsGetKeyLength
extern UNSIGNED32 ENTRYPOINT AdsGetKeyLength(ADSHANDLE hIndex, UNSIGNED16* pusLen);
#undef AdsGetKeyLength
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetKeyLength=_AdsGetKeyLength")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetKeyLength(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsGetKeyLength(a0, a1);
}

/* ---- AdsGetKeyNum ---- */
#define AdsGetKeyNum oadsimpl_AdsGetKeyNum
extern UNSIGNED32 ENTRYPOINT AdsGetKeyNum(ADSHANDLE hIndex, UNSIGNED16 usFlag, UNSIGNED32* pulKey);
#undef AdsGetKeyNum
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetKeyNum=_AdsGetKeyNum")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetKeyNum(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED32* a2) {
    return oadsimpl_AdsGetKeyNum(a0, a1, a2);
}

/* ---- AdsGetKeyType ---- */
#define AdsGetKeyType oadsimpl_AdsGetKeyType
extern UNSIGNED32 ENTRYPOINT AdsGetKeyType(ADSHANDLE hIndex, UNSIGNED16* pusType);
#undef AdsGetKeyType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetKeyType=_AdsGetKeyType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetKeyType(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsGetKeyType(a0, a1);
}

/* ---- AdsGetLastAutoinc ---- */
#define AdsGetLastAutoinc oadsimpl_AdsGetLastAutoinc
extern UNSIGNED32 ENTRYPOINT AdsGetLastAutoinc(ADSHANDLE hTable, UNSIGNED32* pulValue);
#undef AdsGetLastAutoinc
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetLastAutoinc=_AdsGetLastAutoinc")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetLastAutoinc(ADSHANDLE a0, UNSIGNED32* a1) {
    return oadsimpl_AdsGetLastAutoinc(a0, a1);
}

/* ---- AdsGetLastError ---- */
#define AdsGetLastError oadsimpl_AdsGetLastError
extern UNSIGNED32 ENTRYPOINT AdsGetLastError(UNSIGNED32* pulCode, UNSIGNED8* pucBuf, UNSIGNED16* pusBufLen);
#undef AdsGetLastError
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetLastError=_AdsGetLastError")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetLastError(UNSIGNED32* a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsGetLastError(a0, a1, a2);
}

/* ---- AdsGetLastTableUpdate ---- */
#define AdsGetLastTableUpdate oadsimpl_AdsGetLastTableUpdate
extern UNSIGNED32 ENTRYPOINT AdsGetLastTableUpdate(ADSHANDLE hTable, UNSIGNED8* pucDate, UNSIGNED16* pusLen);
#undef AdsGetLastTableUpdate
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetLastTableUpdate=_AdsGetLastTableUpdate")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetLastTableUpdate(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsGetLastTableUpdate(a0, a1, a2);
}

/* ---- AdsGetLockCycle ---- */
#define AdsGetLockCycle oadsimpl_AdsGetLockCycle
extern UNSIGNED32 ENTRYPOINT AdsGetLockCycle(ADSHANDLE hConnect, UNSIGNED32* pulCycle);
#undef AdsGetLockCycle
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetLockCycle=_AdsGetLockCycle")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetLockCycle(ADSHANDLE a0, UNSIGNED32* a1) {
    return oadsimpl_AdsGetLockCycle(a0, a1);
}

/* ---- AdsGetLockRetryCount ---- */
#define AdsGetLockRetryCount oadsimpl_AdsGetLockRetryCount
extern UNSIGNED32 ENTRYPOINT AdsGetLockRetryCount(ADSHANDLE hConnect, UNSIGNED16* pusRetryCount);
#undef AdsGetLockRetryCount
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetLockRetryCount=_AdsGetLockRetryCount")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetLockRetryCount(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsGetLockRetryCount(a0, a1);
}

/* ---- AdsGetLogical ---- */
#define AdsGetLogical oadsimpl_AdsGetLogical
extern UNSIGNED32 ENTRYPOINT AdsGetLogical(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pbValue);
#undef AdsGetLogical
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetLogical=_AdsGetLogical")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetLogical(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsGetLogical(a0, a1, a2);
}

/* ---- AdsGetLong ---- */
#define AdsGetLong oadsimpl_AdsGetLong
extern UNSIGNED32 ENTRYPOINT AdsGetLong(ADSHANDLE hTable, UNSIGNED8* pucField, SIGNED32* plVal);
#undef AdsGetLong
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetLong=_AdsGetLong")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetLong(ADSHANDLE a0, UNSIGNED8* a1, SIGNED32* a2) {
    return oadsimpl_AdsGetLong(a0, a1, a2);
}

/* ---- AdsGetLongLong ---- */
#define AdsGetLongLong oadsimpl_AdsGetLongLong
extern UNSIGNED32 ENTRYPOINT AdsGetLongLong(ADSHANDLE hTable, UNSIGNED8* pucField, int64_t* pllValue);
#undef AdsGetLongLong
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetLongLong=_AdsGetLongLong")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetLongLong(ADSHANDLE a0, UNSIGNED8* a1, int64_t* a2) {
    return oadsimpl_AdsGetLongLong(a0, a1, a2);
}

/* ---- AdsGetMemoBlockSize ---- */
#define AdsGetMemoBlockSize oadsimpl_AdsGetMemoBlockSize
extern UNSIGNED32 ENTRYPOINT AdsGetMemoBlockSize(ADSHANDLE hObj, UNSIGNED16* pusBlockSize);
#undef AdsGetMemoBlockSize
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetMemoBlockSize=_AdsGetMemoBlockSize")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetMemoBlockSize(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsGetMemoBlockSize(a0, a1);
}

/* ---- AdsGetMemoDataType ---- */
#define AdsGetMemoDataType oadsimpl_AdsGetMemoDataType
extern UNSIGNED32 ENTRYPOINT AdsGetMemoDataType(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pusType);
#undef AdsGetMemoDataType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetMemoDataType=_AdsGetMemoDataType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetMemoDataType(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsGetMemoDataType(a0, a1, a2);
}

/* ---- AdsGetMemoLength ---- */
#define AdsGetMemoLength oadsimpl_AdsGetMemoLength
extern UNSIGNED32 ENTRYPOINT AdsGetMemoLength(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED32* pulLen);
#undef AdsGetMemoLength
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetMemoLength=_AdsGetMemoLength")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetMemoLength(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32* a2) {
    return oadsimpl_AdsGetMemoLength(a0, a1, a2);
}

/* ---- AdsGetMilliseconds ---- */
#define AdsGetMilliseconds oadsimpl_AdsGetMilliseconds
extern UNSIGNED32 ENTRYPOINT AdsGetMilliseconds(ADSHANDLE hTable, UNSIGNED8* pucField, SIGNED32* plMs);
#undef AdsGetMilliseconds
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetMilliseconds=_AdsGetMilliseconds")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetMilliseconds(ADSHANDLE a0, UNSIGNED8* a1, SIGNED32* a2) {
    return oadsimpl_AdsGetMilliseconds(a0, a1, a2);
}

/* ---- AdsGetNumActiveLinks ---- */
#define AdsGetNumActiveLinks oadsimpl_AdsGetNumActiveLinks
extern UNSIGNED32 ENTRYPOINT AdsGetNumActiveLinks(ADSHANDLE hTable, UNSIGNED16* pusCount);
#undef AdsGetNumActiveLinks
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetNumActiveLinks=_AdsGetNumActiveLinks")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetNumActiveLinks(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsGetNumActiveLinks(a0, a1);
}

/* ---- AdsGetNumFields ---- */
#define AdsGetNumFields oadsimpl_AdsGetNumFields
extern UNSIGNED32 ENTRYPOINT AdsGetNumFields(ADSHANDLE hTable, UNSIGNED16* pusFields);
#undef AdsGetNumFields
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetNumFields=_AdsGetNumFields")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetNumFields(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsGetNumFields(a0, a1);
}

/* ---- AdsGetNumIndexes ---- */
#define AdsGetNumIndexes oadsimpl_AdsGetNumIndexes
extern UNSIGNED32 ENTRYPOINT AdsGetNumIndexes(ADSHANDLE hTable, UNSIGNED16* pusCount);
#undef AdsGetNumIndexes
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetNumIndexes=_AdsGetNumIndexes")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetNumIndexes(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsGetNumIndexes(a0, a1);
}

/* ---- AdsGetNumLocks ---- */
#define AdsGetNumLocks oadsimpl_AdsGetNumLocks
extern UNSIGNED32 ENTRYPOINT AdsGetNumLocks(ADSHANDLE hTable, UNSIGNED16* pusCount);
#undef AdsGetNumLocks
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetNumLocks=_AdsGetNumLocks")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetNumLocks(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsGetNumLocks(a0, a1);
}

/* ---- AdsGetNumOpenTables ---- */
#define AdsGetNumOpenTables oadsimpl_AdsGetNumOpenTables
extern UNSIGNED32 ENTRYPOINT AdsGetNumOpenTables(UNSIGNED16* pusCount);
#undef AdsGetNumOpenTables
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetNumOpenTables=_AdsGetNumOpenTables")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetNumOpenTables(UNSIGNED16* a0) {
    return oadsimpl_AdsGetNumOpenTables(a0);
}

/* ---- AdsGetNumParams ---- */
#define AdsGetNumParams oadsimpl_AdsGetNumParams
extern UNSIGNED32 ENTRYPOINT AdsGetNumParams(ADSHANDLE hStatement, UNSIGNED16* pusNumParams);
#undef AdsGetNumParams
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetNumParams=_AdsGetNumParams")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetNumParams(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsGetNumParams(a0, a1);
}

/* ---- AdsGetRecord ---- */
#define AdsGetRecord oadsimpl_AdsGetRecord
extern UNSIGNED32 ENTRYPOINT AdsGetRecord(ADSHANDLE hTable, UNSIGNED8* pucBuf, UNSIGNED32* pulLen);
#undef AdsGetRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetRecord=_AdsGetRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetRecord(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32* a2) {
    return oadsimpl_AdsGetRecord(a0, a1, a2);
}

/* ---- AdsGetRecordCRC ---- */
#define AdsGetRecordCRC oadsimpl_AdsGetRecordCRC
extern UNSIGNED32 ENTRYPOINT AdsGetRecordCRC(ADSHANDLE hTable, UNSIGNED32* pulCRC, UNSIGNED32 ulOptions);
#undef AdsGetRecordCRC
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetRecordCRC=_AdsGetRecordCRC")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetRecordCRC(ADSHANDLE a0, UNSIGNED32* a1, UNSIGNED32 a2) {
    return oadsimpl_AdsGetRecordCRC(a0, a1, a2);
}

/* ---- AdsGetRecordCount ---- */
#define AdsGetRecordCount oadsimpl_AdsGetRecordCount
extern UNSIGNED32 ENTRYPOINT AdsGetRecordCount(ADSHANDLE hTable, UNSIGNED16 bFilterOption, UNSIGNED32* pulRecordCount);
#undef AdsGetRecordCount
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetRecordCount=_AdsGetRecordCount")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetRecordCount(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED32* a2) {
    return oadsimpl_AdsGetRecordCount(a0, a1, a2);
}

/* ---- AdsGetRecordLength ---- */
#define AdsGetRecordLength oadsimpl_AdsGetRecordLength
extern UNSIGNED32 ENTRYPOINT AdsGetRecordLength(ADSHANDLE hTable, UNSIGNED32* pulLen);
#undef AdsGetRecordLength
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetRecordLength=_AdsGetRecordLength")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetRecordLength(ADSHANDLE a0, UNSIGNED32* a1) {
    return oadsimpl_AdsGetRecordLength(a0, a1);
}

/* ---- AdsGetRecordNum ---- */
#define AdsGetRecordNum oadsimpl_AdsGetRecordNum
extern UNSIGNED32 ENTRYPOINT AdsGetRecordNum(ADSHANDLE hTable, UNSIGNED16 bFilterOption, UNSIGNED32* pulRecordNum);
#undef AdsGetRecordNum
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetRecordNum=_AdsGetRecordNum")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetRecordNum(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED32* a2) {
    return oadsimpl_AdsGetRecordNum(a0, a1, a2);
}

/* ---- AdsGetRelKeyPos ---- */
#define AdsGetRelKeyPos oadsimpl_AdsGetRelKeyPos
extern UNSIGNED32 ENTRYPOINT AdsGetRelKeyPos(ADSHANDLE hIndex, double* pdPos);
#undef AdsGetRelKeyPos
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetRelKeyPos=_AdsGetRelKeyPos")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetRelKeyPos(ADSHANDLE a0, double* a1) {
    return oadsimpl_AdsGetRelKeyPos(a0, a1);
}

/* ---- AdsGetScope ---- */
#define AdsGetScope oadsimpl_AdsGetScope
extern UNSIGNED32 ENTRYPOINT AdsGetScope(ADSHANDLE hIndex, UNSIGNED16 usScope, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetScope
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetScope=_AdsGetScope")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetScope(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    return oadsimpl_AdsGetScope(a0, a1, a2, a3);
}

/* ---- AdsGetSearchPath ---- */
#define AdsGetSearchPath oadsimpl_AdsGetSearchPath
extern UNSIGNED32 ENTRYPOINT AdsGetSearchPath(UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetSearchPath
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetSearchPath=_AdsGetSearchPath")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetSearchPath(UNSIGNED8* a0, UNSIGNED16* a1) {
    return oadsimpl_AdsGetSearchPath(a0, a1);
}

/* ---- AdsGetServerName ---- */
#define AdsGetServerName oadsimpl_AdsGetServerName
extern UNSIGNED32 ENTRYPOINT AdsGetServerName(ADSHANDLE hConnect, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetServerName
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetServerName=_AdsGetServerName")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetServerName(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsGetServerName(a0, a1, a2);
}

/* ---- AdsGetServerTime ---- */
#define AdsGetServerTime oadsimpl_AdsGetServerTime
extern UNSIGNED32 ENTRYPOINT AdsGetServerTime(ADSHANDLE hConnect, UNSIGNED8* pucDateBuf, UNSIGNED16* pusDateLen, SIGNED32* plTime, UNSIGNED8* pucTimeBuf, UNSIGNED16* pusTimeLen);
#undef AdsGetServerTime
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetServerTime=_AdsGetServerTime")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetServerTime(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2, SIGNED32* a3, UNSIGNED8* a4, UNSIGNED16* a5) {
    return oadsimpl_AdsGetServerTime(a0, a1, a2, a3, a4, a5);
}

/* ---- AdsGetString ---- */
#define AdsGetString oadsimpl_AdsGetString
extern UNSIGNED32 ENTRYPOINT AdsGetString(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED8* pucBuf, UNSIGNED32* pulLen, UNSIGNED16 usOption);
#undef AdsGetString
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetString=_AdsGetString")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetString(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32* a3, UNSIGNED16 a4) {
    return oadsimpl_AdsGetString(a0, a1, a2, a3, a4);
}

/* ---- AdsGetStringW ---- */
#define AdsGetStringW oadsimpl_AdsGetStringW
extern UNSIGNED32 ENTRYPOINT AdsGetStringW(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pucBufW, UNSIGNED32* pulLenW, UNSIGNED16 usOption);
#undef AdsGetStringW
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetStringW=_AdsGetStringW")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetStringW(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2, UNSIGNED32* a3, UNSIGNED16 a4) {
    return oadsimpl_AdsGetStringW(a0, a1, a2, a3, a4);
}

/* ---- AdsGetTableAlias ---- */
#define AdsGetTableAlias oadsimpl_AdsGetTableAlias
extern UNSIGNED32 ENTRYPOINT AdsGetTableAlias(ADSHANDLE hTable, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetTableAlias
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableAlias=_AdsGetTableAlias")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableAlias(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsGetTableAlias(a0, a1, a2);
}

/* ---- AdsGetTableCharType ---- */
#define AdsGetTableCharType oadsimpl_AdsGetTableCharType
extern UNSIGNED32 ENTRYPOINT AdsGetTableCharType(ADSHANDLE hTable, UNSIGNED16* pusType);
#undef AdsGetTableCharType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableCharType=_AdsGetTableCharType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableCharType(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsGetTableCharType(a0, a1);
}

/* ---- AdsGetTableConType ---- */
#define AdsGetTableConType oadsimpl_AdsGetTableConType
extern UNSIGNED32 ENTRYPOINT AdsGetTableConType(ADSHANDLE hTable, UNSIGNED16* pusType);
#undef AdsGetTableConType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableConType=_AdsGetTableConType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableConType(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsGetTableConType(a0, a1);
}

/* ---- AdsGetTableConnection ---- */
#define AdsGetTableConnection oadsimpl_AdsGetTableConnection
extern UNSIGNED32 ENTRYPOINT AdsGetTableConnection(ADSHANDLE hTable, ADSHANDLE* phConnect);
#undef AdsGetTableConnection
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableConnection=_AdsGetTableConnection")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableConnection(ADSHANDLE a0, ADSHANDLE* a1) {
    return oadsimpl_AdsGetTableConnection(a0, a1);
}

/* ---- AdsGetTableFilename ---- */
#define AdsGetTableFilename oadsimpl_AdsGetTableFilename
extern UNSIGNED32 ENTRYPOINT AdsGetTableFilename(ADSHANDLE hTable, UNSIGNED16 usOption, UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
#undef AdsGetTableFilename
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableFilename=_AdsGetTableFilename")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableFilename(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED8* a2, UNSIGNED16* a3) {
    return oadsimpl_AdsGetTableFilename(a0, a1, a2, a3);
}

/* ---- AdsGetTableHandle25 ---- */
#define AdsGetTableHandle25 oadsimpl_AdsGetTableHandle25
extern UNSIGNED32 ENTRYPOINT AdsGetTableHandle25(ADSHANDLE hConnect, UNSIGNED8* pucName, ADSHANDLE* phTable);
#undef AdsGetTableHandle25
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableHandle25=_AdsGetTableHandle25")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableHandle25(ADSHANDLE a0, UNSIGNED8* a1, ADSHANDLE* a2) {
    return oadsimpl_AdsGetTableHandle25(a0, a1, a2);
}

/* ---- AdsGetTableLockType ---- */
#define AdsGetTableLockType oadsimpl_AdsGetTableLockType
extern UNSIGNED32 ENTRYPOINT AdsGetTableLockType(ADSHANDLE hTable, UNSIGNED16* pusLockType);
#undef AdsGetTableLockType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableLockType=_AdsGetTableLockType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableLockType(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsGetTableLockType(a0, a1);
}

/* ---- AdsGetTableOpenOptions ---- */
#define AdsGetTableOpenOptions oadsimpl_AdsGetTableOpenOptions
extern UNSIGNED32 ENTRYPOINT AdsGetTableOpenOptions(ADSHANDLE hTable, UNSIGNED32* pulOptions);
#undef AdsGetTableOpenOptions
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableOpenOptions=_AdsGetTableOpenOptions")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableOpenOptions(ADSHANDLE a0, UNSIGNED32* a1) {
    return oadsimpl_AdsGetTableOpenOptions(a0, a1);
}

/* ---- AdsGetTableType ---- */
#define AdsGetTableType oadsimpl_AdsGetTableType
extern UNSIGNED32 ENTRYPOINT AdsGetTableType(ADSHANDLE hTable, UNSIGNED16* pusType);
#undef AdsGetTableType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetTableType=_AdsGetTableType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetTableType(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsGetTableType(a0, a1);
}

/* ---- AdsGetVersion ---- */
#define AdsGetVersion oadsimpl_AdsGetVersion
extern UNSIGNED32 ENTRYPOINT AdsGetVersion(UNSIGNED32* pulMajor, UNSIGNED32* pulMinor, UNSIGNED8* pucLetter, UNSIGNED8* pucDesc, UNSIGNED16* pusDescLen);
#undef AdsGetVersion
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGetVersion=_AdsGetVersion")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGetVersion(UNSIGNED32* a0, UNSIGNED32* a1, UNSIGNED8* a2, UNSIGNED8* a3, UNSIGNED16* a4) {
    return oadsimpl_AdsGetVersion(a0, a1, a2, a3, a4);
}

/* ---- AdsGotoBookmark60 ---- */
#define AdsGotoBookmark60 oadsimpl_AdsGotoBookmark60
extern UNSIGNED32 ENTRYPOINT AdsGotoBookmark60(ADSHANDLE hObj, UNSIGNED8* pucBookmark, UNSIGNED32 ulLength);
#undef AdsGotoBookmark60
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGotoBookmark60=_AdsGotoBookmark60")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGotoBookmark60(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32 a2) {
    return oadsimpl_AdsGotoBookmark60(a0, a1, a2);
}

/* ---- AdsGotoBottom ---- */
#define AdsGotoBottom oadsimpl_AdsGotoBottom
extern UNSIGNED32 ENTRYPOINT AdsGotoBottom(ADSHANDLE hTable);
#undef AdsGotoBottom
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGotoBottom=_AdsGotoBottom")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGotoBottom(ADSHANDLE a0) {
    return oadsimpl_AdsGotoBottom(a0);
}

/* ---- AdsGotoRecord ---- */
#define AdsGotoRecord oadsimpl_AdsGotoRecord
extern UNSIGNED32 ENTRYPOINT AdsGotoRecord(ADSHANDLE hTable, UNSIGNED32 ulRecord);
#undef AdsGotoRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGotoRecord=_AdsGotoRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGotoRecord(ADSHANDLE a0, UNSIGNED32 a1) {
    return oadsimpl_AdsGotoRecord(a0, a1);
}

/* ---- AdsGotoTop ---- */
#define AdsGotoTop oadsimpl_AdsGotoTop
extern UNSIGNED32 ENTRYPOINT AdsGotoTop(ADSHANDLE hTable);
#undef AdsGotoTop
#pragma comment(linker, "/alternatename:_oadsimpl_AdsGotoTop=_AdsGotoTop")
__declspec(dllexport) UNSIGNED32 __stdcall AdsGotoTop(ADSHANDLE a0) {
    return oadsimpl_AdsGotoTop(a0);
}

/* ---- AdsInTransaction ---- */
#define AdsInTransaction oadsimpl_AdsInTransaction
extern UNSIGNED32 ENTRYPOINT AdsInTransaction(ADSHANDLE hConnect, UNSIGNED16* pbInTx);
#undef AdsInTransaction
#pragma comment(linker, "/alternatename:_oadsimpl_AdsInTransaction=_AdsInTransaction")
__declspec(dllexport) UNSIGNED32 __stdcall AdsInTransaction(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsInTransaction(a0, a1);
}

/* ---- AdsInitRawKey ---- */
#define AdsInitRawKey oadsimpl_AdsInitRawKey
extern UNSIGNED32 ENTRYPOINT AdsInitRawKey(ADSHANDLE hIndex);
#undef AdsInitRawKey
#pragma comment(linker, "/alternatename:_oadsimpl_AdsInitRawKey=_AdsInitRawKey")
__declspec(dllexport) UNSIGNED32 __stdcall AdsInitRawKey(ADSHANDLE a0) {
    return oadsimpl_AdsInitRawKey(a0);
}

/* ---- AdsIsConnectionAlive ---- */
#define AdsIsConnectionAlive oadsimpl_AdsIsConnectionAlive
extern UNSIGNED32 ENTRYPOINT AdsIsConnectionAlive(ADSHANDLE hConnect, UNSIGNED16* pbAlive);
#undef AdsIsConnectionAlive
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsConnectionAlive=_AdsIsConnectionAlive")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsConnectionAlive(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsIsConnectionAlive(a0, a1);
}

/* ---- AdsIsEmpty ---- */
#define AdsIsEmpty oadsimpl_AdsIsEmpty
extern UNSIGNED32 ENTRYPOINT AdsIsEmpty(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pbEmpty);
#undef AdsIsEmpty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsEmpty=_AdsIsEmpty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsEmpty(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsIsEmpty(a0, a1, a2);
}

/* ---- AdsIsEncryptionEnabled ---- */
#define AdsIsEncryptionEnabled oadsimpl_AdsIsEncryptionEnabled
extern UNSIGNED32 ENTRYPOINT AdsIsEncryptionEnabled(ADSHANDLE hConnect, UNSIGNED16* pbEnabled);
#undef AdsIsEncryptionEnabled
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsEncryptionEnabled=_AdsIsEncryptionEnabled")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsEncryptionEnabled(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsIsEncryptionEnabled(a0, a1);
}

/* ---- AdsIsExprValid ---- */
#define AdsIsExprValid oadsimpl_AdsIsExprValid
extern UNSIGNED32 ENTRYPOINT AdsIsExprValid(ADSHANDLE hTable, UNSIGNED8* pucExpr, UNSIGNED16* pbValid);
#undef AdsIsExprValid
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsExprValid=_AdsIsExprValid")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsExprValid(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsIsExprValid(a0, a1, a2);
}

/* ---- AdsIsFound ---- */
#define AdsIsFound oadsimpl_AdsIsFound
extern UNSIGNED32 ENTRYPOINT AdsIsFound(ADSHANDLE hTable, UNSIGNED16* pbFound);
#undef AdsIsFound
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsFound=_AdsIsFound")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsFound(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsIsFound(a0, a1);
}

/* ---- AdsIsIndexCustom ---- */
#define AdsIsIndexCustom oadsimpl_AdsIsIndexCustom
extern UNSIGNED32 ENTRYPOINT AdsIsIndexCustom(ADSHANDLE hIndex, UNSIGNED16* pbCustom);
#undef AdsIsIndexCustom
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsIndexCustom=_AdsIsIndexCustom")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsIndexCustom(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsIsIndexCustom(a0, a1);
}

/* ---- AdsIsIndexDescending ---- */
#define AdsIsIndexDescending oadsimpl_AdsIsIndexDescending
extern UNSIGNED32 ENTRYPOINT AdsIsIndexDescending(ADSHANDLE hIndex, UNSIGNED16* pbDesc);
#undef AdsIsIndexDescending
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsIndexDescending=_AdsIsIndexDescending")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsIndexDescending(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsIsIndexDescending(a0, a1);
}

/* ---- AdsIsIndexUnique ---- */
#define AdsIsIndexUnique oadsimpl_AdsIsIndexUnique
extern UNSIGNED32 ENTRYPOINT AdsIsIndexUnique(ADSHANDLE hIndex, UNSIGNED16* pbUnique);
#undef AdsIsIndexUnique
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsIndexUnique=_AdsIsIndexUnique")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsIndexUnique(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsIsIndexUnique(a0, a1);
}

/* ---- AdsIsNull ---- */
#define AdsIsNull oadsimpl_AdsIsNull
extern UNSIGNED32 ENTRYPOINT AdsIsNull(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pbNull);
#undef AdsIsNull
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsNull=_AdsIsNull")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsNull(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsIsNull(a0, a1, a2);
}

/* ---- AdsIsNullable ---- */
#define AdsIsNullable oadsimpl_AdsIsNullable
extern UNSIGNED32 ENTRYPOINT AdsIsNullable(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pbNullable);
#undef AdsIsNullable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsNullable=_AdsIsNullable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsNullable(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsIsNullable(a0, a1, a2);
}

/* ---- AdsIsRecordDeleted ---- */
#define AdsIsRecordDeleted oadsimpl_AdsIsRecordDeleted
extern UNSIGNED32 ENTRYPOINT AdsIsRecordDeleted(ADSHANDLE hTable, UNSIGNED16* pbDeleted);
#undef AdsIsRecordDeleted
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsRecordDeleted=_AdsIsRecordDeleted")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsRecordDeleted(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsIsRecordDeleted(a0, a1);
}

/* ---- AdsIsRecordEncrypted ---- */
#define AdsIsRecordEncrypted oadsimpl_AdsIsRecordEncrypted
extern UNSIGNED32 ENTRYPOINT AdsIsRecordEncrypted(ADSHANDLE hTable, UNSIGNED16* pbEncrypted);
#undef AdsIsRecordEncrypted
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsRecordEncrypted=_AdsIsRecordEncrypted")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsRecordEncrypted(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsIsRecordEncrypted(a0, a1);
}

/* ---- AdsIsRecordInAOF ---- */
#define AdsIsRecordInAOF oadsimpl_AdsIsRecordInAOF
extern UNSIGNED32 ENTRYPOINT AdsIsRecordInAOF(ADSHANDLE hTable, UNSIGNED32 ulRecord, UNSIGNED16* pbInAOF);
#undef AdsIsRecordInAOF
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsRecordInAOF=_AdsIsRecordInAOF")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsRecordInAOF(ADSHANDLE a0, UNSIGNED32 a1, UNSIGNED16* a2) {
    return oadsimpl_AdsIsRecordInAOF(a0, a1, a2);
}

/* ---- AdsIsRecordLocked ---- */
#define AdsIsRecordLocked oadsimpl_AdsIsRecordLocked
extern UNSIGNED32 ENTRYPOINT AdsIsRecordLocked(ADSHANDLE hTable, UNSIGNED32 ulRecord, UNSIGNED16* pbLocked);
#undef AdsIsRecordLocked
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsRecordLocked=_AdsIsRecordLocked")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsRecordLocked(ADSHANDLE a0, UNSIGNED32 a1, UNSIGNED16* a2) {
    return oadsimpl_AdsIsRecordLocked(a0, a1, a2);
}

/* ---- AdsIsRecordVisible ---- */
#define AdsIsRecordVisible oadsimpl_AdsIsRecordVisible
extern UNSIGNED32 ENTRYPOINT AdsIsRecordVisible(ADSHANDLE hObj, UNSIGNED16* pbVisible);
#undef AdsIsRecordVisible
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsRecordVisible=_AdsIsRecordVisible")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsRecordVisible(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsIsRecordVisible(a0, a1);
}

/* ---- AdsIsServerLoaded ---- */
#define AdsIsServerLoaded oadsimpl_AdsIsServerLoaded
extern UNSIGNED32 ENTRYPOINT AdsIsServerLoaded(UNSIGNED8* pucServer, UNSIGNED16* pbLoaded);
#undef AdsIsServerLoaded
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsServerLoaded=_AdsIsServerLoaded")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsServerLoaded(UNSIGNED8* a0, UNSIGNED16* a1) {
    return oadsimpl_AdsIsServerLoaded(a0, a1);
}

/* ---- AdsIsTableEncrypted ---- */
#define AdsIsTableEncrypted oadsimpl_AdsIsTableEncrypted
extern UNSIGNED32 ENTRYPOINT AdsIsTableEncrypted(ADSHANDLE hTable, UNSIGNED16* pbEncrypted);
#undef AdsIsTableEncrypted
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsTableEncrypted=_AdsIsTableEncrypted")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsTableEncrypted(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsIsTableEncrypted(a0, a1);
}

/* ---- AdsIsTableLocked ---- */
#define AdsIsTableLocked oadsimpl_AdsIsTableLocked
extern UNSIGNED32 ENTRYPOINT AdsIsTableLocked(ADSHANDLE hTable, UNSIGNED16* pbLocked);
#undef AdsIsTableLocked
#pragma comment(linker, "/alternatename:_oadsimpl_AdsIsTableLocked=_AdsIsTableLocked")
__declspec(dllexport) UNSIGNED32 __stdcall AdsIsTableLocked(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsIsTableLocked(a0, a1);
}

/* ---- AdsLockRecord ---- */
#define AdsLockRecord oadsimpl_AdsLockRecord
extern UNSIGNED32 ENTRYPOINT AdsLockRecord(ADSHANDLE hTable, UNSIGNED32 ulRecord);
#undef AdsLockRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsLockRecord=_AdsLockRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsLockRecord(ADSHANDLE a0, UNSIGNED32 a1) {
    return oadsimpl_AdsLockRecord(a0, a1);
}

/* ---- AdsLockTable ---- */
#define AdsLockTable oadsimpl_AdsLockTable
extern UNSIGNED32 ENTRYPOINT AdsLockTable(ADSHANDLE hTable);
#undef AdsLockTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsLockTable=_AdsLockTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsLockTable(ADSHANDLE a0) {
    return oadsimpl_AdsLockTable(a0);
}

/* ---- AdsMgConnect ---- */
#define AdsMgConnect oadsimpl_AdsMgConnect
extern UNSIGNED32 ENTRYPOINT AdsMgConnect(UNSIGNED8* pucServer, UNSIGNED8* pucUser, UNSIGNED8* pucPwd, ADSHANDLE* phMg);
#undef AdsMgConnect
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgConnect=_AdsMgConnect")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgConnect(UNSIGNED8* a0, UNSIGNED8* a1, UNSIGNED8* a2, ADSHANDLE* a3) {
    return oadsimpl_AdsMgConnect(a0, a1, a2, a3);
}

/* ---- AdsMgDisconnect ---- */
#define AdsMgDisconnect oadsimpl_AdsMgDisconnect
extern UNSIGNED32 ENTRYPOINT AdsMgDisconnect(ADSHANDLE hMg);
#undef AdsMgDisconnect
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgDisconnect=_AdsMgDisconnect")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgDisconnect(ADSHANDLE a0) {
    return oadsimpl_AdsMgDisconnect(a0);
}

/* ---- AdsMgDumpInternalTables ---- */
#define AdsMgDumpInternalTables oadsimpl_AdsMgDumpInternalTables
extern UNSIGNED32 ENTRYPOINT AdsMgDumpInternalTables(ADSHANDLE hMgmtHandle);
#undef AdsMgDumpInternalTables
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgDumpInternalTables=_AdsMgDumpInternalTables")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgDumpInternalTables(ADSHANDLE a0) {
    return oadsimpl_AdsMgDumpInternalTables(a0);
}

/* ---- AdsMgGetActivityInfo ---- */
#define AdsMgGetActivityInfo oadsimpl_AdsMgGetActivityInfo
extern UNSIGNED32 ENTRYPOINT AdsMgGetActivityInfo(ADSHANDLE hMg, void* pInfo, UNSIGNED16* pusSize);
#undef AdsMgGetActivityInfo
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetActivityInfo=_AdsMgGetActivityInfo")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetActivityInfo(ADSHANDLE a0, void* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsMgGetActivityInfo(a0, a1, a2);
}

/* ---- AdsMgGetCommStats ---- */
#define AdsMgGetCommStats oadsimpl_AdsMgGetCommStats
extern UNSIGNED32 ENTRYPOINT AdsMgGetCommStats(ADSHANDLE hMg, void* pInfo, UNSIGNED16* pusSize);
#undef AdsMgGetCommStats
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetCommStats=_AdsMgGetCommStats")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetCommStats(ADSHANDLE a0, void* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsMgGetCommStats(a0, a1, a2);
}

/* ---- AdsMgGetConfigInfo ---- */
#define AdsMgGetConfigInfo oadsimpl_AdsMgGetConfigInfo
extern UNSIGNED32 ENTRYPOINT AdsMgGetConfigInfo(ADSHANDLE hMg, void* pVals, UNSIGNED16* pusValsSize, void* pMem, UNSIGNED16* pusMemSize);
#undef AdsMgGetConfigInfo
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetConfigInfo=_AdsMgGetConfigInfo")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetConfigInfo(ADSHANDLE a0, void* a1, UNSIGNED16* a2, void* a3, UNSIGNED16* a4) {
    return oadsimpl_AdsMgGetConfigInfo(a0, a1, a2, a3, a4);
}

/* ---- AdsMgGetInstallInfo ---- */
#define AdsMgGetInstallInfo oadsimpl_AdsMgGetInstallInfo
extern UNSIGNED32 ENTRYPOINT AdsMgGetInstallInfo(ADSHANDLE hMg, void* pInfo, UNSIGNED16* pusSize);
#undef AdsMgGetInstallInfo
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetInstallInfo=_AdsMgGetInstallInfo")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetInstallInfo(ADSHANDLE a0, void* a1, UNSIGNED16* a2) {
    return oadsimpl_AdsMgGetInstallInfo(a0, a1, a2);
}

/* ---- AdsMgGetLockOwner ---- */
#define AdsMgGetLockOwner oadsimpl_AdsMgGetLockOwner
extern UNSIGNED32 ENTRYPOINT AdsMgGetLockOwner(ADSHANDLE hMg, UNSIGNED8* pucTable, UNSIGNED32 ulRecord, void* pInfo, UNSIGNED16* pusSize, UNSIGNED16* pusLockType);
#undef AdsMgGetLockOwner
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetLockOwner=_AdsMgGetLockOwner")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetLockOwner(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32 a2, void* a3, UNSIGNED16* a4, UNSIGNED16* a5) {
    return oadsimpl_AdsMgGetLockOwner(a0, a1, a2, a3, a4, a5);
}

/* ---- AdsMgGetLocks ---- */
#define AdsMgGetLocks oadsimpl_AdsMgGetLocks
extern UNSIGNED32 ENTRYPOINT AdsMgGetLocks(ADSHANDLE hMg, UNSIGNED8* pucTable, UNSIGNED8* pucUser, UNSIGNED16 usConnNumber, void* pInfo, UNSIGNED16* pusCount, UNSIGNED16* pusSize);
#undef AdsMgGetLocks
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetLocks=_AdsMgGetLocks")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetLocks(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, void* a4, UNSIGNED16* a5, UNSIGNED16* a6) {
    return oadsimpl_AdsMgGetLocks(a0, a1, a2, a3, a4, a5, a6);
}

/* ---- AdsMgGetOpenIndexes ---- */
#define AdsMgGetOpenIndexes oadsimpl_AdsMgGetOpenIndexes
extern UNSIGNED32 ENTRYPOINT AdsMgGetOpenIndexes(ADSHANDLE hMg, UNSIGNED8* pucTable, UNSIGNED8* pucUser, UNSIGNED16 usConnNumber, void* pInfo, UNSIGNED16* pusCount, UNSIGNED16* pusSize);
#undef AdsMgGetOpenIndexes
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetOpenIndexes=_AdsMgGetOpenIndexes")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetOpenIndexes(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, void* a4, UNSIGNED16* a5, UNSIGNED16* a6) {
    return oadsimpl_AdsMgGetOpenIndexes(a0, a1, a2, a3, a4, a5, a6);
}

/* ---- AdsMgGetOpenTables ---- */
#define AdsMgGetOpenTables oadsimpl_AdsMgGetOpenTables
extern UNSIGNED32 ENTRYPOINT AdsMgGetOpenTables(ADSHANDLE hMg, UNSIGNED8* pucUser, UNSIGNED16 usConnNumber, void* pInfo, UNSIGNED16* pusCount, UNSIGNED16* pusSize);
#undef AdsMgGetOpenTables
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetOpenTables=_AdsMgGetOpenTables")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetOpenTables(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16* a4, UNSIGNED16* a5) {
    return oadsimpl_AdsMgGetOpenTables(a0, a1, a2, a3, a4, a5);
}

/* ---- AdsMgGetOpenTables2 ---- */
#define AdsMgGetOpenTables2 oadsimpl_AdsMgGetOpenTables2
extern UNSIGNED32 ENTRYPOINT AdsMgGetOpenTables2(ADSHANDLE hMg, UNSIGNED8* pucUser, UNSIGNED16 usConnNumber, void* pInfo, UNSIGNED16* pusCount, UNSIGNED16* pusSize);
#undef AdsMgGetOpenTables2
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetOpenTables2=_AdsMgGetOpenTables2")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetOpenTables2(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, void* a3, UNSIGNED16* a4, UNSIGNED16* a5) {
    return oadsimpl_AdsMgGetOpenTables2(a0, a1, a2, a3, a4, a5);
}

/* ---- AdsMgGetServerType ---- */
#define AdsMgGetServerType oadsimpl_AdsMgGetServerType
extern UNSIGNED32 ENTRYPOINT AdsMgGetServerType(ADSHANDLE hMg, UNSIGNED16* pusT);
#undef AdsMgGetServerType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetServerType=_AdsMgGetServerType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetServerType(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsMgGetServerType(a0, a1);
}

/* ---- AdsMgGetThreadSql ---- */
#define AdsMgGetThreadSql oadsimpl_AdsMgGetThreadSql
extern UNSIGNED32 ENTRYPOINT AdsMgGetThreadSql(ADSHANDLE hMg, UNSIGNED32 ulThreadNumber, UNSIGNED8* pucBuf, UNSIGNED32* pulLen, UNSIGNED64* pullStartEpoch);
#undef AdsMgGetThreadSql
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetThreadSql=_AdsMgGetThreadSql")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetThreadSql(ADSHANDLE a0, UNSIGNED32 a1, UNSIGNED8* a2, UNSIGNED32* a3, UNSIGNED64* a4) {
    return oadsimpl_AdsMgGetThreadSql(a0, a1, a2, a3, a4);
}

/* ---- AdsMgGetUserAvgCost ---- */
#define AdsMgGetUserAvgCost oadsimpl_AdsMgGetUserAvgCost
extern UNSIGNED32 ENTRYPOINT AdsMgGetUserAvgCost(ADSHANDLE hMg, UNSIGNED32* pulCosts, UNSIGNED16* pusCount, UNSIGNED16* pusSize);
#undef AdsMgGetUserAvgCost
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetUserAvgCost=_AdsMgGetUserAvgCost")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetUserAvgCost(ADSHANDLE a0, UNSIGNED32* a1, UNSIGNED16* a2, UNSIGNED16* a3) {
    return oadsimpl_AdsMgGetUserAvgCost(a0, a1, a2, a3);
}

/* ---- AdsMgGetUserNames ---- */
#define AdsMgGetUserNames oadsimpl_AdsMgGetUserNames
extern UNSIGNED32 ENTRYPOINT AdsMgGetUserNames(ADSHANDLE hMg, UNSIGNED8* pucFile, void* pInfo, UNSIGNED16* pusCount, UNSIGNED16* pusSize);
#undef AdsMgGetUserNames
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetUserNames=_AdsMgGetUserNames")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetUserNames(ADSHANDLE a0, UNSIGNED8* a1, void* a2, UNSIGNED16* a3, UNSIGNED16* a4) {
    return oadsimpl_AdsMgGetUserNames(a0, a1, a2, a3, a4);
}

/* ---- AdsMgGetWorkerThreadActivity ---- */
#define AdsMgGetWorkerThreadActivity oadsimpl_AdsMgGetWorkerThreadActivity
extern UNSIGNED32 ENTRYPOINT AdsMgGetWorkerThreadActivity(ADSHANDLE hMg, void* pInfo, UNSIGNED16* pusCount, UNSIGNED16* pusSize);
#undef AdsMgGetWorkerThreadActivity
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgGetWorkerThreadActivity=_AdsMgGetWorkerThreadActivity")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgGetWorkerThreadActivity(ADSHANDLE a0, void* a1, UNSIGNED16* a2, UNSIGNED16* a3) {
    return oadsimpl_AdsMgGetWorkerThreadActivity(a0, a1, a2, a3);
}

/* ---- AdsMgKillUser ---- */
#define AdsMgKillUser oadsimpl_AdsMgKillUser
extern UNSIGNED32 ENTRYPOINT AdsMgKillUser(ADSHANDLE hMg, UNSIGNED8* pucUser, UNSIGNED16 usOption);
#undef AdsMgKillUser
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgKillUser=_AdsMgKillUser")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgKillUser(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2) {
    return oadsimpl_AdsMgKillUser(a0, a1, a2);
}

/* ---- AdsMgResetCommStats ---- */
#define AdsMgResetCommStats oadsimpl_AdsMgResetCommStats
extern UNSIGNED32 ENTRYPOINT AdsMgResetCommStats(ADSHANDLE hMg);
#undef AdsMgResetCommStats
#pragma comment(linker, "/alternatename:_oadsimpl_AdsMgResetCommStats=_AdsMgResetCommStats")
__declspec(dllexport) UNSIGNED32 __stdcall AdsMgResetCommStats(ADSHANDLE a0) {
    return oadsimpl_AdsMgResetCommStats(a0);
}

/* ---- AdsOpenIndex ---- */
#define AdsOpenIndex oadsimpl_AdsOpenIndex
extern UNSIGNED32 ENTRYPOINT AdsOpenIndex(ADSHANDLE hTable, UNSIGNED8* pucName, ADSHANDLE* ahIndex, UNSIGNED16* pu16ArrayLen);
#undef AdsOpenIndex
#pragma comment(linker, "/alternatename:_oadsimpl_AdsOpenIndex=_AdsOpenIndex")
__declspec(dllexport) UNSIGNED32 __stdcall AdsOpenIndex(ADSHANDLE a0, UNSIGNED8* a1, ADSHANDLE* a2, UNSIGNED16* a3) {
    return oadsimpl_AdsOpenIndex(a0, a1, a2, a3);
}

/* ---- AdsOpenTable ---- */
#define AdsOpenTable oadsimpl_AdsOpenTable
extern UNSIGNED32 ENTRYPOINT AdsOpenTable(ADSHANDLE hConnect, UNSIGNED8* pucName, UNSIGNED8* pucAlias, UNSIGNED16 usTableType, UNSIGNED16 usCharType, UNSIGNED16 usLockType, UNSIGNED16 usCheckRights, UNSIGNED16 usMode, ADSHANDLE* phTable);
#undef AdsOpenTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsOpenTable=_AdsOpenTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsOpenTable(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, UNSIGNED16 a4, UNSIGNED16 a5, UNSIGNED16 a6, UNSIGNED16 a7, ADSHANDLE* a8) {
    return oadsimpl_AdsOpenTable(a0, a1, a2, a3, a4, a5, a6, a7, a8);
}

/* ---- AdsPackTable ---- */
#define AdsPackTable oadsimpl_AdsPackTable
extern UNSIGNED32 ENTRYPOINT AdsPackTable(ADSHANDLE hTable);
#undef AdsPackTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsPackTable=_AdsPackTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsPackTable(ADSHANDLE a0) {
    return oadsimpl_AdsPackTable(a0);
}

/* ---- AdsPackTable120 ---- */
#define AdsPackTable120 oadsimpl_AdsPackTable120
extern UNSIGNED32 ENTRYPOINT AdsPackTable120(ADSHANDLE hTable, UNSIGNED32 ulMemoBlockSize, UNSIGNED32 ulOptions);
#undef AdsPackTable120
#pragma comment(linker, "/alternatename:_oadsimpl_AdsPackTable120=_AdsPackTable120")
__declspec(dllexport) UNSIGNED32 __stdcall AdsPackTable120(ADSHANDLE a0, UNSIGNED32 a1, UNSIGNED32 a2) {
    return oadsimpl_AdsPackTable120(a0, a1, a2);
}

/* ---- AdsPrepareSQL ---- */
#define AdsPrepareSQL oadsimpl_AdsPrepareSQL
extern UNSIGNED32 ENTRYPOINT AdsPrepareSQL(ADSHANDLE hStatement, UNSIGNED8* pucSQL);
#undef AdsPrepareSQL
#pragma comment(linker, "/alternatename:_oadsimpl_AdsPrepareSQL=_AdsPrepareSQL")
__declspec(dllexport) UNSIGNED32 __stdcall AdsPrepareSQL(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsPrepareSQL(a0, a1);
}

/* ---- AdsPrepareSQLW ---- */
#define AdsPrepareSQLW oadsimpl_AdsPrepareSQLW
extern UNSIGNED32 ENTRYPOINT AdsPrepareSQLW(ADSHANDLE hStatement, UNSIGNED16* pwcSQL);
#undef AdsPrepareSQLW
#pragma comment(linker, "/alternatename:_oadsimpl_AdsPrepareSQLW=_AdsPrepareSQLW")
__declspec(dllexport) UNSIGNED32 __stdcall AdsPrepareSQLW(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsPrepareSQLW(a0, a1);
}

/* ---- AdsRecallAllRecords ---- */
#define AdsRecallAllRecords oadsimpl_AdsRecallAllRecords
extern UNSIGNED32 ENTRYPOINT AdsRecallAllRecords(ADSHANDLE hTable);
#undef AdsRecallAllRecords
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRecallAllRecords=_AdsRecallAllRecords")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRecallAllRecords(ADSHANDLE a0) {
    return oadsimpl_AdsRecallAllRecords(a0);
}

/* ---- AdsRecallRecord ---- */
#define AdsRecallRecord oadsimpl_AdsRecallRecord
extern UNSIGNED32 ENTRYPOINT AdsRecallRecord(ADSHANDLE hTable);
#undef AdsRecallRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRecallRecord=_AdsRecallRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRecallRecord(ADSHANDLE a0) {
    return oadsimpl_AdsRecallRecord(a0);
}

/* ---- AdsRefreshAOF ---- */
#define AdsRefreshAOF oadsimpl_AdsRefreshAOF
extern UNSIGNED32 ENTRYPOINT AdsRefreshAOF(ADSHANDLE hTable);
#undef AdsRefreshAOF
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRefreshAOF=_AdsRefreshAOF")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRefreshAOF(ADSHANDLE a0) {
    return oadsimpl_AdsRefreshAOF(a0);
}

/* ---- AdsRefreshRecord ---- */
#define AdsRefreshRecord oadsimpl_AdsRefreshRecord
extern UNSIGNED32 ENTRYPOINT AdsRefreshRecord(ADSHANDLE hTable);
#undef AdsRefreshRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRefreshRecord=_AdsRefreshRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRefreshRecord(ADSHANDLE a0) {
    return oadsimpl_AdsRefreshRecord(a0);
}

/* ---- AdsRegisterCallbackFunction ---- */
#define AdsRegisterCallbackFunction oadsimpl_AdsRegisterCallbackFunction
extern UNSIGNED32 ENTRYPOINT AdsRegisterCallbackFunction(void* pCallback, ADSHANDLE hTable);
#undef AdsRegisterCallbackFunction
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRegisterCallbackFunction=_AdsRegisterCallbackFunction")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRegisterCallbackFunction(void* pCallback, ADSHANDLE hTable) {
    return oadsimpl_AdsRegisterCallbackFunction(pCallback, hTable);
}

/* ---- AdsRegisterProgressCallback ---- */
#define AdsRegisterProgressCallback oadsimpl_AdsRegisterProgressCallback
extern UNSIGNED32 ENTRYPOINT AdsRegisterProgressCallback(void* pCallback);
#undef AdsRegisterProgressCallback
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRegisterProgressCallback=_AdsRegisterProgressCallback")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRegisterProgressCallback(void* a0) {
    return oadsimpl_AdsRegisterProgressCallback(a0);
}

/* ---- AdsReindex ---- */
#define AdsReindex oadsimpl_AdsReindex
extern UNSIGNED32 ENTRYPOINT AdsReindex(ADSHANDLE hTable);
#undef AdsReindex
#pragma comment(linker, "/alternatename:_oadsimpl_AdsReindex=_AdsReindex")
__declspec(dllexport) UNSIGNED32 __stdcall AdsReindex(ADSHANDLE a0) {
    return oadsimpl_AdsReindex(a0);
}

/* ---- AdsReindex61 ---- */
#define AdsReindex61 oadsimpl_AdsReindex61
extern UNSIGNED32 ENTRYPOINT AdsReindex61(ADSHANDLE hObject, UNSIGNED32 ulPageSize);
#undef AdsReindex61
#pragma comment(linker, "/alternatename:_oadsimpl_AdsReindex61=_AdsReindex61")
__declspec(dllexport) UNSIGNED32 __stdcall AdsReindex61(ADSHANDLE a0, UNSIGNED32 a1) {
    return oadsimpl_AdsReindex61(a0, a1);
}

/* ---- AdsReleaseSavepoint ---- */
#define AdsReleaseSavepoint oadsimpl_AdsReleaseSavepoint
extern UNSIGNED32 ENTRYPOINT AdsReleaseSavepoint(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsReleaseSavepoint
#pragma comment(linker, "/alternatename:_oadsimpl_AdsReleaseSavepoint=_AdsReleaseSavepoint")
__declspec(dllexport) UNSIGNED32 __stdcall AdsReleaseSavepoint(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsReleaseSavepoint(a0, a1);
}

/* ---- AdsRenameFile ---- */
#define AdsRenameFile oadsimpl_AdsRenameFile
extern UNSIGNED32 ENTRYPOINT AdsRenameFile(ADSHANDLE hConnect, UNSIGNED8* pucOld, UNSIGNED8* pucNew);
#undef AdsRenameFile
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRenameFile=_AdsRenameFile")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRenameFile(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    return oadsimpl_AdsRenameFile(a0, a1, a2);
}

/* ---- AdsResetConnection ---- */
#define AdsResetConnection oadsimpl_AdsResetConnection
extern UNSIGNED32 ENTRYPOINT AdsResetConnection(ADSHANDLE hConnect);
#undef AdsResetConnection
#pragma comment(linker, "/alternatename:_oadsimpl_AdsResetConnection=_AdsResetConnection")
__declspec(dllexport) UNSIGNED32 __stdcall AdsResetConnection(ADSHANDLE a0) {
    return oadsimpl_AdsResetConnection(a0);
}

/* ---- AdsRestructureTable ---- */
#define AdsRestructureTable oadsimpl_AdsRestructureTable
extern UNSIGNED32 ENTRYPOINT AdsRestructureTable(ADSHANDLE hConnect, UNSIGNED8* pucTableName, UNSIGNED8* pucAlias, UNSIGNED16 usFileType, UNSIGNED16 usCharType, UNSIGNED16 usLockType, UNSIGNED16 usCheckRights, UNSIGNED8* pucAddFields, UNSIGNED8* pucDeleteFields, UNSIGNED8* pucChangeFields);
#undef AdsRestructureTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRestructureTable=_AdsRestructureTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRestructureTable(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3, UNSIGNED16 a4, UNSIGNED16 a5, UNSIGNED16 a6, UNSIGNED8* a7, UNSIGNED8* a8, UNSIGNED8* a9) {
    return oadsimpl_AdsRestructureTable(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9);
}

/* ---- AdsRollbackTransaction ---- */
#define AdsRollbackTransaction oadsimpl_AdsRollbackTransaction
extern UNSIGNED32 ENTRYPOINT AdsRollbackTransaction(ADSHANDLE hConnect);
#undef AdsRollbackTransaction
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRollbackTransaction=_AdsRollbackTransaction")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRollbackTransaction(ADSHANDLE a0) {
    return oadsimpl_AdsRollbackTransaction(a0);
}

/* ---- AdsRollbackTransaction80 ---- */
#define AdsRollbackTransaction80 oadsimpl_AdsRollbackTransaction80
extern UNSIGNED32 ENTRYPOINT AdsRollbackTransaction80(ADSHANDLE hConnect, UNSIGNED8* pucSavepoint, UNSIGNED32 ulOptions);
#undef AdsRollbackTransaction80
#pragma comment(linker, "/alternatename:_oadsimpl_AdsRollbackTransaction80=_AdsRollbackTransaction80")
__declspec(dllexport) UNSIGNED32 __stdcall AdsRollbackTransaction80(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32 a2) {
    return oadsimpl_AdsRollbackTransaction80(a0, a1, a2);
}

/* ---- AdsSeek ---- */
#define AdsSeek oadsimpl_AdsSeek
extern UNSIGNED32 ENTRYPOINT AdsSeek(ADSHANDLE hIndex, UNSIGNED8* pucKey, UNSIGNED16 usKeyLen, UNSIGNED16 usKeyType, UNSIGNED16 usSeekType, UNSIGNED16* pbFound);
#undef AdsSeek
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSeek=_AdsSeek")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSeek(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, UNSIGNED16 a3, UNSIGNED16 a4, UNSIGNED16* a5) {
    return oadsimpl_AdsSeek(a0, a1, a2, a3, a4, a5);
}

/* ---- AdsSeekLast ---- */
#define AdsSeekLast oadsimpl_AdsSeekLast
extern UNSIGNED32 ENTRYPOINT AdsSeekLast(ADSHANDLE hIndex, UNSIGNED8* pucKey, UNSIGNED16 usKeyLen, UNSIGNED16 usKeyType, UNSIGNED16* pbFound);
#undef AdsSeekLast
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSeekLast=_AdsSeekLast")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSeekLast(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, UNSIGNED16 a3, UNSIGNED16* a4) {
    return oadsimpl_AdsSeekLast(a0, a1, a2, a3, a4);
}

/* ---- AdsSetAOF ---- */
#define AdsSetAOF oadsimpl_AdsSetAOF
extern UNSIGNED32 ENTRYPOINT AdsSetAOF(ADSHANDLE hTable, UNSIGNED8* pucCondition, UNSIGNED16 usResolve);
#undef AdsSetAOF
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetAOF=_AdsSetAOF")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetAOF(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2) {
    return oadsimpl_AdsSetAOF(a0, a1, a2);
}

/* ---- AdsSetBinary ---- */
#define AdsSetBinary oadsimpl_AdsSetBinary
extern UNSIGNED32 ENTRYPOINT AdsSetBinary(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16 usBinaryType, UNSIGNED32 ulTotalBytes, UNSIGNED32 ulOffset, UNSIGNED8* pucBuf, UNSIGNED32 ulBytes);
#undef AdsSetBinary
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetBinary=_AdsSetBinary")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetBinary(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2, UNSIGNED32 a3, UNSIGNED32 a4, UNSIGNED8* a5, UNSIGNED32 a6) {
    return oadsimpl_AdsSetBinary(a0, a1, a2, a3, a4, a5, a6);
}

/* ---- AdsSetCollation ---- */
#define AdsSetCollation oadsimpl_AdsSetCollation
extern UNSIGNED32 ENTRYPOINT AdsSetCollation(ADSHANDLE hConnect, UNSIGNED8* pucName);
#undef AdsSetCollation
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetCollation=_AdsSetCollation")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetCollation(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsSetCollation(a0, a1);
}

/* ---- AdsSetDateFormat ---- */
#define AdsSetDateFormat oadsimpl_AdsSetDateFormat
extern UNSIGNED32 ENTRYPOINT AdsSetDateFormat(UNSIGNED8* pucFormat);
#undef AdsSetDateFormat
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetDateFormat=_AdsSetDateFormat")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetDateFormat(UNSIGNED8* a0) {
    return oadsimpl_AdsSetDateFormat(a0);
}

/* ---- AdsSetDateFormat60 ---- */
#define AdsSetDateFormat60 oadsimpl_AdsSetDateFormat60
extern UNSIGNED32 ENTRYPOINT AdsSetDateFormat60(ADSHANDLE hConnect, UNSIGNED8* pucFormat);
#undef AdsSetDateFormat60
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetDateFormat60=_AdsSetDateFormat60")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetDateFormat60(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsSetDateFormat60(a0, a1);
}

/* ---- AdsSetDecimals ---- */
#define AdsSetDecimals oadsimpl_AdsSetDecimals
extern UNSIGNED32 ENTRYPOINT AdsSetDecimals(UNSIGNED16 usDecimals);
#undef AdsSetDecimals
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetDecimals=_AdsSetDecimals")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetDecimals(UNSIGNED16 a0) {
    return oadsimpl_AdsSetDecimals(a0);
}

/* ---- AdsSetDefault ---- */
#define AdsSetDefault oadsimpl_AdsSetDefault
extern UNSIGNED32 ENTRYPOINT AdsSetDefault(UNSIGNED8* pucDir);
#undef AdsSetDefault
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetDefault=_AdsSetDefault")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetDefault(UNSIGNED8* a0) {
    return oadsimpl_AdsSetDefault(a0);
}

/* ---- AdsSetDeferredFlush ---- */
#define AdsSetDeferredFlush oadsimpl_AdsSetDeferredFlush
extern UNSIGNED32 ENTRYPOINT AdsSetDeferredFlush(ADSHANDLE hTable, UNSIGNED16 usDeferred);
#undef AdsSetDeferredFlush
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetDeferredFlush=_AdsSetDeferredFlush")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetDeferredFlush(ADSHANDLE a0, UNSIGNED16 a1) {
    return oadsimpl_AdsSetDeferredFlush(a0, a1);
}

/* ---- AdsSetDouble ---- */
#define AdsSetDouble oadsimpl_AdsSetDouble
extern UNSIGNED32 ENTRYPOINT AdsSetDouble(ADSHANDLE hTable, UNSIGNED8* pucField, double dValue);
#undef AdsSetDouble
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetDouble=_AdsSetDouble")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetDouble(ADSHANDLE a0, UNSIGNED8* a1, double a2) {
    return oadsimpl_AdsSetDouble(a0, a1, a2);
}

/* ---- AdsSetEmpty ---- */
#define AdsSetEmpty oadsimpl_AdsSetEmpty
extern UNSIGNED32 ENTRYPOINT AdsSetEmpty(ADSHANDLE hObj, UNSIGNED8* pucFldId);
#undef AdsSetEmpty
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetEmpty=_AdsSetEmpty")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetEmpty(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsSetEmpty(a0, a1);
}

/* ---- AdsSetEncryptionPassword ---- */
#define AdsSetEncryptionPassword oadsimpl_AdsSetEncryptionPassword
extern UNSIGNED32 ENTRYPOINT AdsSetEncryptionPassword(ADSHANDLE hConnect, UNSIGNED8* pucPassword);
#undef AdsSetEncryptionPassword
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetEncryptionPassword=_AdsSetEncryptionPassword")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetEncryptionPassword(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsSetEncryptionPassword(a0, a1);
}

/* ---- AdsSetEpoch ---- */
#define AdsSetEpoch oadsimpl_AdsSetEpoch
extern UNSIGNED32 ENTRYPOINT AdsSetEpoch(UNSIGNED16 usEpoch);
#undef AdsSetEpoch
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetEpoch=_AdsSetEpoch")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetEpoch(UNSIGNED16 a0) {
    return oadsimpl_AdsSetEpoch(a0);
}

/* ---- AdsSetExact ---- */
#define AdsSetExact oadsimpl_AdsSetExact
extern UNSIGNED32 ENTRYPOINT AdsSetExact(UNSIGNED16 bExact);
#undef AdsSetExact
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetExact=_AdsSetExact")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetExact(UNSIGNED16 a0) {
    return oadsimpl_AdsSetExact(a0);
}

/* ---- AdsSetField ---- */
#define AdsSetField oadsimpl_AdsSetField
extern UNSIGNED32 ENTRYPOINT AdsSetField(ADSHANDLE hObj, UNSIGNED8* pucFldId, UNSIGNED8* pucBuf, UNSIGNED32 ulLen);
#undef AdsSetField
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetField=_AdsSetField")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetField(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32 a3) {
    return oadsimpl_AdsSetField(a0, a1, a2, a3);
}

/* ---- AdsSetFieldRaw ---- */
#define AdsSetFieldRaw oadsimpl_AdsSetFieldRaw
extern UNSIGNED32 ENTRYPOINT AdsSetFieldRaw(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED8* pucBuf, UNSIGNED32 ulLen);
#undef AdsSetFieldRaw
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetFieldRaw=_AdsSetFieldRaw")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetFieldRaw(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32 a3) {
    return oadsimpl_AdsSetFieldRaw(a0, a1, a2, a3);
}

/* ---- AdsSetFilter ---- */
#define AdsSetFilter oadsimpl_AdsSetFilter
extern UNSIGNED32 ENTRYPOINT AdsSetFilter(ADSHANDLE hTable, UNSIGNED8* pucExpr);
#undef AdsSetFilter
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetFilter=_AdsSetFilter")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetFilter(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsSetFilter(a0, a1);
}

/* ---- AdsSetIndexDirection ---- */
#define AdsSetIndexDirection oadsimpl_AdsSetIndexDirection
extern UNSIGNED32 ENTRYPOINT AdsSetIndexDirection(ADSHANDLE hIndex, UNSIGNED16 usDir);
#undef AdsSetIndexDirection
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetIndexDirection=_AdsSetIndexDirection")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetIndexDirection(ADSHANDLE a0, UNSIGNED16 a1) {
    return oadsimpl_AdsSetIndexDirection(a0, a1);
}

/* ---- AdsSetIndexOrderByHandle ---- */
#define AdsSetIndexOrderByHandle oadsimpl_AdsSetIndexOrderByHandle
extern UNSIGNED32 ENTRYPOINT AdsSetIndexOrderByHandle(ADSHANDLE hTable, ADSHANDLE hIndex);
#undef AdsSetIndexOrderByHandle
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetIndexOrderByHandle=_AdsSetIndexOrderByHandle")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetIndexOrderByHandle(ADSHANDLE a0, ADSHANDLE a1) {
    return oadsimpl_AdsSetIndexOrderByHandle(a0, a1);
}

/* ---- AdsSetIndexOrder ---- */
#define AdsSetIndexOrder oadsimpl_AdsSetIndexOrder
extern UNSIGNED32 ENTRYPOINT AdsSetIndexOrder(ADSHANDLE hTable, UNSIGNED8* pucName);
#undef AdsSetIndexOrder
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetIndexOrder=_AdsSetIndexOrder")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetIndexOrder(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsSetIndexOrder(a0, a1);
}

/* ---- AdsSetJulian ---- */
#define AdsSetJulian oadsimpl_AdsSetJulian
extern UNSIGNED32 ENTRYPOINT AdsSetJulian(ADSHANDLE hTable, UNSIGNED8* pucField, SIGNED32 lJulian);
#undef AdsSetJulian
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetJulian=_AdsSetJulian")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetJulian(ADSHANDLE a0, UNSIGNED8* a1, SIGNED32 a2) {
    return oadsimpl_AdsSetJulian(a0, a1, a2);
}

/* ---- AdsSetLockCycle ---- */
#define AdsSetLockCycle oadsimpl_AdsSetLockCycle
extern UNSIGNED32 ENTRYPOINT AdsSetLockCycle(ADSHANDLE hConnect, UNSIGNED32 ulCycle);
#undef AdsSetLockCycle
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetLockCycle=_AdsSetLockCycle")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetLockCycle(ADSHANDLE a0, UNSIGNED32 a1) {
    return oadsimpl_AdsSetLockCycle(a0, a1);
}

/* ---- AdsSetLockRetryCount ---- */
#define AdsSetLockRetryCount oadsimpl_AdsSetLockRetryCount
extern UNSIGNED32 ENTRYPOINT AdsSetLockRetryCount(ADSHANDLE hConnect, UNSIGNED16 usRetryCount);
#undef AdsSetLockRetryCount
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetLockRetryCount=_AdsSetLockRetryCount")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetLockRetryCount(ADSHANDLE a0, UNSIGNED16 a1) {
    return oadsimpl_AdsSetLockRetryCount(a0, a1);
}

/* ---- AdsSetLogical ---- */
#define AdsSetLogical oadsimpl_AdsSetLogical
extern UNSIGNED32 ENTRYPOINT AdsSetLogical(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16 bValue);
#undef AdsSetLogical
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetLogical=_AdsSetLogical")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetLogical(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16 a2) {
    return oadsimpl_AdsSetLogical(a0, a1, a2);
}

/* ---- AdsSetLongLong ---- */
#define AdsSetLongLong oadsimpl_AdsSetLongLong
extern UNSIGNED32 ENTRYPOINT AdsSetLongLong(ADSHANDLE hTable, UNSIGNED8* pucField, int64_t llValue);
#undef AdsSetLongLong
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetLongLong=_AdsSetLongLong")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetLongLong(ADSHANDLE a0, UNSIGNED8* a1, int64_t a2) {
    return oadsimpl_AdsSetLongLong(a0, a1, a2);
}

/* ---- AdsSetMilliseconds ---- */
#define AdsSetMilliseconds oadsimpl_AdsSetMilliseconds
extern UNSIGNED32 ENTRYPOINT AdsSetMilliseconds(ADSHANDLE hTable, UNSIGNED8* pucField, SIGNED32 lMs);
#undef AdsSetMilliseconds
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetMilliseconds=_AdsSetMilliseconds")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetMilliseconds(ADSHANDLE a0, UNSIGNED8* a1, SIGNED32 a2) {
    return oadsimpl_AdsSetMilliseconds(a0, a1, a2);
}

/* ---- AdsSetMoney ---- */
#define AdsSetMoney oadsimpl_AdsSetMoney
extern UNSIGNED32 ENTRYPOINT AdsSetMoney(ADSHANDLE hObj, UNSIGNED8* pucFldId, SIGNED64 qValue);
#undef AdsSetMoney
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetMoney=_AdsSetMoney")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetMoney(ADSHANDLE a0, UNSIGNED8* a1, SIGNED64 a2) {
    return oadsimpl_AdsSetMoney(a0, a1, a2);
}

/* ---- AdsSetNull ---- */
#define AdsSetNull oadsimpl_AdsSetNull
extern UNSIGNED32 ENTRYPOINT AdsSetNull(ADSHANDLE hTable, UNSIGNED8* pucFldId);
#undef AdsSetNull
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetNull=_AdsSetNull")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetNull(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsSetNull(a0, a1);
}

/* ---- AdsSetRecord ---- */
#define AdsSetRecord oadsimpl_AdsSetRecord
extern UNSIGNED32 ENTRYPOINT AdsSetRecord(ADSHANDLE hTable, UNSIGNED8* pucBuf, UNSIGNED32 ulLen);
#undef AdsSetRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetRecord=_AdsSetRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetRecord(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED32 a2) {
    return oadsimpl_AdsSetRecord(a0, a1, a2);
}

/* ---- AdsSetRelKeyPos ---- */
#define AdsSetRelKeyPos oadsimpl_AdsSetRelKeyPos
extern UNSIGNED32 ENTRYPOINT AdsSetRelKeyPos(ADSHANDLE hIndex, double dPos);
#undef AdsSetRelKeyPos
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetRelKeyPos=_AdsSetRelKeyPos")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetRelKeyPos(ADSHANDLE a0, double a1) {
    return oadsimpl_AdsSetRelKeyPos(a0, a1);
}

/* ---- AdsSetRelation ---- */
#define AdsSetRelation oadsimpl_AdsSetRelation
extern UNSIGNED32 ENTRYPOINT AdsSetRelation(ADSHANDLE hParent, ADSHANDLE hChild, UNSIGNED8* pucExpr);
#undef AdsSetRelation
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetRelation=_AdsSetRelation")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetRelation(ADSHANDLE a0, ADSHANDLE a1, UNSIGNED8* a2) {
    return oadsimpl_AdsSetRelation(a0, a1, a2);
}

/* ---- AdsSetScope ---- */
#define AdsSetScope oadsimpl_AdsSetScope
extern UNSIGNED32 ENTRYPOINT AdsSetScope(ADSHANDLE hIndex, UNSIGNED16 usScope, UNSIGNED8* pucScope, UNSIGNED16 usLen, UNSIGNED16 usDataType);
#undef AdsSetScope
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetScope=_AdsSetScope")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetScope(ADSHANDLE a0, UNSIGNED16 a1, UNSIGNED8* a2, UNSIGNED16 a3, UNSIGNED16 a4) {
    return oadsimpl_AdsSetScope(a0, a1, a2, a3, a4);
}

/* ---- AdsSetScopedRelation ---- */
#define AdsSetScopedRelation oadsimpl_AdsSetScopedRelation
extern UNSIGNED32 ENTRYPOINT AdsSetScopedRelation(ADSHANDLE hParent, ADSHANDLE hChild, UNSIGNED8* pucExpr);
#undef AdsSetScopedRelation
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetScopedRelation=_AdsSetScopedRelation")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetScopedRelation(ADSHANDLE a0, ADSHANDLE a1, UNSIGNED8* a2) {
    return oadsimpl_AdsSetScopedRelation(a0, a1, a2);
}

/* ---- AdsSetSearchPath ---- */
#define AdsSetSearchPath oadsimpl_AdsSetSearchPath
extern UNSIGNED32 ENTRYPOINT AdsSetSearchPath(UNSIGNED8* pucPath);
#undef AdsSetSearchPath
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetSearchPath=_AdsSetSearchPath")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetSearchPath(UNSIGNED8* a0) {
    return oadsimpl_AdsSetSearchPath(a0);
}

/* ---- AdsSetServerType ---- */
#define AdsSetServerType oadsimpl_AdsSetServerType
extern UNSIGNED32 ENTRYPOINT AdsSetServerType(UNSIGNED16 usType);
#undef AdsSetServerType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetServerType=_AdsSetServerType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetServerType(UNSIGNED16 a0) {
    return oadsimpl_AdsSetServerType(a0);
}

/* ---- AdsSetShort ---- */
#define AdsSetShort oadsimpl_AdsSetShort
extern UNSIGNED32 ENTRYPOINT AdsSetShort(ADSHANDLE hObj, UNSIGNED8* pucFldId, SIGNED32 sValue);
#undef AdsSetShort
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetShort=_AdsSetShort")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetShort(ADSHANDLE a0, UNSIGNED8* a1, SIGNED32 a2) {
    return oadsimpl_AdsSetShort(a0, a1, a2);
}

/* ---- AdsSetString ---- */
#define AdsSetString oadsimpl_AdsSetString
extern UNSIGNED32 ENTRYPOINT AdsSetString(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED8* pucValue, UNSIGNED32 ulLen);
#undef AdsSetString
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetString=_AdsSetString")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetString(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32 a3) {
    return oadsimpl_AdsSetString(a0, a1, a2, a3);
}

/* ---- AdsSetStringW ---- */
#define AdsSetStringW oadsimpl_AdsSetStringW
extern UNSIGNED32 ENTRYPOINT AdsSetStringW(ADSHANDLE hTable, UNSIGNED8* pucField, UNSIGNED16* pucValueW, UNSIGNED32 ulLen);
#undef AdsSetStringW
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetStringW=_AdsSetStringW")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetStringW(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED16* a2, UNSIGNED32 a3) {
    return oadsimpl_AdsSetStringW(a0, a1, a2, a3);
}

/* ---- AdsSetTime ---- */
#define AdsSetTime oadsimpl_AdsSetTime
extern UNSIGNED32 ENTRYPOINT AdsSetTime(ADSHANDLE hObj, UNSIGNED8* pucFldId, UNSIGNED8* pucValue, UNSIGNED16 usLen);
#undef AdsSetTime
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetTime=_AdsSetTime")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetTime(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED16 a3) {
    return oadsimpl_AdsSetTime(a0, a1, a2, a3);
}

/* ---- AdsSetTimeStamp ---- */
#define AdsSetTimeStamp oadsimpl_AdsSetTimeStamp
extern UNSIGNED32 ENTRYPOINT AdsSetTimeStamp(ADSHANDLE hObj, UNSIGNED8* pucFldId, UNSIGNED8* pucBuf, UNSIGNED32 ulLen);
#undef AdsSetTimeStamp
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSetTimeStamp=_AdsSetTimeStamp")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSetTimeStamp(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2, UNSIGNED32 a3) {
    return oadsimpl_AdsSetTimeStamp(a0, a1, a2, a3);
}

/* ---- AdsShowDeleted ---- */
#define AdsShowDeleted oadsimpl_AdsShowDeleted
extern UNSIGNED32 ENTRYPOINT AdsShowDeleted(UNSIGNED16 bShow);
#undef AdsShowDeleted
#pragma comment(linker, "/alternatename:_oadsimpl_AdsShowDeleted=_AdsShowDeleted")
__declspec(dllexport) UNSIGNED32 __stdcall AdsShowDeleted(UNSIGNED16 a0) {
    return oadsimpl_AdsShowDeleted(a0);
}

/* ---- AdsShowError ---- */
#define AdsShowError oadsimpl_AdsShowError
extern UNSIGNED32 ENTRYPOINT AdsShowError(UNSIGNED8* pucCaption);
#undef AdsShowError
#pragma comment(linker, "/alternatename:_oadsimpl_AdsShowError=_AdsShowError")
__declspec(dllexport) UNSIGNED32 __stdcall AdsShowError(UNSIGNED8* a0) {
    return oadsimpl_AdsShowError(a0);
}

/* ---- AdsSkip ---- */
#define AdsSkip oadsimpl_AdsSkip
extern UNSIGNED32 ENTRYPOINT AdsSkip(ADSHANDLE hTable, SIGNED32 lRows);
#undef AdsSkip
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSkip=_AdsSkip")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSkip(ADSHANDLE a0, SIGNED32 a1) {
    return oadsimpl_AdsSkip(a0, a1);
}

/* ---- AdsSkipUnique ---- */
#define AdsSkipUnique oadsimpl_AdsSkipUnique
extern UNSIGNED32 ENTRYPOINT AdsSkipUnique(ADSHANDLE hIndex, SIGNED32 lDirection);
#undef AdsSkipUnique
#pragma comment(linker, "/alternatename:_oadsimpl_AdsSkipUnique=_AdsSkipUnique")
__declspec(dllexport) UNSIGNED32 __stdcall AdsSkipUnique(ADSHANDLE a0, SIGNED32 a1) {
    return oadsimpl_AdsSkipUnique(a0, a1);
}

/* ---- AdsStmtClearTablePasswords ---- */
#define AdsStmtClearTablePasswords oadsimpl_AdsStmtClearTablePasswords
extern UNSIGNED32 ENTRYPOINT AdsStmtClearTablePasswords(ADSHANDLE hStatement);
#undef AdsStmtClearTablePasswords
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStmtClearTablePasswords=_AdsStmtClearTablePasswords")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStmtClearTablePasswords(ADSHANDLE a0) {
    return oadsimpl_AdsStmtClearTablePasswords(a0);
}

/* ---- AdsStmtDisableEncryption ---- */
#define AdsStmtDisableEncryption oadsimpl_AdsStmtDisableEncryption
extern UNSIGNED32 ENTRYPOINT AdsStmtDisableEncryption(ADSHANDLE hStatement);
#undef AdsStmtDisableEncryption
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStmtDisableEncryption=_AdsStmtDisableEncryption")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStmtDisableEncryption(ADSHANDLE a0) {
    return oadsimpl_AdsStmtDisableEncryption(a0);
}

/* ---- AdsStmtSetTableCollation ---- */
#define AdsStmtSetTableCollation oadsimpl_AdsStmtSetTableCollation
extern UNSIGNED32 ENTRYPOINT AdsStmtSetTableCollation(ADSHANDLE hStatement, UNSIGNED8* pucCollation);
#undef AdsStmtSetTableCollation
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStmtSetTableCollation=_AdsStmtSetTableCollation")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStmtSetTableCollation(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsStmtSetTableCollation(a0, a1);
}

/* ---- AdsStmtSetTableLockType ---- */
#define AdsStmtSetTableLockType oadsimpl_AdsStmtSetTableLockType
extern UNSIGNED32 ENTRYPOINT AdsStmtSetTableLockType(ADSHANDLE hStmt, UNSIGNED16 usType);
#undef AdsStmtSetTableLockType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStmtSetTableLockType=_AdsStmtSetTableLockType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStmtSetTableLockType(ADSHANDLE a0, UNSIGNED16 a1) {
    return oadsimpl_AdsStmtSetTableLockType(a0, a1);
}

/* ---- AdsStmtSetTablePassword ---- */
#define AdsStmtSetTablePassword oadsimpl_AdsStmtSetTablePassword
extern UNSIGNED32 ENTRYPOINT AdsStmtSetTablePassword(ADSHANDLE hStmt, UNSIGNED8* pucName, UNSIGNED8* pucPwd);
#undef AdsStmtSetTablePassword
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStmtSetTablePassword=_AdsStmtSetTablePassword")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStmtSetTablePassword(ADSHANDLE a0, UNSIGNED8* a1, UNSIGNED8* a2) {
    return oadsimpl_AdsStmtSetTablePassword(a0, a1, a2);
}

/* ---- AdsStmtSetTableReadOnly ---- */
#define AdsStmtSetTableReadOnly oadsimpl_AdsStmtSetTableReadOnly
extern UNSIGNED32 ENTRYPOINT AdsStmtSetTableReadOnly(ADSHANDLE hStmt, UNSIGNED16 bReadOnly);
#undef AdsStmtSetTableReadOnly
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStmtSetTableReadOnly=_AdsStmtSetTableReadOnly")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStmtSetTableReadOnly(ADSHANDLE a0, UNSIGNED16 a1) {
    return oadsimpl_AdsStmtSetTableReadOnly(a0, a1);
}

/* ---- AdsStmtSetTableType ---- */
#define AdsStmtSetTableType oadsimpl_AdsStmtSetTableType
extern UNSIGNED32 ENTRYPOINT AdsStmtSetTableType(ADSHANDLE hStmt, UNSIGNED16 usType);
#undef AdsStmtSetTableType
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStmtSetTableType=_AdsStmtSetTableType")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStmtSetTableType(ADSHANDLE a0, UNSIGNED16 a1) {
    return oadsimpl_AdsStmtSetTableType(a0, a1);
}

/* ---- AdsStudioPort ---- */
#define AdsStudioPort oadsimpl_AdsStudioPort
extern UNSIGNED32 ENTRYPOINT AdsStudioPort(UNSIGNED16* pusPort);
#undef AdsStudioPort
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStudioPort=_AdsStudioPort")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStudioPort(UNSIGNED16* a0) {
    return oadsimpl_AdsStudioPort(a0);
}

/* ---- AdsStudioStart ---- */
#define AdsStudioStart oadsimpl_AdsStudioStart
extern UNSIGNED32 ENTRYPOINT AdsStudioStart(UNSIGNED16 usPort, UNSIGNED8* pucDataDir);
#undef AdsStudioStart
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStudioStart=_AdsStudioStart")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStudioStart(UNSIGNED16 a0, UNSIGNED8* a1) {
    return oadsimpl_AdsStudioStart(a0, a1);
}

/* ---- AdsStudioStop ---- */
#define AdsStudioStop oadsimpl_AdsStudioStop
extern UNSIGNED32 ENTRYPOINT AdsStudioStop(void);
#undef AdsStudioStop
#pragma comment(linker, "/alternatename:_oadsimpl_AdsStudioStop=_AdsStudioStop")
__declspec(dllexport) UNSIGNED32 __stdcall AdsStudioStop(void) {
    return oadsimpl_AdsStudioStop();
}

/* ---- AdsTestRecLocks ---- */
#define AdsTestRecLocks oadsimpl_AdsTestRecLocks
extern UNSIGNED32 ENTRYPOINT AdsTestRecLocks(ADSHANDLE hTable);
#undef AdsTestRecLocks
#pragma comment(linker, "/alternatename:_oadsimpl_AdsTestRecLocks=_AdsTestRecLocks")
__declspec(dllexport) UNSIGNED32 __stdcall AdsTestRecLocks(ADSHANDLE a0) {
    return oadsimpl_AdsTestRecLocks(a0);
}

/* ---- AdsThreadExit ---- */
#define AdsThreadExit oadsimpl_AdsThreadExit
extern UNSIGNED32 ENTRYPOINT AdsThreadExit(void);
#undef AdsThreadExit
#pragma comment(linker, "/alternatename:_oadsimpl_AdsThreadExit=_AdsThreadExit")
__declspec(dllexport) UNSIGNED32 __stdcall AdsThreadExit(void) {
    return oadsimpl_AdsThreadExit();
}

/* ---- AdsUnlockRecord ---- */
#define AdsUnlockRecord oadsimpl_AdsUnlockRecord
extern UNSIGNED32 ENTRYPOINT AdsUnlockRecord(ADSHANDLE hTable, UNSIGNED32 ulRecord);
#undef AdsUnlockRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsUnlockRecord=_AdsUnlockRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsUnlockRecord(ADSHANDLE a0, UNSIGNED32 a1) {
    return oadsimpl_AdsUnlockRecord(a0, a1);
}

/* ---- AdsUnlockTable ---- */
#define AdsUnlockTable oadsimpl_AdsUnlockTable
extern UNSIGNED32 ENTRYPOINT AdsUnlockTable(ADSHANDLE hTable);
#undef AdsUnlockTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsUnlockTable=_AdsUnlockTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsUnlockTable(ADSHANDLE a0) {
    return oadsimpl_AdsUnlockTable(a0);
}

/* ---- AdsVerifySQL ---- */
#define AdsVerifySQL oadsimpl_AdsVerifySQL
extern UNSIGNED32 ENTRYPOINT AdsVerifySQL(ADSHANDLE hStatement, UNSIGNED8* pucSQL);
#undef AdsVerifySQL
#pragma comment(linker, "/alternatename:_oadsimpl_AdsVerifySQL=_AdsVerifySQL")
__declspec(dllexport) UNSIGNED32 __stdcall AdsVerifySQL(ADSHANDLE a0, UNSIGNED8* a1) {
    return oadsimpl_AdsVerifySQL(a0, a1);
}

/* ---- AdsVerifySQLW ---- */
#define AdsVerifySQLW oadsimpl_AdsVerifySQLW
extern UNSIGNED32 ENTRYPOINT AdsVerifySQLW(ADSHANDLE hStatement, UNSIGNED16* pwcSQL);
#undef AdsVerifySQLW
#pragma comment(linker, "/alternatename:_oadsimpl_AdsVerifySQLW=_AdsVerifySQLW")
__declspec(dllexport) UNSIGNED32 __stdcall AdsVerifySQLW(ADSHANDLE a0, UNSIGNED16* a1) {
    return oadsimpl_AdsVerifySQLW(a0, a1);
}

/* ---- AdsWriteAllRecords ---- */
#define AdsWriteAllRecords oadsimpl_AdsWriteAllRecords
extern UNSIGNED32 ENTRYPOINT AdsWriteAllRecords(void);
#undef AdsWriteAllRecords
#pragma comment(linker, "/alternatename:_oadsimpl_AdsWriteAllRecords=_AdsWriteAllRecords")
__declspec(dllexport) UNSIGNED32 __stdcall AdsWriteAllRecords(void) {
    return oadsimpl_AdsWriteAllRecords();
}

/* ---- AdsWriteRecord ---- */
#define AdsWriteRecord oadsimpl_AdsWriteRecord
extern UNSIGNED32 ENTRYPOINT AdsWriteRecord(ADSHANDLE hTable);
#undef AdsWriteRecord
#pragma comment(linker, "/alternatename:_oadsimpl_AdsWriteRecord=_AdsWriteRecord")
__declspec(dllexport) UNSIGNED32 __stdcall AdsWriteRecord(ADSHANDLE a0) {
    return oadsimpl_AdsWriteRecord(a0);
}

/* ---- AdsZapTable ---- */
#define AdsZapTable oadsimpl_AdsZapTable
extern UNSIGNED32 ENTRYPOINT AdsZapTable(ADSHANDLE hTable);
#undef AdsZapTable
#pragma comment(linker, "/alternatename:_oadsimpl_AdsZapTable=_AdsZapTable")
__declspec(dllexport) UNSIGNED32 __stdcall AdsZapTable(ADSHANDLE a0) {
    return oadsimpl_AdsZapTable(a0);
}

