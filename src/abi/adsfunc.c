/*
 * adsfunc.c -- OpenADS legacy oads_*() filesystem / directory C API.
 *
 * These are thin ABI aliases: each oads_Foo() simply forwards to the
 * corresponding AdsFoo() implementation.  The separation exists so that
 * FiveWin / Harbour callers who linked against the old "oads_" spelling
 * keep working without changes.
 *
 * Compile as part of the OpenADS DLL (CMake adds this file) or link
 * standalone into a Harbour hbmk2 project together with oads_hb.c.
 *
 * Copyright (c) 2024-2026 OpenADS contributors -- MIT licence.
 */

#include "openads/ace.h"

/* ------------------------------------------------------------------ */
/*  File I/O                                                          */
/* ------------------------------------------------------------------ */

UNSIGNED32 OADSAPI oads_FOpen( ADSHANDLE hConnect, UNSIGNED8 *pucName,
                                  UNSIGNED16 usMode, ADSHANDLE *phFile )
{
    return AdsFOpen( hConnect, pucName, usMode, phFile );
}

UNSIGNED32 OADSAPI oads_FCreate( ADSHANDLE hConnect, UNSIGNED8 *pucName,
                                    UNSIGNED16 usAttribute, ADSHANDLE *phFile )
{
    return AdsFCreate( hConnect, pucName, usAttribute, phFile );
}

UNSIGNED32 OADSAPI oads_FClose( ADSHANDLE hFile )
{
    return AdsFClose( hFile );
}

UNSIGNED32 OADSAPI oads_FRead( ADSHANDLE hFile, void *pBuf,
                                  UNSIGNED32 ulLen, UNSIGNED32 *pulRead )
{
    return AdsFRead( hFile, pBuf, ulLen, pulRead );
}

UNSIGNED32 OADSAPI oads_FWrite( ADSHANDLE hFile, const void *pBuf,
                                   UNSIGNED32 ulLen, UNSIGNED32 *pulWritten )
{
    return AdsFWrite( hFile, pBuf, ulLen, pulWritten );
}

UNSIGNED32 OADSAPI oads_FSeek( ADSHANDLE hFile, SIGNED32 lOffset,
                                  UNSIGNED16 usOrigin, UNSIGNED32 *pulPos )
{
    return AdsFSeek( hFile, lOffset, usOrigin, pulPos );
}

/* ------------------------------------------------------------------ */
/*  File existence / metadata                                         */
/* ------------------------------------------------------------------ */

UNSIGNED32 OADSAPI oads_CheckExistence( ADSHANDLE hConnect,
                                           UNSIGNED8 *pucName,
                                           UNSIGNED16 *pbExists )
{
    return AdsCheckExistence( hConnect, pucName, pbExists );
}

UNSIGNED32 OADSAPI oads_DeleteFile( ADSHANDLE hConnect,
                                       UNSIGNED8 *pucName )
{
    return AdsDeleteFile( hConnect, pucName );
}

UNSIGNED32 OADSAPI oads_RenameFile( ADSHANDLE hConnect,
                                       UNSIGNED8 *pucOld,
                                       UNSIGNED8 *pucNew )
{
    return AdsRenameFile( hConnect, pucOld, pucNew );
}

UNSIGNED32 OADSAPI oads_CopyFile( ADSHANDLE hConnect,
                                     UNSIGNED8 *pucSrc,
                                     UNSIGNED8 *pucDst )
{
    return AdsCopyFile( hConnect, pucSrc, pucDst );
}

UNSIGNED32 OADSAPI oads_LoadFile( ADSHANDLE hConnect,
                                     UNSIGNED8 *pucName,
                                     void *pBuf,
                                     UNSIGNED32 *pulBufLen )
{
    return AdsLoadFile( hConnect, pucName, pBuf, pulBufLen );
}

UNSIGNED32 OADSAPI oads_SaveFile( ADSHANDLE hConnect,
                                     UNSIGNED8 *pucName,
                                     const void *pBuf,
                                     UNSIGNED32 ulLen )
{
    return AdsSaveFile( hConnect, pucName, pBuf, ulLen );
}

UNSIGNED32 OADSAPI oads_FReadAt( ADSHANDLE hFile,
                                     UNSIGNED64 ullOffset,
                                     void *pBuf,
                                     UNSIGNED32 ulLen,
                                     UNSIGNED32 *pulRead )
{
    return AdsFReadAt( hFile, ullOffset, pBuf, ulLen, pulRead );
}

UNSIGNED32 OADSAPI oads_FWriteAt( ADSHANDLE hFile,
                                     UNSIGNED64 ullOffset,
                                     const void *pBuf,
                                     UNSIGNED32 ulLen,
                                     UNSIGNED32 *pulWritten )
{
    return AdsFWriteAt( hFile, ullOffset, pBuf, ulLen, pulWritten );
}

UNSIGNED32 OADSAPI oads_FFlush( ADSHANDLE hFile )
{
    return AdsFFlush( hFile );
}

UNSIGNED32 OADSAPI oads_FEof( ADSHANDLE hFile,
                                     UNSIGNED16 *pbEof )
{
    return AdsFEof( hFile, pbEof );
}

