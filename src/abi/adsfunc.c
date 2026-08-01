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
