#pragma once

// OpenADS ACE-compatible C ABI — phase 1, milestone M1 subset.
// See openads/error.h for AE_* error codes.

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// SAP-shipped ace.h tags every public function with ENTRYPOINT, which
// expands to platform-specific calling-convention + import/export
// attributes. OpenADS uses plain C linkage on every export so consumers
// (Harbour's contrib/rddads, third-party Clipper apps) link the same
// import library without picking up extra ABI baggage.
#ifndef ENTRYPOINT
#  define ENTRYPOINT
#endif

typedef uint8_t  UNSIGNED8;
typedef uint16_t UNSIGNED16;
typedef uint32_t UNSIGNED32;
typedef int32_t  SIGNED32;
typedef int64_t  SIGNED64;
typedef uint64_t UNSIGNED64;
typedef uint64_t ADSHANDLE;

#define ADS_DEFAULT 0
#define ADS_NTX     1
#define ADS_CDX     2
#define ADS_ADT     3
#define ADS_VFP     4

#define ADS_LOCAL_SERVER  1
#define ADS_REMOTE_SERVER 2

UNSIGNED32 AdsConnect60     (UNSIGNED8* pucServer, UNSIGNED16 usServerType,
                              UNSIGNED8* pucUserName, UNSIGNED8* pucPassword,
                              UNSIGNED32 ulOptions, ADSHANDLE* phConnect);
UNSIGNED32 AdsDisconnect    (ADSHANDLE hConnect);

UNSIGNED32 AdsOpenTable     (ADSHANDLE  hConnect,
                              UNSIGNED8* pucName,
                              UNSIGNED8* pucAlias,
                              UNSIGNED16 usTableType,
                              UNSIGNED16 usCharType,
                              UNSIGNED16 usLockType,
                              UNSIGNED16 usCheckRights,
                              UNSIGNED16 usMode,
                              ADSHANDLE* phTable);
UNSIGNED32 AdsCloseTable    (ADSHANDLE hTable);

UNSIGNED32 AdsGotoTop       (ADSHANDLE hTable);
UNSIGNED32 AdsGotoBottom    (ADSHANDLE hTable);
UNSIGNED32 AdsGotoRecord    (ADSHANDLE hTable, UNSIGNED32 ulRecord);

UNSIGNED32 AdsGetTableType  (ADSHANDLE hTable, UNSIGNED16* pusType);
UNSIGNED32 AdsGetTableFilename(ADSHANDLE  hTable, UNSIGNED16 usOption,
                              UNSIGNED8* pucBuf, UNSIGNED16* pusLen);

UNSIGNED32 AdsCheckExistence(ADSHANDLE  hConnect, UNSIGNED8* pucName,
                              UNSIGNED16* pbExists);
UNSIGNED32 AdsDeleteFile    (ADSHANDLE  hConnect, UNSIGNED8* pucName);
UNSIGNED32 AdsCloseAllTables(void);
UNSIGNED32 AdsGetRecordLength(ADSHANDLE hTable, UNSIGNED32* pulLen);

UNSIGNED32 AdsRefreshRecord (ADSHANDLE hTable);
UNSIGNED32 AdsReindex       (ADSHANDLE hTable);
UNSIGNED32 AdsCopyTable     (ADSHANDLE  hHandle, UNSIGNED16 usFilterOption,
                              UNSIGNED8* pucFile);
UNSIGNED32 ENTRYPOINT AdsCopyTableContents(ADSHANDLE hSrc, ADSHANDLE hDst,
                              UNSIGNED16 usFilterOption);
UNSIGNED32 AdsConvertTable  (ADSHANDLE  hHandle, UNSIGNED16 usFilterOption,
                              UNSIGNED8* pucFile, UNSIGNED16 usTargetType);
UNSIGNED32 AdsFTSSearch     (ADSHANDLE   hConnect,
                              UNSIGNED8*  pucFile,
                              UNSIGNED8*  pucQuery,
                              UNSIGNED32* paRecnos,
                              UNSIGNED32* pulCount);

UNSIGNED32 AdsCreateFTSIndex(ADSHANDLE   hTable,
                              UNSIGNED8*  pucFileName,
                              UNSIGNED8*  pucTag,
                              UNSIGNED8*  pucField,
                              UNSIGNED32  ulPageSize,
                              UNSIGNED32  ulMinWordLen,
                              UNSIGNED32  ulMaxWordLen,
                              UNSIGNED16  usUseDefaultDelim,
                              UNSIGNED8*  pucDelimiters,
                              UNSIGNED16  usUseDefaultNoise,
                              UNSIGNED8*  pucNoiseWords,
                              UNSIGNED16  usUseDefaultDrop,
                              UNSIGNED8*  pucDropChars,
                              UNSIGNED16  usUseDefaultConditionals,
                              UNSIGNED8*  pucConditionalChars,
                              UNSIGNED8*  pucReserved1,
                              UNSIGNED8*  pucReserved2,
                              UNSIGNED32  ulOptions);

UNSIGNED32 AdsCreateIndex61 (ADSHANDLE  hTable, UNSIGNED8* pucFileName,
                              UNSIGNED8* pucIndexName, UNSIGNED8* pucExpr,
                              UNSIGNED8* pucCondition, UNSIGNED8* pucKeyFilter,
                              UNSIGNED32 ulOptions, UNSIGNED16 usPageSize,
                              ADSHANDLE* phIndex);
UNSIGNED32 AdsExtractKey    (ADSHANDLE hIndex, UNSIGNED8* pucBuf,
                              UNSIGNED16* pusLen);

UNSIGNED32 AdsAddCustomKey   (ADSHANDLE hIndex);
UNSIGNED32 AdsDeleteCustomKey(ADSHANDLE hIndex);

UNSIGNED32 AdsGetLongLong   (ADSHANDLE hTable, UNSIGNED8* pucField,
                              int64_t* pllValue);
UNSIGNED32 AdsSetFieldRaw   (ADSHANDLE hTable, UNSIGNED8* pucField,
                              UNSIGNED8* pucBuf, UNSIGNED32 ulLen);
UNSIGNED32 AdsVerifySQL     (ADSHANDLE  hStatement, UNSIGNED8* pucSQL);
UNSIGNED32 AdsFailedTransactionRecovery(UNSIGNED8* pucServer);
UNSIGNED32 AdsGetAllLocks   (ADSHANDLE hTable, UNSIGNED32* paRecnos,
                              UNSIGNED16* pusCount);
UNSIGNED32 AdsSkipUnique    (ADSHANDLE hIndex, SIGNED32 lDirection);

UNSIGNED32 AdsCreateTable   (ADSHANDLE  hConnect, UNSIGNED8* pucName,
                              UNSIGNED8* pucAlias,
                              UNSIGNED16 usTableType, UNSIGNED16 usCharType,
                              UNSIGNED16 usLockType, UNSIGNED16 usCheckRights,
                              UNSIGNED16 usMemoBlockSize,
                              UNSIGNED8* pucFields,
                              ADSHANDLE* phTable);
UNSIGNED32 AdsRestructureTable(ADSHANDLE  hConnect, UNSIGNED8* pucTableName,
                              UNSIGNED8* pucAlias,
                              UNSIGNED16 usFileType, UNSIGNED16 usCharType,
                              UNSIGNED16 usLockType, UNSIGNED16 usCheckRights,
                              UNSIGNED8* pucAddFields,
                              UNSIGNED8* pucDeleteFields,
                              UNSIGNED8* pucChangeFields);
UNSIGNED32 AdsSkip          (ADSHANDLE hTable, SIGNED32 lRows);
UNSIGNED32 AdsAtEOF         (ADSHANDLE hTable, UNSIGNED16* pbAtEnd);
UNSIGNED32 AdsAtBOF         (ADSHANDLE hTable, UNSIGNED16* pbAtBegin);

UNSIGNED32 AdsGetField      (ADSHANDLE  hTable, UNSIGNED8* pucField,
                              UNSIGNED8* pucBuf, UNSIGNED32* pulLen,
                              UNSIGNED16 usOption);