UNSIGNED32 OADSAPI oads_SetFileAttr( ADSHANDLE hConnect,
                                     UNSIGNED8 *pucName,
                                     UNSIGNED32 ulAttr )
{
    return AdsSetFileAttr( hConnect, pucName, ulAttr );
}

UNSIGNED32 OADSAPI oads_SetFileTime( ADSHANDLE hConnect,
                                     UNSIGNED8 *pucName,
                                     UNSIGNED16 usYear,
                                     UNSIGNED8 ucMon,
                                     UNSIGNED8 ucDay,
                                     UNSIGNED8 ucHour,
                                     UNSIGNED8 ucMin,
                                     UNSIGNED8 ucSec )
{
    return AdsSetFileTime( hConnect, pucName, usYear, ucMon, ucDay,
                           ucHour, ucMin, ucSec );
}

UNSIGNED32 OADSAPI oads_FTrunc( ADSHANDLE hFile,
                                     UNSIGNED64 ullSize )
{
    return AdsFTrunc( hFile, ullSize );
}

UNSIGNED32 OADSAPI oads_TempFile( ADSHANDLE hConnect,
                                     UNSIGNED8 *pucDir,
                                     UNSIGNED8 *pucPrefix,
                                     UNSIGNED8 *pucName,
                                     UNSIGNED16 *pusNameLen )
{
    return AdsTempFile( hConnect, pucDir, pucPrefix, pucName, pusNameLen );
}

UNSIGNED32 OADSAPI oads_DirSpace( ADSHANDLE hConnect,
                                     UNSIGNED8 *pucDir,
                                     UNSIGNED8 ucType,
                                     UNSIGNED64 *pullBytes )
{
    return AdsDirSpace( hConnect, pucDir, ucType, pullBytes );
}

UNSIGNED32 OADSAPI oads_GetFileSize( ADSHANDLE hConnect,
                                        UNSIGNED8 *pucName,
                                        UNSIGNED32 *pulSize )
{
    return AdsGetFileSize( hConnect, pucName, pulSize );
}

UNSIGNED32 OADSAPI oads_GetFileTime( ADSHANDLE hConnect,
                                        UNSIGNED8 *pucName,
                                        UNSIGNED8 *pucTime,
                                        UNSIGNED16 *pusLen )
{
    return AdsGetFileTime( hConnect, pucName, pucTime, pusLen );
}

UNSIGNED32 OADSAPI oads_GetFileDate( ADSHANDLE hConnect,
                                        UNSIGNED8 *pucName,
                                        UNSIGNED8 *pucDate,
                                        UNSIGNED16 *pusLen )
{
    return AdsGetFileDate( hConnect, pucName, pucDate, pusLen );
}

/* ------------------------------------------------------------------ */
/*  Directory operations                                              */
/* ------------------------------------------------------------------ */

UNSIGNED32 OADSAPI oads_DirMake( ADSHANDLE hConnect,
                                    UNSIGNED8 *pucPath )
{
    return AdsDirMake( hConnect, pucPath );
}

/* ------------------------------------------------------------------ */
/*  Connection switching                                              */
/* ------------------------------------------------------------------ */

UNSIGNED32 OADSAPI oads_SetConnection( ADSHANDLE hConnect )
{
    return AdsSetDefaultConnection( hConnect );
}

UNSIGNED32 OADSAPI oads_GetConnection( ADSHANDLE *phConnect )
{
    return AdsGetDefaultConnection( phConnect );
}

UNSIGNED32 OADSAPI oads_DirRemove( ADSHANDLE hConnect,
                                      UNSIGNED8 *pucPath )
{
    return AdsDirRemove( hConnect, pucPath );
}

UNSIGNED32 OADSAPI oads_DirExist( ADSHANDLE hConnect,
                                     UNSIGNED8 *pucPath,
                                     UNSIGNED16 *pbExists )
{
    return AdsDirExist( hConnect, pucPath, pbExists );
}

UNSIGNED32 OADSAPI oads_Directory( ADSHANDLE hConnect,
                                      UNSIGNED8 *pucMask,
                                      UNSIGNED16 usAttr,
                                      UNSIGNED8 *pucBuffer,
                                      UNSIGNED32 *pulBufLen )
{
    return AdsDirectory( hConnect, pucMask, usAttr, pucBuffer, pulBufLen );
}

UNSIGNED32 OADSAPI oads_FLock(ADSHANDLE hFile, UNSIGNED64 ullStart, UNSIGNED64 ullLen, UNSIGNED16 usType)
{ return AdsFLock(hFile, ullStart, ullLen, usType); }

UNSIGNED32 OADSAPI oads_FUnlock(ADSHANDLE hFile, UNSIGNED64 ullStart, UNSIGNED64 ullLen)
{ return AdsFUnlock(hFile, ullStart, ullLen); }

UNSIGNED32 OADSAPI oads_FLockTest(ADSHANDLE hFile, UNSIGNED64 ullStart, UNSIGNED64 ullLen, UNSIGNED16 usType, SIGNED32* plResult)
{ return AdsFLockTest(hFile, ullStart, ullLen, usType, plResult); }