UNSIGNED32 AdsGetFieldName  (ADSHANDLE  hTable, UNSIGNED16 usFieldNum,
                              UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
UNSIGNED32 AdsGetNumFields  (ADSHANDLE  hTable, UNSIGNED16* pusFields);
UNSIGNED32 AdsGetFieldType  (ADSHANDLE  hTable, UNSIGNED8* pucField,
                              UNSIGNED16* pusType);
UNSIGNED32 AdsGetFieldLength(ADSHANDLE  hTable, UNSIGNED8* pucField,
                              UNSIGNED32* pulLen);
UNSIGNED32 AdsGetRecordNum  (ADSHANDLE  hTable, UNSIGNED16 bFilterOption,
                              UNSIGNED32* pulRecordNum);
UNSIGNED32 AdsGetRecordCount(ADSHANDLE  hTable, UNSIGNED16 bFilterOption,
                              UNSIGNED32* pulRecordCount);

UNSIGNED32 AdsGetLastError  (UNSIGNED32* pulCode, UNSIGNED8* pucBuf,
                              UNSIGNED16* pusBufLen);

UNSIGNED32 ENTRYPOINT AdsGetVersion (UNSIGNED32* pulMajor,
                              UNSIGNED32* pulMinor,
                              UNSIGNED8*  pucLetter,
                              UNSIGNED8*  pucDesc,
                              UNSIGNED16* pusDescLen);

UNSIGNED32 AdsGetServerName (ADSHANDLE   hConnect,
                              UNSIGNED8*  pucBuf, UNSIGNED16* pusLen);
UNSIGNED32 AdsGetServerTime (ADSHANDLE   hConnect,
                              UNSIGNED8*  pucDateBuf, UNSIGNED16* pusDateLen,
                              SIGNED32*   plTime,
                              UNSIGNED8*  pucTimeBuf, UNSIGNED16* pusTimeLen);

UNSIGNED32 AdsAppendRecord  (ADSHANDLE hTable);
UNSIGNED32 AdsWriteRecord   (ADSHANDLE hTable);
UNSIGNED32 AdsDeleteRecord  (ADSHANDLE hTable);
UNSIGNED32 AdsRecallRecord  (ADSHANDLE hTable);
UNSIGNED32 AdsIsRecordDeleted(ADSHANDLE hTable, UNSIGNED16* pbDeleted);

UNSIGNED32 AdsSetString     (ADSHANDLE hTable, UNSIGNED8* pucField,
                              UNSIGNED8* pucValue, UNSIGNED32 ulLen);
UNSIGNED32 ENTRYPOINT AdsGetString (ADSHANDLE hTable, UNSIGNED8* pucField,
                              UNSIGNED8* pucBuf, UNSIGNED32* pulLen,
                              UNSIGNED16 usOption);
UNSIGNED32 ENTRYPOINT AdsGetLong   (ADSHANDLE hTable, UNSIGNED8* pucField,
                              SIGNED32* plVal);
// SAP ACE W-variants keep field names as ASCII (UNSIGNED8*); only
// the data buffer (pucValueW / pucBufW) is wide-char UTF-16LE.
// Harbour's ads1.c passes ADSFIELD(n) (UNSIGNED8*) — matching SAP.
// Cast a 1-based field index to the UNSIGNED8* accepted by ADS API
// functions that take either a name string or a numeric field index.
#ifndef ADSFIELD
#  define ADSFIELD( n )  ( ( UNSIGNED8 * ) ( uintptr_t ) ( n ) )
#endif
UNSIGNED32 AdsSetStringW    (ADSHANDLE  hTable, UNSIGNED8* pucField,
                              UNSIGNED16* pucValueW, UNSIGNED32 ulLen);
UNSIGNED32 AdsGetStringW    (ADSHANDLE  hTable, UNSIGNED8* pucField,
                              UNSIGNED16* pucBufW, UNSIGNED32* pulLenW,
                              UNSIGNED16 usOption);
UNSIGNED32 AdsGetFieldW     (ADSHANDLE  hTable, UNSIGNED8* pucField,
                              UNSIGNED16* pucBufW, UNSIGNED32* pulLenW,
                              UNSIGNED16 usOption);
UNSIGNED32 AdsSetLogical    (ADSHANDLE hTable, UNSIGNED8* pucField,
                              UNSIGNED16 bValue);
UNSIGNED32 AdsSetDouble     (ADSHANDLE hTable, UNSIGNED8* pucField,
                              double dValue);

UNSIGNED32 AdsLockRecord    (ADSHANDLE hTable, UNSIGNED32 ulRecord);
UNSIGNED32 AdsUnlockRecord  (ADSHANDLE hTable, UNSIGNED32 ulRecord);
UNSIGNED32 AdsLockTable     (ADSHANDLE hTable);
UNSIGNED32 AdsUnlockTable   (ADSHANDLE hTable);

UNSIGNED32 AdsSetLockCycle      (ADSHANDLE hConnect, UNSIGNED32 ulCycle);
UNSIGNED32 AdsGetLockCycle      (ADSHANDLE hConnect, UNSIGNED32* pulCycle);
UNSIGNED32 AdsSetLockRetryCount (ADSHANDLE hConnect, UNSIGNED16 usRetryCount);
UNSIGNED32 AdsGetLockRetryCount (ADSHANDLE hConnect, UNSIGNED16* pusRetryCount);

UNSIGNED32 AdsFlushFileBuffers(ADSHANDLE hTable);
UNSIGNED32 AdsSetDeferredFlush(ADSHANDLE hTable, UNSIGNED16 usDeferred);

UNSIGNED32 AdsOpenIndex     (ADSHANDLE hTable, UNSIGNED8* pucName,
                              ADSHANDLE* ahIndex,
                              UNSIGNED16* pu16ArrayLen);
UNSIGNED32 AdsCloseIndex    (ADSHANDLE hIndex);
UNSIGNED32 AdsCloseAllIndexes(ADSHANDLE hTable);
UNSIGNED32 AdsCreateIndex   (ADSHANDLE hTable, UNSIGNED8* pucFile,
                              UNSIGNED8* pucTag, UNSIGNED8* pucExpr,
                              UNSIGNED8* pucCondition, UNSIGNED32 ulOptions,
                              UNSIGNED16 usKeyType, ADSHANDLE* phIndex);
UNSIGNED32 AdsDeleteIndex   (ADSHANDLE hIndex);
UNSIGNED32 AdsGetNumIndexes (ADSHANDLE hTable, UNSIGNED16* pusCount);
UNSIGNED32 AdsGetIndexHandle(ADSHANDLE hTable, UNSIGNED8* pucName,
                              ADSHANDLE* phIndex);
UNSIGNED32 AdsGetIndexHandleByOrder(ADSHANDLE hTable, UNSIGNED16 usOrder,
                                    ADSHANDLE* phIndex);
UNSIGNED32 AdsGetIndexExpr  (ADSHANDLE hIndex, UNSIGNED8* pucBuf,
                              UNSIGNED16* pusBufLen);
UNSIGNED32 AdsGetIndexName  (ADSHANDLE hIndex, UNSIGNED8* pucBuf,
                              UNSIGNED16* pusBufLen);
UNSIGNED32 AdsSetIndexDirection(ADSHANDLE hIndex, UNSIGNED16 usDir);

UNSIGNED32 AdsSeek          (ADSHANDLE hIndex, UNSIGNED8* pucKey,
                              UNSIGNED16 usKeyLen, UNSIGNED16 usKeyType,
                              UNSIGNED16 usSeekType, UNSIGNED16* pbFound);
UNSIGNED32 AdsSeekLast      (ADSHANDLE hIndex, UNSIGNED8* pucKey,
                              UNSIGNED16 usKeyLen, UNSIGNED16 usKeyType,
                              UNSIGNED16* pbFound);

UNSIGNED32 ENTRYPOINT AdsSetScope (ADSHANDLE hIndex, UNSIGNED16 usScope,
                              UNSIGNED8* pucScope, UNSIGNED16 usLen,
                              UNSIGNED16 usDataType);
UNSIGNED32 AdsClearScope    (ADSHANDLE hIndex, UNSIGNED16 usScope);
UNSIGNED32 AdsGetScope      (ADSHANDLE hIndex, UNSIGNED16 usScope,
                              UNSIGNED8* pucBuf, UNSIGNED16* pusLen);

UNSIGNED32 AdsPackTable     (ADSHANDLE hTable);
UNSIGNED32 AdsZapTable      (ADSHANDLE hTable);
UNSIGNED32 AdsSetAOF        (ADSHANDLE hTable, UNSIGNED8* pucCondition,
                              UNSIGNED16 usResolve);
UNSIGNED32 AdsGetAOFOptLevel(ADSHANDLE hTable, UNSIGNED16* pusLevel,
                              UNSIGNED8* pucBuf, UNSIGNED16* pusLen);
UNSIGNED32 AdsClearAOF      (ADSHANDLE hTable);

UNSIGNED32 AdsGetMemoLength    (ADSHANDLE hTable, UNSIGNED8* pucField,
                                 UNSIGNED32* pulLen);
UNSIGNED32 AdsGetMemoDataType  (ADSHANDLE hTable, UNSIGNED8* pucField,
                                 UNSIGNED16* pusType);
UNSIGNED32 AdsBinaryToFile     (ADSHANDLE hTable, UNSIGNED8* pucField,
                                 UNSIGNED8* pucPath);
UNSIGNED32 AdsFileToBinary     (ADSHANDLE hTable, UNSIGNED8* pucField,
                                 UNSIGNED16 usType, UNSIGNED8* pucPath);
UNSIGNED32 AdsGetBinaryLength  (ADSHANDLE hTable, UNSIGNED8* pucField,
                                 UNSIGNED32* pulLength);
UNSIGNED32 AdsGetBinary        (ADSHANDLE hTable, UNSIGNED8* pucField,
                                 UNSIGNED32 ulOffset, UNSIGNED8* pucBuf,
                                 UNSIGNED32* pulLen);
UNSIGNED32 AdsSetBinary        (ADSHANDLE hTable, UNSIGNED8* pucField,
                                 UNSIGNED16 usBinaryType,
                                 UNSIGNED32 ulTotalBytes,
                                 UNSIGNED32 ulOffset,
                                 UNSIGNED8* pucBuf, UNSIGNED32 ulBytes);
UNSIGNED32 AdsGetLastAutoinc   (ADSHANDLE hTable, UNSIGNED32* pulValue);

UNSIGNED32 AdsEnableEncryption (ADSHANDLE hConnect, UNSIGNED8* pucPassword);
UNSIGNED32 AdsDisableEncryption(ADSHANDLE hConnect);
UNSIGNED32 AdsIsEncryptionEnabled(ADSHANDLE hConnect, UNSIGNED16* pbEnabled);
UNSIGNED32 AdsIsTableEncrypted (ADSHANDLE hTable, UNSIGNED16* pbEncrypted);
UNSIGNED32 AdsIsRecordEncrypted(ADSHANDLE hTable, UNSIGNED16* pbEncrypted);
UNSIGNED32 AdsEncryptTable     (ADSHANDLE hTable);
UNSIGNED32 AdsDecryptTable     (ADSHANDLE hTable);
UNSIGNED32 AdsEncryptRecord    (ADSHANDLE hTable);
UNSIGNED32 AdsDecryptRecord    (ADSHANDLE hTable);

UNSIGNED32 AdsBeginTransaction   (ADSHANDLE hConnect);
UNSIGNED32 AdsCommitTransaction  (ADSHANDLE hConnect);
UNSIGNED32 AdsRollbackTransaction(ADSHANDLE hConnect);
UNSIGNED32 AdsInTransaction      (ADSHANDLE hConnect, UNSIGNED16* pbInTx);

UNSIGNED32 ENTRYPOINT AdsCreateSavepoint    (ADSHANDLE hConnect, UNSIGNED8* pucName,
                              UNSIGNED32 ulOptions);
UNSIGNED32 ENTRYPOINT AdsReleaseSavepoint   (ADSHANDLE hConnect, UNSIGNED8* pucName);
UNSIGNED32 AdsSetEncryptionPassword(ADSHANDLE hConnect, UNSIGNED8* pucPassword);
UNSIGNED32 AdsSetCollation       (ADSHANDLE hConnect, UNSIGNED8* pucName);
UNSIGNED32 AdsConvertOemToAnsi   (UNSIGNED8* pucBuf, UNSIGNED32* pulLen);
UNSIGNED32 AdsConvertAnsiToOem   (UNSIGNED8* pucBuf, UNSIGNED32* pulLen);
UNSIGNED32 ENTRYPOINT AdsRollbackTransaction80(ADSHANDLE hConnect, UNSIGNED8* pucSavepoint,
                              UNSIGNED32 ulOptions);

UNSIGNED32 AdsFindFirstTable     (ADSHANDLE   hConnect,
                                  UNSIGNED8*  pucMask,
                                  UNSIGNED8*  pucFileName,
                                  UNSIGNED16* pusFileNameLen,
                                  ADSHANDLE*  phFind);
UNSIGNED32 AdsFindNextTable      (ADSHANDLE   hConnect,
                                  ADSHANDLE   hFind,
                                  UNSIGNED8*  pucFileName,
                                  UNSIGNED16* pusFileNameLen);
UNSIGNED32 AdsFindClose          (ADSHANDLE hConnect, ADSHANDLE hFind);

UNSIGNED32 AdsDDCreate               (UNSIGNED8*  pucDictionary,
                                      UNSIGNED16  bEncrypt,
                                      UNSIGNED8*  pucAdminPassword,
                                      ADSHANDLE*  phConnect);
UNSIGNED32 AdsDDAddTable             (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucAlias,
                                      UNSIGNED8*  pucTablePath,
                                      UNSIGNED16  usFileType,
                                      UNSIGNED16  usCharType,
                                      UNSIGNED8*  pucIndexPath,
                                      UNSIGNED8*  pucComment);
UNSIGNED32 AdsDDRemoveTable          (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucAlias,
                                      UNSIGNED16  usDeleteFiles);
UNSIGNED32 AdsDDSetDatabaseProperty  (ADSHANDLE   hConnect,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16  usPropertyLen);
UNSIGNED32 AdsDDGetDatabaseProperty  (ADSHANDLE   hConnect,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16* pusPropertyLen);
UNSIGNED32 AdsDDGetTableProperty     (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucTableName,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16* pusPropertyLen);
UNSIGNED32 AdsDDSetTableProperty     (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucTableName,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16  usPropertyLen);
UNSIGNED32 AdsDDGetFieldProperty     (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucTableName,
                                      UNSIGNED8*  pucFieldName,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16* pusPropertyLen);
UNSIGNED32 AdsDDSetFieldProperty     (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucTableName,
                                      UNSIGNED8*  pucFieldName,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16  usPropertyLen);
UNSIGNED32 AdsDDAddIndexFile         (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucTableName,
                                      UNSIGNED8*  pucIndexFile,
                                      UNSIGNED8*  pucComment);
UNSIGNED32 AdsDDRemoveIndexFile      (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucTableName,
                                      UNSIGNED8*  pucIndexFile,
                                      UNSIGNED16  usDeleteFile);
UNSIGNED32 AdsDDGetIndexProperty     (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucTableName,
                                      UNSIGNED8*  pucTagName,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16* pusPropertyLen);
UNSIGNED32 AdsDDSetIndexProperty     (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucTableName,
                                      UNSIGNED8*  pucTagName,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16  usPropertyLen);
UNSIGNED32 AdsDDCreateUser           (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucGroup,
                                      UNSIGNED8*  pucUser,
                                      UNSIGNED8*  pucPassword,
                                      UNSIGNED8*  pucDescription);
UNSIGNED32 AdsDDDeleteUser           (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucUser);
UNSIGNED32 AdsDDGetUserProperty      (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucUser,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16* pusPropertyLen);
UNSIGNED32 AdsDDSetUserProperty      (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucUser,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16  usPropertyLen);
UNSIGNED32 AdsDDAddUserToGroup       (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucGroup,
                                      UNSIGNED8*  pucUser);
UNSIGNED32 AdsDDRemoveUserFromGroup  (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucGroup,
                                      UNSIGNED8*  pucUser);
UNSIGNED32 AdsDDGetUserTableRights   (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucTableName,
                                      UNSIGNED8*  pucUser,
                                      UNSIGNED32* pulRights);
UNSIGNED32 AdsDDSetUserTableRights   (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucTableName,
                                      UNSIGNED8*  pucUser,
                                      UNSIGNED32  ulRights);
UNSIGNED32 AdsDDCreateView           (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName,
                                      UNSIGNED8*  pucComment,
                                      UNSIGNED8*  pucSQL);
UNSIGNED32 AdsDDDropView             (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName);
UNSIGNED32 AdsDDGetViewProperty      (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16* pusPropertyLen);
UNSIGNED32 AdsDDSetViewProperty      (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16  usPropertyLen);
UNSIGNED32 AdsDDCreateProcedure      (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName,
                                      UNSIGNED8*  pucContainer,
                                      UNSIGNED8*  pucProcName,
                                      UNSIGNED32  ulInvokeOption,
                                      UNSIGNED8*  pucInParams,
                                      UNSIGNED8*  pucOutParams,
                                      UNSIGNED8*  pucComments);
UNSIGNED32 AdsDDDropProcedure        (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName);
UNSIGNED32 AdsDDGetProcProperty      (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16* pusPropertyLen);
UNSIGNED32 AdsDDSetProcProperty      (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16  usPropertyLen);
UNSIGNED32 AdsDDCreateFunction         (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName,
                                      UNSIGNED8*  pucContainer,
                                      UNSIGNED8*  pucImplementation,
                                      UNSIGNED8*  pucRetType,
                                      UNSIGNED8*  pucInParams,
                                      UNSIGNED8*  pucComment);
UNSIGNED32 AdsDDDropFunction           (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName);
UNSIGNED32 AdsDDGetFunctionProperty    (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16* pusPropertyLen);
UNSIGNED32 AdsDDSetFunctionProperty    (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16  usPropertyLen);
UNSIGNED32 AdsDDGetRefIntegrityProperty(ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16* pusPropertyLen);
UNSIGNED32 AdsDDSetRefIntegrityProperty(ADSHANDLE  hConnect,
                                      UNSIGNED8*  pucName,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16  usPropertyLen);
UNSIGNED32 AdsDDCreateTrigger        (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName,
                                      UNSIGNED8*  pucTable,
                                      UNSIGNED32  ulType,
                                      UNSIGNED32  ulOptions,
                                      UNSIGNED8*  pucContainer,
                                      UNSIGNED8*  pucProcedure,
                                      UNSIGNED32  ulPriority);
UNSIGNED32 AdsDDDropTrigger          (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName);
UNSIGNED32 AdsDDGetTriggerProperty   (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16* pusPropertyLen);
UNSIGNED32 AdsDDSetTriggerProperty   (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16  usPropertyLen);
// SAP ACE API name aliases
UNSIGNED32 AdsDDAddProcedure         (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName,
                                      UNSIGNED8*  pucContainer,
                                      UNSIGNED8*  pucProcName,
                                      UNSIGNED32  ulInvokeOption,
                                      UNSIGNED8*  pucInParams,
                                      UNSIGNED8*  pucOutParams,
                                      UNSIGNED8*  pucComments);
UNSIGNED32 AdsDDRemoveProcedure      (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName);
UNSIGNED32 AdsDDGetProcedureProperty (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16* pusPropertyLen);
UNSIGNED32 AdsDDSetProcedureProperty (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName,
                                      UNSIGNED16  usPropertyID,
                                      void*       pvProperty,
                                      UNSIGNED16  usPropertyLen);
UNSIGNED32 AdsDDRemoveTrigger        (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName);
UNSIGNED32 AdsDDFindFirstObject      (ADSHANDLE   hObject,
                                      UNSIGNED16  usFindObjectType,
                                      UNSIGNED8*  pucParentName,
                                      UNSIGNED8*  pucObjectName,
                                      UNSIGNED16* pusObjectNameLen,
                                      ADSHANDLE*  phFindHandle);
UNSIGNED32 AdsDDFindNextObject       (ADSHANDLE   hObject,
                                      ADSHANDLE   hFindHandle,
                                      UNSIGNED8*  pucObjectName,
                                      UNSIGNED16* pusObjectNameLen);
UNSIGNED32 AdsDDFindClose            (ADSHANDLE   hObject,
                                      ADSHANDLE   hFindHandle);
UNSIGNED32 AdsDDCreateRefIntegrity   (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName,
                                      UNSIGNED8*  pucFailTable,
                                      UNSIGNED8*  pucParent,
                                      UNSIGNED8*  pucParentTag,
                                      UNSIGNED8*  pucChild,
                                      UNSIGNED8*  pucChildTag,
                                      UNSIGNED16  usUpdateOption,
                                      UNSIGNED16  usDeleteOption);
UNSIGNED32 AdsDDRemoveRefIntegrity   (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucName);
UNSIGNED32 AdsDDCreateLink           (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucAlias,
                                      UNSIGNED8*  pucPath,
                                      UNSIGNED8*  pucUser,
                                      UNSIGNED8*  pucPassword,
                                      UNSIGNED16  usOptions);
UNSIGNED32 AdsDDDropLink             (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucAlias,
                                      UNSIGNED16  usOptions);
UNSIGNED32 AdsDDModifyLink           (ADSHANDLE   hConnect,
                                      UNSIGNED8*  pucAlias,
                                      UNSIGNED8*  pucPath,
                                      UNSIGNED8*  pucUser,
                                      UNSIGNED8*  pucPassword,
                                      UNSIGNED16  usOptions);

UNSIGNED32 AdsCreateSQLStatement (ADSHANDLE hConnect, ADSHANDLE* phStatement);
UNSIGNED32 AdsCloseSQLStatement  (ADSHANDLE hStatement);
UNSIGNED32 AdsPrepareSQL         (ADSHANDLE hStatement, UNSIGNED8* pucSQL);
UNSIGNED32 AdsGetNumParams       (ADSHANDLE hStatement, UNSIGNED16* pusNumParams);
UNSIGNED32 AdsExecuteSQL         (ADSHANDLE hStatement, ADSHANDLE* phCursor);
UNSIGNED32 AdsExecuteSQLDirect   (ADSHANDLE hStatement, UNSIGNED8* pucSQL,
                                  ADSHANDLE* phCursor);

#define ADS_TOP            0
#define ADS_BOTTOM         1
#define ADS_SOFTSEEK       1
#define ADS_OPTIMIZED_NONE 3
// Memo content kinds returned by AdsGetMemoDataType. Match the
// numeric values rddads switches against in its HB_FT_MEMO branch.
// (See ADS_BINARY / ADS_IMAGE further below for the binary kinds.)
#define ADS_MEMO_TEXT      4   // alias for ADS_STRING; text memo
#define ADS_MEMO_PICTURE   7   // alias for ADS_IMAGE; binary memo

// ACE field-type constants.
//
// Values empirically verified (M8.4) against the prebuilt
// `c:\harbour\lib\win\msvc64\rddads.lib` by sweeping `AdsGetFieldType`'s
// return through 0..40 and observing what Clipper field type the
// `adsOpen` switch routes each value to. These are the numeric
// values rddads.lib decodes; OpenADS publishes the same constants so
// applications built against rddads see the contract they expect.
//
//   value | rddads dbFieldInfo.uiType -> Clipper FieldType()
//   ------+--------------------------------------------------
//     1   | HB_FT_LOGICAL          'L'
//     2   | HB_FT_LONG / numeric   'N'
//     3   | HB_FT_DATE             'D'
//     4   | HB_FT_STRING           'C'   <- standard char
//     5   | HB_FT_MEMO             'M'
//     6   | HB_FT_BLOB             'W'
//     7   | HB_FT_IMAGE            'P'
//     8   | HB_FT_VARLENGTH        'Q'
//     9   | HB_FT_DATE             'D'   (compactdate)
//    10   | HB_FT_DOUBLE           'B'
//    11   | HB_FT_INTEGER          'I'
//    12   | HB_FT_INTEGER          'I'   (shortint alias)
//    13   | HB_FT_TIME             'T'
//    14   | HB_FT_TIMESTAMP        '@'
//    15   | HB_FT_AUTOINC          '+'
//    16   | HB_FT_STRING           'C'   (alias)
//    17   | HB_FT_CURDOUBLE        'Z'
//    18   | HB_FT_CURRENCY         'Y'   (money)
//    19   | HB_FT_INTEGER          'I'   (longlong)
//    20   | HB_FT_STRING           'CICHARACTER'  (case-insensitive)
//    21   | HB_FT_ROWVER           '^'
//    22   | HB_FT_MODTIME          '='
//    23   | HB_FT_VARLENGTH        'Q'   (varchar fox)
//    24   | HB_FT_VARLENGTH        'Q'   (varbinary fox)
//    26   | HB_FT_STRING           'C'   (nchar / unicode)
//    27   | HB_FT_VARLENGTH        'Q'   (nvarchar)
//    28   | HB_FT_MEMO             'M'   (nmemo)
//    other| EDBF_CORRUPT (open fails)
#define ADS_LOGICAL               1
// SAP-canonical field-type codes. These values MUST match the
// numeric assignments in the public ace.h so Harbour switch
// statements over uiType-extended see no duplicate-case collisions.
#define ADS_NUMERIC               2
#define ADS_DATE                  3
#define ADS_STRING                4
#define ADS_MEMO                  5
#define ADS_BINARY                6
#define ADS_IMAGE                 7
#define ADS_VARCHAR_FOX           8
#define ADS_VARBINARY_FOX         9
#define ADS_DOUBLE               10
#define ADS_INTEGER              11
#define ADS_SHORTINT             12
#define ADS_TIME                 13
#define ADS_TIMESTAMP            14
#define ADS_AUTOINC              15
#define ADS_RAW                  16
#define ADS_CURDOUBLE            17
#define ADS_MONEY                18
#define ADS_LONGLONG             19
#define ADS_COMPACTDATE          20
#define ADS_ROWVERSION           21
#define ADS_MODTIME              22
#define ADS_VARCHAR              23
#define ADS_VARBINARY            24
#define ADS_CISTRING             25
#define ADS_NCHAR                26
#define ADS_NVARCHAR             27
#define ADS_NMEMO                28
// Legacy ADS_FIELD_TYPE_* aliases kept for the engine-side code that
// pre-dates the ace.h authoritative constants.
#define ADS_FIELD_TYPE_CHAR       ADS_STRING
#define ADS_FIELD_TYPE_NUMERIC    ADS_NUMERIC
#define ADS_FIELD_TYPE_LOGICAL    ADS_LOGICAL
#define ADS_FIELD_TYPE_DATE       ADS_DATE
#define ADS_FIELD_TYPE_DATETIME   ADS_TIMESTAMP
#define ADS_FIELD_TYPE_MEMO       ADS_MEMO
#define ADS_FIELD_TYPE_INTEGER    ADS_INTEGER
#define ADS_FIELD_TYPE_DOUBLE     ADS_DOUBLE
#define ADS_FIELD_TYPE_CURRENCY   ADS_MONEY
#define ADS_FIELD_TYPE_UNKNOWN    99

// ---- Constants required by Harbour contrib/rddads ----
// These mirror the SAP-shipped ace.h constants whose numeric values
// are documented in the Advantage Database SDK and reproduced here
// from public Sybase / SAP documentation. OpenADS is the receiving
// (server) side; the values must match what the standard rddads
// contrib expects so existing Harbour applications and tests
// (rddtst.prg, cdxcl52.prg, ntxcl52.prg) compile and run unchanged.

// Index option flags (AdsCreateIndex61 ulOptions). The numeric values
// match the Advantage Database SDK exactly — VERIFIED empirically against
// the Harbour rddads contrib: `INDEX ON f TAG t` sends 0x02 (COMPOUND),
// `... DESCENDING` adds 0x08 (DESCENDING), `... UNIQUE` adds 0x01.
// rddads / X#'s ADSRDD set COMPOUND on EVERY .cdx tag, so COMPOUND must
// never be mistaken for DESCENDING (that mistake builds every order
// reversed). DO NOT swap DESCENDING and COMPOUND again.
#define ADS_DEFAULT_INDEX  0x00000000
#define ADS_UNIQUE         0x00000001
#define ADS_COMPOUND       0x00000002
#define ADS_CUSTOM         0x00000004
#define ADS_DESCENDING     0x00000008
#define ADS_FTS_INDEX_ORDER 0x00000010
#define ADS_INDEX_ORDER    0x00000020

// Lock / share modes.
#define ADS_EXCLUSIVE      1
#define ADS_SHARED         2
#define ADS_READONLY       3
#define ADS_DEFAULT_MODE   0

// Char-type / character-set codes.
#define ADS_ANSI           1
#define ADS_OEM            2

// Filter / scope flags.
#define ADS_RESPECTSCOPES   1
#define ADS_RESPECTFILTERS  2
#define ADS_IGNOREFILTERS   3
#define ADS_IGNORERIGHTS    4
// ADS_HARDSEEK is the OPPOSITE of ADS_SOFTSEEK (defined at line 320 as 1).
// rddads passes one or the other to AdsSeek* etc. as a single bit-flag.
#define ADS_HARDSEEK        0
#define ADS_CHECKRIGHTS     1
#define ADS_OPTIMIZED_FULL  1
#define ADS_OPTIMIZED_PART  2
#define ADS_AOF_ADD_RECORD  1

// Handle classification (AdsGetHandleType).
#define ADS_NONE              0
#define ADS_TABLE             1
#define ADS_STATEMENT         2
#define ADS_CURSOR            4
#define ADS_DATABASE_CONNECTION 6
#define ADS_SYS_ADMIN_CONNECTION 7

// Server / connection types (AdsGetConnectionType).
#define ADS_AIS_SERVER     1
#define ADS_LINUX          2
#define ADS_NOTIFICATION_CONNECTION 4
#define ADS_REPLICATION_CONNECTION  8
#define ADS_UDP_IP_CONNECTION       16
#define ADS_DEFAULT_SQL_TIMEOUT     30

// Locking models (AdsSetLockingMode).
#define ADS_PROPRIETARY_LOCKING  1
#define ADS_COMPATIBLE_LOCKING   2

// AdsGetTableFilename `usOption` values.
#define ADS_FULLPATHNAME    1
#define ADS_BASENAMEANDEXT  2
#define ADS_BASENAME        3

// SQL key types (AdsGetKeyType).
#define ADS_RAWKEY     0
#define ADS_STRINGKEY  1
#define ADS_DOUBLEKEY  2

// Misc query / filter flags.
#define ADS_DEFCONNECTION    1
#define ADS_GETCONNECTION    1
#define ADS_PUTCONNECTION    2
#define ADS_RETCONNECTION    3
#define ADS_PARCONNECTION    4
#define ADS_REFRESHCOUNT     1
#define ADS_GETFUNCTABLE     1
#define ADS_THREAD_DATA      1
#define ADS_TSD_CONNECTION   2
#define ADS_CONN_DATA        3

// Resolution / connection options.
#define ADS_RESOLVE_DYNAMIC   0
#define ADS_RESOLVE_IMMEDIATE 1
#define ADS_NOT_AUTO_OPEN     0x00000004
#define ADS_READ_ALL_COLUMNS  0x00000010
#define ADS_COMPRESS_ALWAYS   0x00000020
#define ADS_GET_FORMAT_WEB    0x00000040
#define ADS_GET_UTF8          0x00000080
#define ADS_ROOT_DD_ALIAS     0x00000100
// ADS_DD_VERSION* assigned >22 to avoid switch collisions with the
// ADS_DD_* string-property block below.
#define ADS_DD_VERSION                       23
#define ADS_DD_VERSION_MAJOR                 24
#define ADS_DD_VERSION_MINOR                 25
#define ADS_USER_DEFINED      0x00000200
// rddads.h defines ADS_USE_OEM_TRANSLATION conditionally; do not
// redefine here. ADS_VARCHAR / ADS_VARBINARY / ADS_CISTRING /
// ADS_RAW / ADS_COMPACTDATE etc. are defined above with their
// SAP-canonical numeric values.
#define ADS_CURSOR_READONLY   1
#define ADS_CS_AS_1252        1
#define ADS_VERSION_STRING    "1.0.0-rc6"

// Character-set table size limits.
#define ADS_MAX_CHAR_SETS         32
#define ADS_MAX_DBF_FIELD_NAME    10
#define ADS_MAX_FIELD_NAME       128
#define ADS_MAX_TABLE_NAME       255
#define ADS_MAX_TAG_NAME          10
#define ADS_MAX_KEY_LENGTH       240
// Guarded so callers (Harbour's adsfunc.c bumps to 2048) can
// pre-define a larger ceiling without -Wmacro-redefined noise.
#ifndef ADS_MAX_PARAMDEF_LEN
#  define ADS_MAX_PARAMDEF_LEN     256
#endif
#define ADS_MAX_ERROR_LEN        320

// Data Dictionary string-property keys.
#define ADS_DD_COMMENT                       1
#define ADS_DD_ADMIN_PASSWORD                2
#define ADS_DD_DEFAULT_TABLE_PATH            3
#define ADS_DD_TEMP_TABLE_PATH               4
#define ADS_DD_LOG_IN_REQUIRED               5
#define ADS_DD_VERIFY_ACCESS_RIGHTS          6
#define ADS_DD_ENCRYPT_NEW_TABLE             7
#define ADS_DD_ENCRYPT_TABLE_PASSWORD        8
#define ADS_DD_ENCRYPT_INDEXES               9
#define ADS_DD_ENCRYPT_COMMUNICATION         10
#define ADS_DD_ENCRYPTED                     11
#define ADS_DD_ENABLE_INTERNET               12
#define ADS_DD_INTERNET_SECURITY_LEVEL       13
#define ADS_DD_LOGINS_DISABLED               14
#define ADS_DD_LOGINS_DISABLED_ERRSTR        15
#define ADS_DD_DISABLE_DLL_CACHING           16
#define ADS_DD_FTS_DELIMITERS                17
#define ADS_DD_FTS_NOISE                     18
#define ADS_DD_FTS_DROP_CHARS                19
#define ADS_DD_FTS_CONDITIONAL_CHARS         20
#define ADS_DD_MAX_FAILED_ATTEMPTS           21
#define ADS_DD_USER_DEFINED_PROP             22

// Referential integrity action options (usUpdate / usDelete).
#define ADS_DD_RI_CASCADE                 1
#define ADS_DD_RI_RESTRICT                2
#define ADS_DD_RI_SETNULL                 3
#define ADS_DD_RI_SETDEFAULT              4

// User-object property codes (1101-1103).
#define ADS_DD_USER_PASSWORD              1101
#define ADS_DD_USER_GROUP_MEMBERSHIP      1102
#define ADS_DD_USER_BAD_LOGINS            1103

// Table-object property codes (200-216).
#define ADS_DD_TABLE_VALIDATION_EXPR         200
#define ADS_DD_TABLE_VALIDATION_MSG          201
#define ADS_DD_TABLE_PRIMARY_KEY             202
#define ADS_DD_TABLE_AUTO_CREATE             203
#define ADS_DD_TABLE_TYPE                    204
#define ADS_DD_TABLE_PATH                    205
#define ADS_DD_TABLE_FIELD_COUNT             206
#define ADS_DD_TABLE_RI_GRAPH                207
#define ADS_DD_TABLE_OBJ_ID                  208
#define ADS_DD_TABLE_RI_XY                   209
#define ADS_DD_TABLE_IS_RI_PARENT            210
#define ADS_DD_TABLE_RELATIVE_PATH           211
#define ADS_DD_TABLE_CHAR_TYPE               212
#define ADS_DD_TABLE_DEFAULT_INDEX           213
#define ADS_DD_TABLE_ENCRYPTION              214
#define ADS_DD_TABLE_MEMO_BLOCK_SIZE         215
#define ADS_DD_TABLE_PERMISSION_LEVEL        216

// Permission levels for AdsDDGet/SetUserTableRights.
#define ADS_DD_TABLE_PERMISSION_NONE         0
#define ADS_DD_TABLE_PERMISSION_READ         1
#define ADS_DD_TABLE_PERMISSION_WRITE        2
#define ADS_DD_TABLE_PERMISSION_DELETE       3
#define ADS_DD_TABLE_PERMISSION_FULL         4

// Field-object property codes (301-309).
#define ADS_DD_FIELD_NAME                    301
#define ADS_DD_FIELD_TYPE                    302
#define ADS_DD_FIELD_LENGTH                  303
#define ADS_DD_FIELD_DECIMAL                 304
#define ADS_DD_FIELD_REQUIRED                305
#define ADS_DD_FIELD_DEFAULT                 306
#define ADS_DD_FIELD_VALIDATION_RULE         307
#define ADS_DD_FIELD_VALIDATION_MSG          308
#define ADS_DD_FIELD_COMMENT                 309

// Index-object property codes (401-408).
#define ADS_DD_INDEX_FILE_NAME               401
#define ADS_DD_INDEX_EXPR                    402
#define ADS_DD_INDEX_UNIQUE                  403
#define ADS_DD_INDEX_DESCENDING              404
#define ADS_DD_INDEX_CONDITION               405
#define ADS_DD_INDEX_KEY_LENGTH              406
#define ADS_DD_INDEX_TYPE                    407
#define ADS_DD_INDEX_FILE_TYPE               408

// Trigger event-mask bits (used in AdsDDCreateTrigger ulType).
#define ADS_BEFORE_INSERT                    0x0001
#define ADS_AFTER_INSERT                     0x0002
#define ADS_BEFORE_UPDATE                    0x0004
#define ADS_AFTER_UPDATE                     0x0008
#define ADS_BEFORE_DELETE                    0x0010
#define ADS_AFTER_DELETE                     0x0020
// INSTEAD OF variants (OpenADS extension — not in SAP ACE).
#define ADS_INSTEAD_OF_INSERT                0x0040
#define ADS_INSTEAD_OF_UPDATE                0x0080
#define ADS_INSTEAD_OF_DELETE                0x0100

// RI property codes (401-407).
#define ADS_DD_RI_PARENT                     401
#define ADS_DD_RI_CHILD                      402
#define ADS_DD_RI_PARENT_TAG                 403
#define ADS_DD_RI_CHILD_TAG                  404
#define ADS_DD_RI_UPDATE_RULE                405
#define ADS_DD_RI_DELETE_RULE                406
#define ADS_DD_RI_FAIL_TABLE                 407

// Trigger-object property codes (501-508).
#define ADS_DD_TRIGGER_TABLE                 501
#define ADS_DD_TRIGGER_EVENT                 502
#define ADS_DD_TRIGGER_CONTAINER             503
#define ADS_DD_TRIGGER_PROC_NAME             504
#define ADS_DD_TRIGGER_ENABLED               505
#define ADS_DD_TRIGGER_PRIORITY              506
#define ADS_DD_TRIGGER_COMMENT               507

// Stored-procedure property codes (601-605).
#define ADS_DD_PROC_INPUT                    601
#define ADS_DD_PROC_OUTPUT                   602
#define ADS_DD_PROC_CONTAINER                603
#define ADS_DD_PROC_PROC_NAME                604
#define ADS_DD_PROC_COMMENT                  605

// View property codes (701-702).
#define ADS_DD_VIEW_STMT                     701
#define ADS_DD_VIEW_COMMENT                  702

// SAP ACE property constant aliases — SAP ace.h uses different names for some
// of these constants.  Only the names that differ from OpenADS are defined here.
#ifndef ADS_DD_PROC_DLL_NAME
#  define ADS_DD_PROC_DLL_NAME           ADS_DD_PROC_CONTAINER   /* 603 */
#  define ADS_DD_PROC_DLL_FUNCTION_NAME  ADS_DD_PROC_PROC_NAME   /* 604 */
#  define ADS_DD_PROC_INVOKE_OPTION      604  /* not stored; ignored on set */
#  define ADS_DD_PROC_SCRIPT             ADS_DD_PROC_COMMENT     /* 605 */
#endif
#ifndef ADS_DD_TRIG_EVENT_TYPE
#  define ADS_DD_TRIG_TABLEID            ADS_DD_TRIGGER_TABLE    /* 501 */
#  define ADS_DD_TRIG_EVENT_TYPE         ADS_DD_TRIGGER_EVENT    /* 502 */
#  define ADS_DD_TRIG_CONTAINER          ADS_DD_TRIGGER_CONTAINER /* 503 */
#  define ADS_DD_TRIG_FUNCTION_NAME      ADS_DD_TRIGGER_PROC_NAME /* 504 */
#  define ADS_DD_TRIG_PRIORITY           ADS_DD_TRIGGER_PRIORITY  /* 506 */
#  define ADS_DD_TRIG_TABLENAME          ADS_DD_TRIGGER_TABLE     /* 501 */
#endif

// Note: SAP's ace.h uses the ADS_MGMT_* names for management-info
// struct typedefs (declared further below), not for numeric
// selectors. The earlier integer #defines here would collide with
// those typedefs, so they're removed. Lock-type constants live
// elsewhere.
#define ADS_MGMT_FILE_LOCK         1
#define ADS_MGMT_RECORD_LOCK       2
#define ADS_MGMT_NO_LOCK           3

// AE_* error code macros. The canonical enum lives in
// openads/error.h, but rddads includes only ace.h so these need
// preprocessor visibility too. error.h #undef's the duplicates
// before declaring its enum, so both surfaces stay coherent.
#ifndef AE_SUCCESS
#  define AE_SUCCESS               0
#endif
#define AE_INTERNAL_ERROR          5000
#define AE_FUNCTION_NOT_AVAILABLE  5004
#define AE_LOCKED                  5012
#define AE_LOCK_FAILED             5013
#define AE_NO_FILE_FOUND           5018
#define AE_NO_CONNECTION           5036
#define AE_INVALID_CONNECTION_HANDLE 4097

// Extra AE_* error codes referenced by Harbour rddads.
#define AE_INVALID_HANDLE          5024
#define AE_INVALID_RECORD_NUMBER   5025
// 5026 = AE_INVALID_WORKAREA in the SAP ADS SDK. It was previously
// (incorrectly) labelled AE_NO_CURRENT_RECORD here. The correct value
// for "no current record" per the ADS SDK is 5068. Harbour's rddads
// contrib driver (contrib/rddads/ads1.c) special-cases exactly 5068 to
// substitute blank-typed field values when the cursor is at BOF or EOF;
// any other error code — including 5026 — is raised as a hard runtime
// error. Using 5026 here therefore caused hard errors in any application
// that read fields while navigating past the record set (TBrowse painting,
// FOR...NEXT loops at EOF, WHILE .NOT. EOF() patterns, etc.).
#define AE_INVALID_WORKAREA        5026
#define AE_NO_CURRENT_RECORD       5068
#define AE_TABLE_NOT_LOCKED        5034
#define AE_RECORD_NOT_LOCKED       5035
#define AE_TABLE_NOT_SHARED        5036
#define AE_TABLE_READONLY          5037
#define AE_INSUFFICIENT_BUFFER     5051
#define AE_DATA_TOO_LONG           5054
#define AE_DATA_TRUNCATED          5055
#define AE_VALUE_OVERFLOW          5057
#define AE_INVALID_EXPRESSION      5079
#define AE_INDEX_ALREADY_OPEN      5108
#define AE_SAP_PERMS_NEED_IMPORT   5174  /* DD has SAP-format permissions; run import tool */

// ---- Management API struct typedefs (Harbour contrib/rddads) ----
// Field names taken from public Sybase / SAP ace.h documentation;
// rddads' adsmgmnt.c reads these field-by-field. OpenADS' local
// implementation always returns zero-filled buffers (no actual
// server-side stats), but Harbour code that uses Mg* still has to
// link, so the structs must exist and have compatible field
// shapes.

typedef struct _ADS_MGMT_TIME_STRUCT {
    UNSIGNED16 usDays;
    UNSIGNED16 usHours;
    UNSIGNED16 usMinutes;
    UNSIGNED16 usSeconds;
} ADS_MGMT_TIME_STRUCT;

typedef struct _ADS_MGMT_USAGE_STRUCT {
    UNSIGNED32 ulInUse;
    UNSIGNED32 ulMaxUsed;
    UNSIGNED32 ulRejected;
} ADS_MGMT_USAGE_STRUCT;

typedef struct _ADS_MGMT_INSTALL_INFO {
    UNSIGNED32 ulUserOption;
    UNSIGNED8  aucRegisteredOwner [128];
    UNSIGNED8  aucVersionStr      [16];
    UNSIGNED8  aucInstallDate     [32];
    UNSIGNED8  aucOemCharName     [32];
    UNSIGNED8  aucAnsiCharName    [32];
    UNSIGNED8  aucEvalExpireDate  [32];
    UNSIGNED8  aucSerialNumber    [32];
} ADS_MGMT_INSTALL_INFO;

typedef struct _ADS_MGMT_ACTIVITY_INFO {
    UNSIGNED32             ulOperations;
    UNSIGNED32             ulLoggedErrors;
    ADS_MGMT_TIME_STRUCT   stUpTime;
    ADS_MGMT_USAGE_STRUCT  stUsers;
    ADS_MGMT_USAGE_STRUCT  stConnections;
    ADS_MGMT_USAGE_STRUCT  stWorkAreas;
    ADS_MGMT_USAGE_STRUCT  stTables;
    ADS_MGMT_USAGE_STRUCT  stIndexes;
    ADS_MGMT_USAGE_STRUCT  stLocks;
    ADS_MGMT_USAGE_STRUCT  stTpsHeaderElems;
    ADS_MGMT_USAGE_STRUCT  stTpsVisElems;
    ADS_MGMT_USAGE_STRUCT  stTpsMemoElems;
    ADS_MGMT_USAGE_STRUCT  stWorkerThreads;
} ADS_MGMT_ACTIVITY_INFO;

typedef struct _ADS_MGMT_COMM_STATS {
    double      dPercentCheckSums;
    UNSIGNED32  ulTotalPackets;
    UNSIGNED32  ulRcvPktOutOfSeq;
    UNSIGNED32  ulNotLoggedIn;
    UNSIGNED32  ulRcvReqOutOfSeq;
    UNSIGNED32  ulCheckSumFailures;
    UNSIGNED32  ulDisconnectedUsers;
    UNSIGNED32  ulPartialConnects;
    UNSIGNED32  ulInvalidPackets;
    UNSIGNED32  ulRecvFromErrors;
    UNSIGNED32  ulSendToErrors;
} ADS_MGMT_COMM_STATS;

typedef struct _ADS_MGMT_USER_INFO {
    UNSIGNED8   aucUserName        [32];
    UNSIGNED16  usConnNumber;
    UNSIGNED8   aucAddress         [64];
    UNSIGNED8   aucTSAddress       [64];
    UNSIGNED8   aucOSUserLoginName [64];
    UNSIGNED8   aucAuthUserName    [64];
} ADS_MGMT_USER_INFO;

typedef struct _ADS_MGMT_THREAD_ACTIVITY {
    UNSIGNED32  ulThreadNumber;
    UNSIGNED16  usOpCode;
    UNSIGNED8   aucUserName        [32];
    UNSIGNED16  usConnNumber;
    UNSIGNED16  usReserved1;
    UNSIGNED8   aucOSUserLoginName [64];
} ADS_MGMT_THREAD_ACTIVITY;

typedef struct _ADS_MGMT_LOCK_INFO {
    UNSIGNED8   aucUserName        [32];
    UNSIGNED16  usConnNumber;
    UNSIGNED32  ulRecordNumber;
} ADS_MGMT_LOCK_INFO;

typedef struct _ADS_MGMT_TABLE_INFO {
    UNSIGNED8   aucTableName       [256];
    UNSIGNED8   aucUserName        [32];
    UNSIGNED16  usConnNumber;
    UNSIGNED16  usOpenMode;
    UNSIGNED16  usLockType;
} ADS_MGMT_TABLE_INFO;

typedef struct _ADS_MGMT_INDEX_INFO {
    UNSIGNED8   aucIndexName       [256];
    UNSIGNED8   aucTagName         [16];
    UNSIGNED8   aucExpression      [256];
} ADS_MGMT_INDEX_INFO;

typedef struct _ADS_MGMT_RECORD_INFO {
    UNSIGNED32  ulRecordNumber;
    UNSIGNED8   aucUserName        [32];
} ADS_MGMT_RECORD_INFO;

typedef struct _ADS_MGMT_CONFIG_PARAMS {
    UNSIGNED32  ulNumConnections;
    UNSIGNED32  ulNumWorkAreas;
    UNSIGNED32  ulNumTables;
    UNSIGNED32  ulNumIndexes;
    UNSIGNED32  ulNumLocks;
    UNSIGNED32  ulUserBufferSize;
    UNSIGNED32  ulStatDumpInterval;
    UNSIGNED32  ulErrorLogMax;
    UNSIGNED32  ulNumTPSHeaderElems;
    UNSIGNED32  ulNumTPSVisibilityElems;
    UNSIGNED32  ulNumTPSMemoTransElems;
    UNSIGNED16  usNumReceiveECBs;
    UNSIGNED16  usNumSendECBs;
    UNSIGNED16  usNumBurstPackets;
    UNSIGNED16  usNumWorkerThreads;
    UNSIGNED32  ulSortBuffSize;
    UNSIGNED16  usSortBuffSize;
    UNSIGNED8   ucReserved1;
    UNSIGNED8   ucReserved2;
    UNSIGNED8   aucErrorLog       [256];
    UNSIGNED8   aucSemaphore      [256];
    UNSIGNED8   aucTransaction    [256];
    UNSIGNED8   ucReserved3;
    UNSIGNED8   ucReserved4;
    UNSIGNED16  usSendIPPort;
    UNSIGNED16  usReceiveIPPort;
    UNSIGNED16  usReserved5;
} ADS_MGMT_CONFIG_PARAMS;

typedef struct _ADS_MGMT_CONFIG_MEMORY {
    double      ulTotalConfigMem;
    UNSIGNED32  ulConnectionMem;
    UNSIGNED32  ulWorkAreaMem;
    UNSIGNED32  ulTableMem;
    UNSIGNED32  ulIndexMem;
    UNSIGNED32  ulLockMem;
    UNSIGNED32  ulUserBufferMem;
    UNSIGNED32  ulTPSHeaderElemMem;
    UNSIGNED32  ulTPSVisibilityElemMem;
    UNSIGNED32  ulTPSMemoTransElemMem;
    UNSIGNED32  ulReceiveEcbMem;
    UNSIGNED32  ulSendEcbMem;
    UNSIGNED32  ulWorkerThreadMem;
} ADS_MGMT_CONFIG_MEMORY;

// ---- Function declarations required by Harbour contrib/rddads ----
// Many of these are already implemented in src/abi/ace_exports.cpp;
// the declarations are added here so the standard rddads contrib
// builds against this header. Functions whose engine support is not
// yet available return AE_FUNCTION_NOT_AVAILABLE (5004).

UNSIGNED32 ENTRYPOINT AdsConnect           (UNSIGNED8* pucServer,
                                             ADSHANDLE* phConnect);
UNSIGNED32 ENTRYPOINT AdsApplicationExit   (void);
UNSIGNED32 ENTRYPOINT AdsClearFilter       (ADSHANDLE hTable);
UNSIGNED32 ENTRYPOINT AdsClearRelation     (ADSHANDLE hTable);
UNSIGNED32 ENTRYPOINT AdsClearCallbackFunction(void);
UNSIGNED32 ENTRYPOINT AdsClearProgressCallback(void);
UNSIGNED32 ENTRYPOINT AdsCacheOpenCursors  (UNSIGNED16 usCacheCount);
UNSIGNED32 ENTRYPOINT AdsCacheOpenTables   (UNSIGNED16 usCacheCount);
UNSIGNED32 ENTRYPOINT AdsCacheRecords      (ADSHANDLE hTable,
                                             UNSIGNED16 usRecCount);
UNSIGNED32 ENTRYPOINT AdsCloseCachedTables (ADSHANDLE hConnect);
UNSIGNED32 ENTRYPOINT AdsCopyTableContent  (ADSHANDLE hSrc, ADSHANDLE hDst);
UNSIGNED32 ENTRYPOINT AdsCustomizeAOF      (ADSHANDLE hTable,
                                             UNSIGNED32 ulNumRecords,
                                             UNSIGNED32* pulRecords,
                                             UNSIGNED16 usOption);
UNSIGNED32 ENTRYPOINT AdsData              (UNSIGNED16 usFlag,
                                             void* pvData);
UNSIGNED32 ENTRYPOINT AdsEvalAOF           (ADSHANDLE hTable,
                                             UNSIGNED8* pucExpr,
                                             UNSIGNED16* pusOptLevel);
UNSIGNED32 ENTRYPOINT AdsFilterOption      (ADSHANDLE hTable,
                                             UNSIGNED16 usOption,
                                             UNSIGNED16* pusValue);
UNSIGNED32 ENTRYPOINT AdsGetAOF            (ADSHANDLE hTable,
                                             UNSIGNED8* pucFilter,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetConnectionType (ADSHANDLE hConnect,
                                             UNSIGNED16* pusType);
UNSIGNED32 ENTRYPOINT AdsGetDateFormat     (UNSIGNED8* pucBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetDefault        (UNSIGNED8* pucBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetDeleted        (UNSIGNED16* pbShow);
UNSIGNED32 ENTRYPOINT AdsGetDouble         (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             double* pdValue);
UNSIGNED32 ENTRYPOINT AdsGetEpoch          (UNSIGNED16* pusEpoch);
UNSIGNED32 ENTRYPOINT AdsGetErrorString    (UNSIGNED32 ulErr,
                                             UNSIGNED8* pucBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetExact          (UNSIGNED16* pbExact);
UNSIGNED32 ENTRYPOINT AdsGetFieldDecimals  (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             UNSIGNED16* pusDec);
UNSIGNED32 ENTRYPOINT AdsGetFieldRaw       (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             UNSIGNED8* pucBuf,
                                             UNSIGNED32* pulLen);
UNSIGNED32 ENTRYPOINT AdsGetFilter         (ADSHANDLE hTable,
                                             UNSIGNED8* pucBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetHandleType     (ADSHANDLE hAny,
                                             UNSIGNED16* pusType);
UNSIGNED32 ENTRYPOINT AdsGetIndexCondition (ADSHANDLE hIndex,
                                             UNSIGNED8* pucBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetIndexFilename  (ADSHANDLE hIndex,
                                             UNSIGNED16 usOption,
                                             UNSIGNED8* pucBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetIndexOrderByHandle(ADSHANDLE hIndex,
                                             UNSIGNED16* pusOrder);
// M12.16c — flip the active order on a table to the named tag /
// the handle returned by a previous AdsOpenIndex / AdsCreateIndex.
// rddads' adsOrdSetActive uses these for dbSetOrder( cTagName )
// and dbSetOrder( hIndex ).
UNSIGNED32 ENTRYPOINT AdsSetIndexOrder         (ADSHANDLE hTable,
                                             UNSIGNED8* pucName);
UNSIGNED32 ENTRYPOINT AdsSetIndexOrderByHandle (ADSHANDLE hTable,
                                             ADSHANDLE hIndex);
UNSIGNED32 ENTRYPOINT AdsGetJulian         (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             SIGNED32* plJulian);
UNSIGNED32 ENTRYPOINT AdsGetKeyLength      (ADSHANDLE hIndex,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetKeyNum         (ADSHANDLE hIndex,
                                             UNSIGNED16 usFlag,
                                             UNSIGNED32* pulKey);
UNSIGNED32 ENTRYPOINT AdsGetKeyType        (ADSHANDLE hIndex,
                                             UNSIGNED16* pusType);
UNSIGNED32 ENTRYPOINT AdsGetLastTableUpdate(ADSHANDLE hTable,
                                             UNSIGNED8* pucDate,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetLogical        (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             UNSIGNED16* pbValue);
UNSIGNED32 ENTRYPOINT AdsGetMilliseconds   (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             SIGNED32* plMs);
UNSIGNED32 ENTRYPOINT AdsGetNumActiveLinks (ADSHANDLE hTable,
                                             UNSIGNED16* pusCount);
UNSIGNED32 ENTRYPOINT AdsGetNumLocks       (ADSHANDLE hTable,
                                             UNSIGNED16* pusCount);
UNSIGNED32 ENTRYPOINT AdsGetNumOpenTables  (UNSIGNED16* pusCount);
UNSIGNED32 ENTRYPOINT AdsGetRecord         (ADSHANDLE hTable,
                                             UNSIGNED8* pucBuf,
                                             UNSIGNED32* pulLen);
UNSIGNED32 ENTRYPOINT AdsGetRelKeyPos      (ADSHANDLE hIndex,
                                             double* pdPos);
UNSIGNED32 ENTRYPOINT AdsGetSearchPath     (UNSIGNED8* pucBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetTableAlias     (ADSHANDLE hTable,
                                             UNSIGNED8* pucBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetTableCharType  (ADSHANDLE hTable,
                                             UNSIGNED16* pusType);
UNSIGNED32 ENTRYPOINT AdsGetTableConType   (ADSHANDLE hTable,
                                             UNSIGNED16* pusType);
UNSIGNED32 ENTRYPOINT AdsGetTableConnection(ADSHANDLE hTable,
                                             ADSHANDLE* phConnect);
UNSIGNED32 ENTRYPOINT AdsIsConnectionAlive (ADSHANDLE hConnect,
                                             UNSIGNED16* pbAlive);
UNSIGNED32 ENTRYPOINT AdsIsEmpty           (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             UNSIGNED16* pbEmpty);
UNSIGNED32 ENTRYPOINT AdsIsExprValid       (ADSHANDLE hTable,
                                             UNSIGNED8* pucExpr,
                                             UNSIGNED16* pbValid);
UNSIGNED32 ENTRYPOINT AdsIsFound           (ADSHANDLE hTable,
                                             UNSIGNED16* pbFound);
UNSIGNED32 ENTRYPOINT AdsIsIndexCustom     (ADSHANDLE hIndex,
                                             UNSIGNED16* pbCustom);
UNSIGNED32 ENTRYPOINT AdsIsIndexDescending (ADSHANDLE hIndex,
                                             UNSIGNED16* pbDesc);
UNSIGNED32 ENTRYPOINT AdsIsIndexUnique     (ADSHANDLE hIndex,
                                             UNSIGNED16* pbUnique);
UNSIGNED32 ENTRYPOINT AdsIsNull            (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             UNSIGNED16* pbNull);
UNSIGNED32 ENTRYPOINT AdsIsRecordInAOF     (ADSHANDLE hTable,
                                             UNSIGNED32 ulRecord,
                                             UNSIGNED16* pbInAOF);
UNSIGNED32 ENTRYPOINT AdsIsRecordLocked    (ADSHANDLE hTable,
                                             UNSIGNED32 ulRecord,
                                             UNSIGNED16* pbLocked);
UNSIGNED32 ENTRYPOINT AdsIsServerLoaded    (UNSIGNED8* pucServer,
                                             UNSIGNED16* pbLoaded);
UNSIGNED32 ENTRYPOINT AdsIsTableLocked     (ADSHANDLE hTable,
                                             UNSIGNED16* pbLocked);
UNSIGNED32 ENTRYPOINT AdsRefreshAOF        (ADSHANDLE hTable);
UNSIGNED32 ENTRYPOINT AdsRegisterCallbackFunction(void* pCallback);
UNSIGNED32 ENTRYPOINT AdsRegisterProgressCallback(void* pCallback);
UNSIGNED32 ENTRYPOINT AdsSetDateFormat     (UNSIGNED8* pucFormat);
UNSIGNED32 ENTRYPOINT AdsSetDecimals       (UNSIGNED16 usDecimals);
UNSIGNED32 ENTRYPOINT AdsSetDefault        (UNSIGNED8* pucDir);
UNSIGNED32 ENTRYPOINT AdsSetEpoch          (UNSIGNED16 usEpoch);
UNSIGNED32 ENTRYPOINT AdsSetExact          (UNSIGNED16 bExact);
UNSIGNED32 ENTRYPOINT AdsSetFilter         (ADSHANDLE hTable,
                                             UNSIGNED8* pucExpr);
UNSIGNED32 ENTRYPOINT AdsSetJulian         (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             SIGNED32 lJulian);
UNSIGNED32 ENTRYPOINT AdsSetLongLong       (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             int64_t llValue);
UNSIGNED32 ENTRYPOINT AdsSetMilliseconds   (ADSHANDLE hTable,
                                             UNSIGNED8* pucField,
                                             SIGNED32 lMs);
UNSIGNED32 ENTRYPOINT AdsSetRecord         (ADSHANDLE hTable,
                                             UNSIGNED8* pucBuf,
                                             UNSIGNED32 ulLen);
UNSIGNED32 ENTRYPOINT AdsSetRelKeyPos      (ADSHANDLE hIndex, double dPos);
UNSIGNED32 ENTRYPOINT AdsSetRelation       (ADSHANDLE hParent,
                                             ADSHANDLE hChild,
                                             UNSIGNED8* pucExpr);
UNSIGNED32 ENTRYPOINT AdsSetScopedRelation (ADSHANDLE hParent,
                                             ADSHANDLE hChild,
                                             UNSIGNED8* pucExpr);
UNSIGNED32 ENTRYPOINT AdsSetSearchPath     (UNSIGNED8* pucPath);
UNSIGNED32 ENTRYPOINT AdsSetServerType     (UNSIGNED16 usType);
UNSIGNED32 ENTRYPOINT AdsShowDeleted       (UNSIGNED16 bShow);
UNSIGNED32 ENTRYPOINT AdsShowError         (UNSIGNED8* pucCaption);
UNSIGNED32 ENTRYPOINT AdsStmtSetTableLockType(ADSHANDLE hStmt,
                                             UNSIGNED16 usType);
UNSIGNED32 ENTRYPOINT AdsStmtSetTablePassword(ADSHANDLE hStmt,
                                             UNSIGNED8* pucName,
                                             UNSIGNED8* pucPwd);
UNSIGNED32 ENTRYPOINT AdsStmtSetTableReadOnly(ADSHANDLE hStmt,
                                             UNSIGNED16 bReadOnly);
UNSIGNED32 ENTRYPOINT AdsStmtSetTableType  (ADSHANDLE hStmt,
                                             UNSIGNED16 usType);
UNSIGNED32 ENTRYPOINT AdsTestLogin         (UNSIGNED8* pucServer,
                                             UNSIGNED16 usServerType,
                                             UNSIGNED8* pucUser,
                                             UNSIGNED8* pucPwd,
                                             UNSIGNED32 ulOptions);
UNSIGNED32 ENTRYPOINT AdsTestRecLocks      (ADSHANDLE hTable);
UNSIGNED32 ENTRYPOINT AdsWriteAllRecords   (void);

// Management API stubs — wire-protocol-level operations. OpenADS local
// mode returns AE_FUNCTION_NOT_AVAILABLE; the server build will route
// these to the corresponding TCP frames.
UNSIGNED32 ENTRYPOINT AdsMgConnect         (UNSIGNED8* pucServer,
                                             UNSIGNED8* pucUser,
                                             UNSIGNED8* pucPwd,
                                             ADSHANDLE* phMg);
UNSIGNED32 ENTRYPOINT AdsMgDisconnect      (ADSHANDLE hMg);
UNSIGNED32 ENTRYPOINT AdsMgGetActivityInfo (ADSHANDLE hMg, void* pInfo,
                                             UNSIGNED16* pusSize);
UNSIGNED32 ENTRYPOINT AdsMgGetCommStats    (ADSHANDLE hMg, void* pInfo,
                                             UNSIGNED16* pusSize);
UNSIGNED32 ENTRYPOINT AdsMgGetConfigInfo   (ADSHANDLE hMg,
                                             void* pVals,
                                             UNSIGNED16* pusValsSize,
                                             void* pMem,
                                             UNSIGNED16* pusMemSize);
UNSIGNED32 ENTRYPOINT AdsMgGetInstallInfo  (ADSHANDLE hMg, void* pInfo,
                                             UNSIGNED16* pusSize);
UNSIGNED32 ENTRYPOINT AdsMgGetLockOwner    (ADSHANDLE hMg,
                                             UNSIGNED8* pucTable,
                                             UNSIGNED32 ulRecord,
                                             void* pInfo,
                                             UNSIGNED16* pusSize,
                                             UNSIGNED16* pusLockType);
UNSIGNED32 ENTRYPOINT AdsMgGetLocks        (ADSHANDLE hMg,
                                             UNSIGNED8* pucTable,
                                             UNSIGNED8* pucUser,
                                             UNSIGNED16 usConnNumber,
                                             void* pInfo,
                                             UNSIGNED16* pusCount,
                                             UNSIGNED16* pusSize);
UNSIGNED32 ENTRYPOINT AdsMgGetOpenIndexes  (ADSHANDLE hMg,
                                             UNSIGNED8* pucTable,
                                             UNSIGNED8* pucUser,
                                             UNSIGNED16 usConnNumber,
                                             void* pInfo,
                                             UNSIGNED16* pusCount,
                                             UNSIGNED16* pusSize);
UNSIGNED32 ENTRYPOINT AdsMgGetOpenTables   (ADSHANDLE hMg,
                                             UNSIGNED8* pucUser,
                                             UNSIGNED16 usConnNumber,
                                             void* pInfo,
                                             UNSIGNED16* pusCount,
                                             UNSIGNED16* pusSize);
UNSIGNED32 ENTRYPOINT AdsMgGetOpenTables2  (ADSHANDLE hMg,
                                             UNSIGNED8* pucUser,
                                             UNSIGNED16 usConnNumber,
                                             void* pInfo,
                                             UNSIGNED16* pusCount,
                                             UNSIGNED16* pusSize);
UNSIGNED32 ENTRYPOINT AdsMgGetServerType   (ADSHANDLE hMg, UNSIGNED16* pusT);
UNSIGNED32 ENTRYPOINT AdsMgGetUserNames    (ADSHANDLE hMg,
                                             UNSIGNED8* pucFile,
                                             void* pInfo,
                                             UNSIGNED16* pusCount,
                                             UNSIGNED16* pusSize);
UNSIGNED32 ENTRYPOINT AdsMgGetWorkerThreadActivity(ADSHANDLE hMg, void* pInfo,
                                             UNSIGNED16* pusCount,
                                             UNSIGNED16* pusSize);
UNSIGNED32 ENTRYPOINT AdsMgKillUser        (ADSHANDLE hMg, UNSIGNED8* pucUser,
                                             UNSIGNED16 usOption);
UNSIGNED32 ENTRYPOINT AdsMgResetCommStats  (ADSHANDLE hMg);

// Data-dictionary helpers used by rddads.
UNSIGNED32 ENTRYPOINT AdsDDAddIndexFile    (ADSHANDLE hConnect,
                                             UNSIGNED8* pucTable,
                                             UNSIGNED8* pucIndex,
                                             UNSIGNED8* pucComment);
UNSIGNED32 ENTRYPOINT AdsDDAddUserToGroup  (ADSHANDLE hConnect,
                                             UNSIGNED8* pucGroup,
                                             UNSIGNED8* pucUser);
UNSIGNED32 ENTRYPOINT AdsDDCreateLink      (ADSHANDLE hConnect,
                                             UNSIGNED8* pucLink,
                                             UNSIGNED8* pucPath,
                                             UNSIGNED8* pucUser,
                                             UNSIGNED8* pucPwd,
                                             UNSIGNED16 usOptions);
UNSIGNED32 ENTRYPOINT AdsDDCreateRefIntegrity(ADSHANDLE hConnect,
                                             UNSIGNED8* pucName,
                                             UNSIGNED8* pucFail,
                                             UNSIGNED8* pucParent,
                                             UNSIGNED8* pucParentTag,
                                             UNSIGNED8* pucChild,
                                             UNSIGNED8* pucChildTag,
                                             UNSIGNED16 usUpdate,
                                             UNSIGNED16 usDelete);
UNSIGNED32 ENTRYPOINT AdsDDCreateUser      (ADSHANDLE hConnect,
                                             UNSIGNED8* pucGroup,
                                             UNSIGNED8* pucUser,
                                             UNSIGNED8* pucPwd,
                                             UNSIGNED8* pucComment);
UNSIGNED32 ENTRYPOINT AdsDDDeleteUser      (ADSHANDLE hConnect,
                                             UNSIGNED8* pucUser);
UNSIGNED32 ENTRYPOINT AdsDDDropLink        (ADSHANDLE hConnect,
                                             UNSIGNED8* pucLink,
                                             UNSIGNED16 usOptions);
UNSIGNED32 ENTRYPOINT AdsDDGetDatabaseProperty(ADSHANDLE hConnect,
                                             UNSIGNED16 usProp,
                                             void* pvBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsDDGetUserProperty (ADSHANDLE hConnect,
                                             UNSIGNED8* pucUser,
                                             UNSIGNED16 usProp,
                                             void* pvBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsDDModifyLink      (ADSHANDLE hConnect,
                                             UNSIGNED8* pucLink,
                                             UNSIGNED8* pucPath,
                                             UNSIGNED8* pucUser,
                                             UNSIGNED8* pucPwd,
                                             UNSIGNED16 usOptions);
UNSIGNED32 ENTRYPOINT AdsDDRemoveIndexFile (ADSHANDLE hConnect,
                                             UNSIGNED8* pucTable,
                                             UNSIGNED8* pucIndex,
                                             UNSIGNED16 usOptions);
UNSIGNED32 ENTRYPOINT AdsDDRemoveRefIntegrity(ADSHANDLE hConnect,
                                             UNSIGNED8* pucRI);
UNSIGNED32 ENTRYPOINT AdsDDRemoveUserFromGroup(ADSHANDLE hConnect,
                                             UNSIGNED8* pucGroup,
                                             UNSIGNED8* pucUser);
UNSIGNED32 ENTRYPOINT AdsDDSetDatabaseProperty(ADSHANDLE hConnect,
                                             UNSIGNED16 usProp,
                                             void* pvBuf,
                                             UNSIGNED16 usLen);
UNSIGNED32 ENTRYPOINT AdsDDSetUserProperty  (ADSHANDLE hConnect,
                                             UNSIGNED8* pucUser,
                                             UNSIGNED16 usProp,
                                             void* pvBuf,
                                             UNSIGNED16 usLen);
UNSIGNED32 ENTRYPOINT AdsDDGetTableProperty  (ADSHANDLE hConnect,
                                             UNSIGNED8* pucTable,
                                             UNSIGNED16 usProp,
                                             void* pvBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsDDSetTableProperty  (ADSHANDLE hConnect,
                                             UNSIGNED8* pucTable,
                                             UNSIGNED16 usProp,
                                             void* pvBuf,
                                             UNSIGNED16 usLen);

UNSIGNED32 ENTRYPOINT AdsDDSetUserTableRights(ADSHANDLE hConnect,
                                             UNSIGNED8* pucTable,
                                             UNSIGNED8* pucUser,
                                             UNSIGNED32 ulLevel);
UNSIGNED32 ENTRYPOINT AdsDDGetUserTableRights(ADSHANDLE hConnect,
                                             UNSIGNED8* pucTable,
                                             UNSIGNED8* pucUser,
                                             UNSIGNED32* pulLevel);

UNSIGNED32 ENTRYPOINT AdsDDGetFieldProperty (ADSHANDLE hConnect,
                                             UNSIGNED8* pucTable,
                                             UNSIGNED8* pucField,
                                             UNSIGNED16 usProp,
                                             void*      pvBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsDDSetFieldProperty (ADSHANDLE hConnect,
                                             UNSIGNED8* pucTable,
                                             UNSIGNED8* pucField,
                                             UNSIGNED16 usProp,
                                             void*      pvBuf,
                                             UNSIGNED16 usLen);

UNSIGNED32 ENTRYPOINT AdsDDGetIndexProperty (ADSHANDLE hConnect,
                                             UNSIGNED8* pucTable,
                                             UNSIGNED8* pucIndex,
                                             UNSIGNED16 usProp,
                                             void*      pvBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsDDSetIndexProperty (ADSHANDLE hConnect,
                                             UNSIGNED8* pucTable,
                                             UNSIGNED8* pucIndex,
                                             UNSIGNED16 usProp,
                                             void*      pvBuf,
                                             UNSIGNED16 usLen);

UNSIGNED32 ENTRYPOINT AdsDDCreateTrigger    (ADSHANDLE hConnect,
                                             UNSIGNED8* pucName,
                                             UNSIGNED8* pucTable,
                                             UNSIGNED32 ulType,
                                             UNSIGNED32 ulOptions,
                                             UNSIGNED8* pucContainer,
                                             UNSIGNED8* pucProcedure,
                                             UNSIGNED32 ulPriority);
UNSIGNED32 ENTRYPOINT AdsDDDropTrigger      (ADSHANDLE hConnect,
                                             UNSIGNED8* pucName);
UNSIGNED32 ENTRYPOINT AdsDDGetTriggerProperty(ADSHANDLE hConnect,
                                             UNSIGNED8* pucName,
                                             UNSIGNED16 usProp,
                                             void*      pvBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsDDSetTriggerProperty(ADSHANDLE hConnect,
                                             UNSIGNED8* pucName,
                                             UNSIGNED16 usProp,
                                             void*      pvBuf,
                                             UNSIGNED16 usLen);

UNSIGNED32 ENTRYPOINT AdsDDCreateProcedure  (ADSHANDLE hConnect,
                                             UNSIGNED8* pucName,
                                             UNSIGNED8* pucContainer,
                                             UNSIGNED8* pucProcName,
                                             UNSIGNED32 ulInvokeOption,
                                             UNSIGNED8* pucInParams,
                                             UNSIGNED8* pucOutParams,
                                             UNSIGNED8* pucComments);
UNSIGNED32 ENTRYPOINT AdsDDDropProcedure    (ADSHANDLE hConnect,
                                             UNSIGNED8* pucName);
UNSIGNED32 ENTRYPOINT AdsDDGetProcProperty  (ADSHANDLE hConnect,
                                             UNSIGNED8* pucName,
                                             UNSIGNED16 usProp,
                                             void*      pvBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsDDSetProcProperty  (ADSHANDLE hConnect,
                                             UNSIGNED8* pucName,
                                             UNSIGNED16 usProp,
                                             void*      pvBuf,
                                             UNSIGNED16 usLen);
UNSIGNED32 ENTRYPOINT AdsDDGetRefIntegrityProperty(ADSHANDLE hConnect,
                                             UNSIGNED8* pucName,
                                             UNSIGNED16 usProp,
                                             void*      pvBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsDDSetRefIntegrityProperty(ADSHANDLE hConnect,
                                             UNSIGNED8* pucName,
                                             UNSIGNED16 usProp,
                                             void*      pvBuf,
                                             UNSIGNED16 usLen);
// SAP ACE API name aliases
UNSIGNED32 ENTRYPOINT AdsDDAddProcedure     (ADSHANDLE hConnect,
                                             UNSIGNED8* pucName,
                                             UNSIGNED8* pucContainer,
                                             UNSIGNED8* pucProcName,
                                             UNSIGNED32 ulInvokeOption,
                                             UNSIGNED8* pucInParams,
                                             UNSIGNED8* pucOutParams,
                                             UNSIGNED8* pucComments);
UNSIGNED32 ENTRYPOINT AdsDDRemoveProcedure  (ADSHANDLE hConnect,
                                             UNSIGNED8* pucName);
UNSIGNED32 ENTRYPOINT AdsDDGetProcedureProperty(ADSHANDLE hConnect,
                                             UNSIGNED8* pucName,
                                             UNSIGNED16 usProp,
                                             void*      pvBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsDDSetProcedureProperty(ADSHANDLE hConnect,
                                             UNSIGNED8* pucName,
                                             UNSIGNED16 usProp,
                                             void*      pvBuf,
                                             UNSIGNED16 usLen);
UNSIGNED32 ENTRYPOINT AdsDDRemoveTrigger    (ADSHANDLE hConnect,
                                             UNSIGNED8* pucName);
UNSIGNED32 ENTRYPOINT AdsDDFindFirstObject  (ADSHANDLE hObject,
                                             UNSIGNED16 usFindObjectType,
                                             UNSIGNED8* pucParentName,
                                             UNSIGNED8* pucObjectName,
                                             UNSIGNED16* pusObjectNameLen,
                                             ADSHANDLE* phFindHandle);
UNSIGNED32 ENTRYPOINT AdsDDFindNextObject   (ADSHANDLE hObject,
                                             ADSHANDLE hFindHandle,
                                             UNSIGNED8* pucObjectName,
                                             UNSIGNED16* pusObjectNameLen);
UNSIGNED32 ENTRYPOINT AdsDDFindClose        (ADSHANDLE hObject,
                                             ADSHANDLE hFindHandle);

UNSIGNED32 ENTRYPOINT AdsDDCreateView       (ADSHANDLE hConnect,
                                             UNSIGNED8* pucName,
                                             UNSIGNED8* pucComments,
                                             UNSIGNED8* pucSQL);
UNSIGNED32 ENTRYPOINT AdsDDDropView         (ADSHANDLE hConnect,
                                             UNSIGNED8* pucName);
UNSIGNED32 ENTRYPOINT AdsDDGetViewProperty  (ADSHANDLE hConnect,
                                             UNSIGNED8* pucName,
                                             UNSIGNED16 usProp,
                                             void*      pvBuf,
                                             UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsDDSetViewProperty  (ADSHANDLE hConnect,
                                             UNSIGNED8* pucName,
                                             UNSIGNED16 usProp,
                                             void*      pvBuf,
                                             UNSIGNED16 usLen);

// --------------------------------------------------------------------
// OpenADS-only extension: in-process Studio web console.
//
// These three entry points are NOT part of the SAP-shipped ACE ABI;
// they live in the OpenADS DLL only. They let a LocalServer
// application (one that loads ace64.dll / ace32.dll directly,
// without launching the openads_serverd.exe daemon) spin up the
// Studio HTTP web console alongside the engine in the same
// process. The console serves the same single-page admin UI and
// REST surface (`/api/...`) as openads_serverd, reading the DBF /
// CDX / FPT / DBT files directly from the on-disk data directory.
//
// Auto-start. If the environment variable OPENADS_STUDIO_PORT is
// set when ace64.dll / ace32.dll loads, the DLL boots Studio
// automatically on that port; OPENADS_STUDIO_DATA picks the data
// directory (default = current working directory) and
// OPENADS_STUDIO_HOST picks the bind address (default 127.0.0.1).
// Without OPENADS_STUDIO_PORT the auto-start path stays silent —
// no port is bound unless the host application asks for one.
//
// Build flag. The Studio target is only compiled into the DLL when
// OpenADS is built with -DOPENADS_WITH_HTTP=ON. When that flag is
// off, all three entry points are still exported but return
// AE_FUNCTION_NOT_AVAILABLE so the caller can detect it gracefully.
//
// Locking. Studio opens tables read-only via short-lived ABI
// connections. If your app holds a table in EXCLUSIVE mode, the
// browser will see a "table busy" error for that table until the
// app releases the exclusive lock; shared opens coexist fine.
//
// Returns AE_SUCCESS (0) on success, or an AE_* error code:
//   AE_FUNCTION_NOT_AVAILABLE  — DLL was built without HTTP support.
//   AE_INTERNAL_ERROR          — bind / listen failed (port in
//                                use), or pucDataDir == NULL.
UNSIGNED32 ENTRYPOINT AdsStudioStart(UNSIGNED16 usPort,
                                     UNSIGNED8* pucDataDir);
UNSIGNED32 ENTRYPOINT AdsStudioStop (void);
// Returns the port Studio is currently bound to, or 0 when not
// running. Useful when AdsStudioStart was passed port == 0
// (ephemeral) and the caller needs to know which port the OS
// picked.
UNSIGNED32 ENTRYPOINT AdsStudioPort (UNSIGNED16* pusPort);

// ---- Versioned ACE overloads (X# RDD compat, M12.22) ----
// Thin forwards to the base signatures above, dropping the params
// newer ACE builds added (charset/collation tags, page sizes, RI
// error strings); the bookmark / property / find-by-name entries
// get a minimal implementation. Names mirror the SAP ACE SDK.
UNSIGNED32 ENTRYPOINT AdsConnect26       (UNSIGNED8* pucServer,
                                          UNSIGNED16 usServerType,
                                          ADSHANDLE* phConnect);
UNSIGNED32 ENTRYPOINT AdsCreateTable71   (ADSHANDLE hConnect, UNSIGNED8* pucName,
                                          UNSIGNED8* pucAlias,
                                          UNSIGNED16 usTableType,
                                          UNSIGNED16 usCharType,
                                          UNSIGNED16 usLockType,
                                          UNSIGNED16 usCheckRights,
                                          UNSIGNED16 usMemoSize,
                                          UNSIGNED8* pucFields,
                                          UNSIGNED32 ulOptions,
                                          ADSHANDLE* phTable);
UNSIGNED32 ENTRYPOINT AdsCreateTable90   (ADSHANDLE hConnect, UNSIGNED8* pucName,
                                          UNSIGNED8* pucAlias,
                                          UNSIGNED16 usTableType,
                                          UNSIGNED16 usCharType,
                                          UNSIGNED16 usLockType,
                                          UNSIGNED16 usCheckRights,
                                          UNSIGNED16 usMemoSize,
                                          UNSIGNED8* pucFields,
                                          UNSIGNED32 ulOptions,
                                          UNSIGNED8* pucCollation,
                                          ADSHANDLE* phTable);
UNSIGNED32 ENTRYPOINT AdsOpenTable90     (ADSHANDLE hConnect, UNSIGNED8* pucName,
                                          UNSIGNED8* pucAlias,
                                          UNSIGNED16 usTableType,
                                          UNSIGNED16 usCharType,
                                          UNSIGNED16 usLockType,
                                          UNSIGNED16 usCheckRights,
                                          UNSIGNED32 ulOptions,
                                          UNSIGNED8* pucCollation,
                                          ADSHANDLE* phTable);
UNSIGNED32 ENTRYPOINT AdsCreateIndex90   (ADSHANDLE hObj, UNSIGNED8* pucFileName,
                                          UNSIGNED8* pucTag, UNSIGNED8* pucExpr,
                                          UNSIGNED8* pucCondition,
                                          UNSIGNED8* pucWhile,
                                          UNSIGNED32 ulOptions,
                                          UNSIGNED32 ulPageSize,
                                          UNSIGNED8* pucCollation,
                                          ADSHANDLE* phIndex);
UNSIGNED32 ENTRYPOINT AdsDDAddTable90    (ADSHANDLE hConnect, UNSIGNED8* pucAlias,
                                          UNSIGNED8* pucTablePath,
                                          UNSIGNED16 usTableType,
                                          UNSIGNED16 usCharType,
                                          UNSIGNED8* pucIndexPath,
                                          UNSIGNED8* pucComment,
                                          UNSIGNED8* pucCollation);
UNSIGNED32 ENTRYPOINT AdsDDCreateRefIntegrity62(ADSHANDLE hConnect,
                                          UNSIGNED8* pucName, UNSIGNED8* pucFail,
                                          UNSIGNED8* pucParent,
                                          UNSIGNED8* pucParentTag,
                                          UNSIGNED8* pucChild,
                                          UNSIGNED8* pucChildTag,
                                          UNSIGNED16 usUpdate,
                                          UNSIGNED16 usDelete,
                                          UNSIGNED8* pucNoPrimaryError,
                                          UNSIGNED8* pucCascadeError);
UNSIGNED32 ENTRYPOINT AdsFindFirstTable62(ADSHANDLE hConnect,
                                          UNSIGNED8* pucFileMask,
                                          UNSIGNED8* pucFirstDD,
                                          UNSIGNED16* pusDDLen,
                                          UNSIGNED8* pucFirstFile,
                                          UNSIGNED16* pusFileLen,
                                          ADSHANDLE* phFind);
UNSIGNED32 ENTRYPOINT AdsFindNextTable62 (ADSHANDLE hConnect, ADSHANDLE hFind,
                                          UNSIGNED8* pucDDName,
                                          UNSIGNED16* pusDDLen,
                                          UNSIGNED8* pucFileName,
                                          UNSIGNED16* pusFileLen);
UNSIGNED32 ENTRYPOINT AdsGetDateFormat60 (ADSHANDLE hConnect, UNSIGNED8* pucBuf,
                                          UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsGetExact22      (ADSHANDLE hObj, UNSIGNED16* pbExact);
UNSIGNED32 ENTRYPOINT AdsReindex61       (ADSHANDLE hObject,
                                          UNSIGNED32 ulPageSize);
UNSIGNED32 ENTRYPOINT AdsRestructureTable90(ADSHANDLE hConnect,
                                          UNSIGNED8* pucTableName,
                                          UNSIGNED8* pucPassword,
                                          UNSIGNED16 usTableType,
                                          UNSIGNED16 usCharType,
                                          UNSIGNED16 usLockType,
                                          UNSIGNED16 usCheckRights,
                                          UNSIGNED8* pucAddFields,
                                          UNSIGNED8* pucDeleteFields,
                                          UNSIGNED8* pucChangeFields,
                                          UNSIGNED8* pucCollation);
UNSIGNED32 ENTRYPOINT AdsCancelUpdate90  (ADSHANDLE hTable, UNSIGNED32 ulOptions);
UNSIGNED32 ENTRYPOINT AdsSetProperty90   (ADSHANDLE hObj, UNSIGNED32 ulOperation,
                                          UNSIGNED64* puqValue);
UNSIGNED32 ENTRYPOINT AdsFindConnection25(UNSIGNED8* pucFullPath,
                                          ADSHANDLE* phConnect);
UNSIGNED32 ENTRYPOINT AdsGetTableHandle25(ADSHANDLE hConnect, UNSIGNED8* pucName,
                                          ADSHANDLE* phTable);
UNSIGNED32 ENTRYPOINT AdsGetBookmark60   (ADSHANDLE hObj, UNSIGNED8* pucBookmark,
                                          UNSIGNED32* pulLength);
UNSIGNED32 ENTRYPOINT AdsGotoBookmark60  (ADSHANDLE hObj, UNSIGNED8* pucBookmark,
                              UNSIGNED32 ulLength);
UNSIGNED32 ENTRYPOINT AdsGetMemoBlockSize(ADSHANDLE hObj, UNSIGNED16* pusBlockSize);

// ---- Further X# Advantage RDD entry points (M12.23) ----
// Accept-and-ignore session/statement helpers, thin forwards, and
// AE_FUNCTION_NOT_AVAILABLE stubs. Field setters take the ACE "field
// name OR 1-based ordinal cast to a pointer" idiom (UNSIGNED8* pId).
UNSIGNED32 ENTRYPOINT AdsGetTableOpenOptions(ADSHANDLE hTable, UNSIGNED32* pulOptions);
UNSIGNED32 ENTRYPOINT AdsGetBookmark      (ADSHANDLE hTable, ADSHANDLE* phBookmark);
UNSIGNED32 ENTRYPOINT AdsCancelUpdate     (ADSHANDLE hTable);
UNSIGNED32 ENTRYPOINT AdsClearAllScopes   (ADSHANDLE hTable);
UNSIGNED32 ENTRYPOINT AdsClearDefault     (void);
UNSIGNED32 ENTRYPOINT AdsResetConnection  (ADSHANDLE hConnect);
UNSIGNED32 ENTRYPOINT AdsThreadExit       (void);
UNSIGNED32 ENTRYPOINT AdsDisableLocalConnections(void);
UNSIGNED32 ENTRYPOINT AdsEnableRI         (ADSHANDLE hConnect);
UNSIGNED32 ENTRYPOINT AdsDisableRI        (ADSHANDLE hConnect);
UNSIGNED32 ENTRYPOINT AdsEnableUniqueEnforcement (ADSHANDLE hConnect);
UNSIGNED32 ENTRYPOINT AdsDisableUniqueEnforcement(ADSHANDLE hConnect);
UNSIGNED32 ENTRYPOINT AdsEnableAutoIncEnforcement (ADSHANDLE hConnect);
UNSIGNED32 ENTRYPOINT AdsDisableAutoIncEnforcement(ADSHANDLE hConnect);
UNSIGNED32 ENTRYPOINT AdsRecallAllRecords (ADSHANDLE hTable);
UNSIGNED32 ENTRYPOINT AdsIsRecordVisible  (ADSHANDLE hObj, UNSIGNED16* pbVisible);
UNSIGNED32 ENTRYPOINT AdsGetKeyCount      (ADSHANDLE hIndex, UNSIGNED16 usFilterOption,
                                           UNSIGNED32* pulCount);
UNSIGNED32 ENTRYPOINT AdsContinue         (ADSHANDLE hTable, UNSIGNED16* pbFound);
UNSIGNED32 ENTRYPOINT AdsEvalTestExpr     (ADSHANDLE hTable, UNSIGNED8* pucExpr,
                                           UNSIGNED16* pusType);
UNSIGNED32 ENTRYPOINT AdsEvalLogicalExpr  (ADSHANDLE hTable, UNSIGNED8* pucExpr,
                                           UNSIGNED16* pbResult);
UNSIGNED32 ENTRYPOINT AdsEvalNumericExpr  (ADSHANDLE hTable, UNSIGNED8* pucExpr,
                                           double* pdResult);
UNSIGNED32 ENTRYPOINT AdsEvalStringExpr   (ADSHANDLE hTable, UNSIGNED8* pucExpr,
                                           UNSIGNED8* pucResult, UNSIGNED16* pusLen);
UNSIGNED32 ENTRYPOINT AdsFindConnection   (UNSIGNED8* pucServerName, ADSHANDLE* phConnect);
UNSIGNED32 ENTRYPOINT AdsGetAllIndexes    (ADSHANDLE hTable, ADSHANDLE* ahIndex,
                                           UNSIGNED16* pusArrayLen);
UNSIGNED32 ENTRYPOINT AdsGetFTSIndexes    (ADSHANDLE hTable, ADSHANDLE* ahIndex,
                                           UNSIGNED16* pusArrayLen);
UNSIGNED32 ENTRYPOINT AdsGetAllTables     (ADSHANDLE hConnect, ADSHANDLE* ahTable,
                              UNSIGNED16* pusArrayLen);
UNSIGNED32 ENTRYPOINT AdsCloneTable       (ADSHANDLE hTable, ADSHANDLE* phClone);
UNSIGNED32 ENTRYPOINT AdsCopyTableStructure(ADSHANDLE hTable, UNSIGNED8* pucFile);
UNSIGNED32 ENTRYPOINT AdsGetRecordCRC     (ADSHANDLE hTable, UNSIGNED32* pulCRC,
                                           UNSIGNED32 ulOptions);
UNSIGNED32 ENTRYPOINT AdsInitRawKey       (ADSHANDLE hIndex);
UNSIGNED32 ENTRYPOINT AdsMgDumpInternalTables(ADSHANDLE hMgmtHandle);
UNSIGNED32 ENTRYPOINT AdsClearSQLAbortFunc(void);
UNSIGNED32 ENTRYPOINT AdsClearSQLParams   (ADSHANDLE hStatement);
UNSIGNED32 ENTRYPOINT AdsStmtClearTablePasswords(ADSHANDLE hStatement);
UNSIGNED32 ENTRYPOINT AdsStmtDisableEncryption  (ADSHANDLE hStatement);
UNSIGNED32 ENTRYPOINT AdsStmtSetTableCharType   (ADSHANDLE hStatement, UNSIGNED16 usCharType);
UNSIGNED32 ENTRYPOINT AdsStmtSetTableCollation  (ADSHANDLE hStatement, UNSIGNED8* pucCollation);
UNSIGNED32 ENTRYPOINT AdsStmtSetTableRights     (ADSHANDLE hStatement, UNSIGNED16 usCheckRights);
UNSIGNED32 ENTRYPOINT AdsSetField         (ADSHANDLE hObj, UNSIGNED8* pucFldId,
                                           UNSIGNED8* pucBuf, UNSIGNED32 ulLen);
UNSIGNED32 ENTRYPOINT AdsSetEmpty         (ADSHANDLE hObj, UNSIGNED8* pucFldId);
UNSIGNED32 ENTRYPOINT AdsSetNull          (ADSHANDLE hTable, UNSIGNED8* pucFldId);
UNSIGNED32 ENTRYPOINT AdsSetShort         (ADSHANDLE hObj, UNSIGNED8* pucFldId, SIGNED32 sValue);
UNSIGNED32 ENTRYPOINT AdsSetMoney         (ADSHANDLE hObj, UNSIGNED8* pucFldId, SIGNED64 qValue);
UNSIGNED32 ENTRYPOINT AdsSetTime          (ADSHANDLE hObj, UNSIGNED8* pucFldId,
                                           UNSIGNED8* pucValue, UNSIGNED16 usLen);
UNSIGNED32 ENTRYPOINT AdsSetTimeStamp     (ADSHANDLE hObj, UNSIGNED8* pucFldId,
                                           UNSIGNED8* pucBuf, UNSIGNED32 ulLen);
UNSIGNED32 ENTRYPOINT AdsGetDate          (ADSHANDLE hObj, UNSIGNED8* pucFldId,
                                           UNSIGNED8* pucBuf, UNSIGNED16* pusLen);

#ifdef __cplusplus
}
#endif
