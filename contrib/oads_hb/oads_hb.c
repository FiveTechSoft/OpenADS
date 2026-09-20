/*
 * oads_hb.c -- Harbour HB_FUNC wrappers for the oads_*() C API.
 *
 * Drop this file into your hbmk2 project to get OADS_FOPEN(),
 * OADS_FCREATE(), OADS_FCLOSE(), OADS_FREAD(), OADS_FWRITE(),
 * OADS_FSEEK(), OADS_CHECKEXISTENCE(), OADS_DELETEFILE(),
 * OADS_RENAMEFILE(), OADS_GETFILESIZE(), OADS_GETFILEDATE(),
 * OADS_GETFILETIME(), OADS_DIRMAKE(), OADS_DIRREMOVE(),
 * OADS_DIREXIST(), OADS_DIRECTORY(), OADS_FEXIST(), OADS_ZIP(),
 * OADS_UNZIP(), OADS_ZIPFILECOUNT(), OADS_ZIPFILELIST(), and the
 * server-side distributed mutex functions OADS_MUTEXCREATE(),
 * OADS_MUTEXLOCK(), OADS_MUTEXTRYLOCK(), OADS_MUTEXUNLOCK(),
 * OADS_MUTEXDESTROY() callable from Harbour PRG code, plus the
 * logging kill-switch OADS_SETLOGGING() and the version reporters
 * OADS_ADSVERSION() (DLL build) / OADS_SERVERVERSION() (server build).
 *
 * The actual C implementations live in adsfunc.c (or inside the
 * OpenADS DLL).  This file only contains the Harbour<->C glue.
 *
 * Build (hbmk2) — this file is compiled into YOUR project, not the
 * OpenADS library, so it includes "ace.h" the same way rddads does
 * (Harbour HB_WITH_ADS, the ACE SDK, or a copy of include/openads/ace.h).
 * Do not use "openads/ace.h" here: that path only exists inside the
 * OpenADS source tree and forces every consumer to edit this file.
 *
 *   hbmk2 myproject.hbp oads_hb.c adsfunc.c -I/path/to/dir/with/ace.h
 *
 * Or if oads_*() are already exported by the DLL (openace64.dll):
 *   hbmk2 myproject.hbp oads_hb.c -L/path/to/openads/importlib -lace64
 *
 * Connection management:
 *   OAds_SetConnection( hConn )  -- set default connection for this thread
 *   OAds_GetConnection()         -> hConn  -- get current default connection
 *
 * All OAds_F* functions accept hConn as the first (optional) parameter.
 * When omitted, the thread-local default connection is used:
 *   OAds_FOpen( cFileName, nMode )             -- uses default connection
 *   OAds_FOpen( hConn, cFileName, nMode )      -- uses explicit connection
 */

#include "hbapi.h"
#include "hbapiitm.h"
#include "hbdate.h"
#include "ace.h"
#include <string.h>

/* AdsGetServerVersion is an OpenADS extension (v1.09.28+). Declared
   here as well so this file still compiles against an older ace.h;
   an identical redeclaration is legal C when the new ace.h is used. */
extern UNSIGNED32 ENTRYPOINT AdsGetServerVersion( ADSHANDLE   hConnect,
                                                  UNSIGNED8 * pucBuf,
                                                  UNSIGNED16 * pusLen );

/* ------------------------------------------------------------------ */
/*  OADS_SETCONNECTION( hConn ) -> lOk                                 */
/*  Set the default connection for the calling thread.                 */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_SETCONNECTION )
{
    ADSHANDLE hConn = ( ADSHANDLE ) hb_parnint( 1 );
    hb_retl( AdsSetDefaultConnection( hConn ) == 0 );
}

/* ------------------------------------------------------------------ */
/*  OADS_GETCONNECTION() -> hConn                                      */
/*  Get the current default connection for the calling thread.         */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_GETCONNECTION )
{
    ADSHANDLE hConn = 0;
    AdsGetDefaultConnection( &hConn );
    hb_retnint( ( HB_MAXINT ) hConn );
}

/* ------------------------------------------------------------------ */
/*  OADS_SETLOGGING( lOn ) -> lOk                                      */
/*  Master switch for every log the ace DLL can emit (audit channel +  */
/*  ace_calls.log traces). Call OAds_SetLogging( .F. ) once at startup */
/*  in production so no paths/aliases reach end-user machines.         */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_SETLOGGING )
{
    hb_retl( OAdsSetLogging( ( UNSIGNED16 ) hb_parl( 1 ) ) == 0 );
}

/* ------------------------------------------------------------------ */
/*  OADS_FCreate( hConn, cFileName, nAttribute ) -> hFile (0 on fail) */
/*  OADS_FCreate( cFileName, nAttribute )          -> hFile (default)  */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_FCREATE )
{
    ADSHANDLE  hConn;
    const char *szName;
    UNSIGNED16 usAttr;
    ADSHANDLE  hFile   = 0;
    UNSIGNED32 ulRc    = 1;

    if( hb_pcount() >= 3 )
    {
        hConn  = ( ADSHANDLE ) hb_parnint( 1 );
        szName = hb_parc( 2 );
        usAttr = ( UNSIGNED16 ) hb_parni( 3 );
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        szName = hb_parc( 1 );
        usAttr = ( UNSIGNED16 ) hb_parni( 2 );
    }

    if( szName )
        ulRc = oads_FCreate( hConn, ( UNSIGNED8 * ) szName, usAttr, &hFile );
    hb_retnint( ulRc == 0 ? ( HB_MAXINT ) hFile : 0 );
}

/* ------------------------------------------------------------------ */
/*  OADS_FOpen( hConn, cFileName, nMode ) -> hFile (0 on fail)       */
/*  OADS_FOpen( cFileName, nMode )          -> hFile (default conn)   */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_FOPEN )
{
    ADSHANDLE  hConn;
    const char *szName;
    UNSIGNED16 usMode;
    ADSHANDLE  hFile   = 0;
    UNSIGNED32 ulRc    = 1;

    if( hb_pcount() >= 3 )
    {
        hConn  = ( ADSHANDLE ) hb_parnint( 1 );
        szName = hb_parc( 2 );
        usMode = ( UNSIGNED16 ) hb_parni( 3 );
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        szName = hb_parc( 1 );
        usMode = ( UNSIGNED16 ) hb_parni( 2 );
    }

    if( szName )
        ulRc = oads_FOpen( hConn, ( UNSIGNED8 * ) szName, usMode, &hFile );
    hb_retnint( ulRc == 0 ? ( HB_MAXINT ) hFile : 0 );
}

/* ------------------------------------------------------------------ */
/*  OADS_FClose( hFile ) -> lOk (.T./.F.)                             */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_FCLOSE )
{
    hb_retl( oads_FClose( ( ADSHANDLE ) hb_parnint( 1 ) ) == 0 );
}

/* ------------------------------------------------------------------ */
/*  OADS_FWrite( hFile, cBuffer ) -> nBytesWritten                    */
/*  OADS_FWrite( hFile, cBuffer, @nWritten ) -> lOk   (with @ form)  */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_FWRITE )
{
    ADSHANDLE hFile      = ( ADSHANDLE ) hb_parnint( 1 );
    const char *szBuf    = hb_parc( 2 );
    UNSIGNED32 ulLen     = ( UNSIGNED32 ) hb_parclen( 2 );
    UNSIGNED32 ulWritten = 0;
    UNSIGNED32 ulRc;

    if( !szBuf )
    {
        hb_retni( 0 );
        return;
    }

    ulRc = oads_FWrite( hFile, szBuf, ulLen, &ulWritten );

    if( hb_pcount() >= 3 && HB_ISBYREF( 3 ) )
    {
        hb_stornl( ( long ) ulWritten, 3 );
        hb_retl( ulRc == 0 );
    }
    else
    {
        hb_retnl( ( long ) ulWritten );
    }
}

/* ------------------------------------------------------------------ */
/*  OADS_FRead( hFile, nLen ) -> cData          (simple form)         */
/*  OADS_FRead( hFile, @cBuf, nLen ) -> nRead   (with @ form)        */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_FREAD )
{
    ADSHANDLE hFile   = ( ADSHANDLE ) hb_parnint( 1 );
    UNSIGNED32 ulLen;
    UNSIGNED32 ulRead = 0;
    char *pBuf;

    if( hb_pcount() >= 3 && HB_ISBYREF( 2 ) )
    {
        HB_SIZE nBufLen;
        ulLen = ( UNSIGNED32 ) hb_parnl( 3 );
        pBuf  = ( char * ) hb_xgrab( ulLen + 1 );
        oads_FRead( hFile, pBuf, ulLen, &ulRead );
        memset( pBuf + ulRead, 0, 1 );
        nBufLen = ( HB_SIZE ) ulRead;
        hb_storclen( pBuf, nBufLen, 2 );
        hb_xfree( pBuf );
        hb_retnl( ( long ) ulRead );
    }
    else
    {
        ulLen = ( UNSIGNED32 ) hb_parnl( 2 );
        pBuf  = ( char * ) hb_xgrab( ulLen + 1 );
        oads_FRead( hFile, pBuf, ulLen, &ulRead );
        memset( pBuf + ulRead, 0, 1 );
        hb_retclen( pBuf, ( HB_SIZE ) ulRead );
        hb_xfree( pBuf );
    }
}

/* ------------------------------------------------------------------ */
/*  OADS_FSeek( hFile, nOffset, nOrigin ) -> nPosition                */
/*  nOrigin: 0 = SEEK_SET, 1 = SEEK_CUR, 2 = SEEK_END                */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_FSEEK )
{
    ADSHANDLE hFile    = ( ADSHANDLE ) hb_parnint( 1 );
    SIGNED32  lOffset  = ( SIGNED32 ) hb_parnl( 2 );
    UNSIGNED16 usOrigin = ( UNSIGNED16 ) hb_parni( 3 );
    UNSIGNED32 ulPos   = 0;

    oads_FSeek( hFile, lOffset, usOrigin, &ulPos );
    hb_retnl( ( long ) ulPos );
}

/* ------------------------------------------------------------------ */
/*  OADS_CheckExistence( hConn, cName ) -> lExists                    */
/*  OADS_CheckExistence( cName )          -> lExists (default conn)   */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_CHECKEXISTENCE )
{
    ADSHANDLE  hConn;
    const char *szName;
    UNSIGNED16 usExists = 0;

    if( hb_pcount() >= 2 )
    {
        hConn  = ( ADSHANDLE ) hb_parnint( 1 );
        szName = hb_parc( 2 );
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        szName = hb_parc( 1 );
    }

    if( szName )
        oads_CheckExistence( hConn, ( UNSIGNED8 * ) szName, &usExists );
    hb_retl( usExists != 0 );
}

/* ------------------------------------------------------------------ */
/*  OADS_DeleteFile( hConn, cName ) -> lOk                            */
/*  OADS_DeleteFile( cName )          -> lOk (default conn)           */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_DELETEFILE )
{
    ADSHANDLE  hConn;
    const char *szName;
    UNSIGNED32 ulRc    = 1;

    if( hb_pcount() >= 2 )
    {
        hConn  = ( ADSHANDLE ) hb_parnint( 1 );
        szName = hb_parc( 2 );
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        szName = hb_parc( 1 );
    }

    if( szName )
        ulRc = oads_DeleteFile( hConn, ( UNSIGNED8 * ) szName );
    hb_retl( ulRc == 0 );
}

/* ------------------------------------------------------------------ */
/*  OADS_RenameFile( hConn, cOld, cNew ) -> lOk                       */
/*  OADS_RenameFile( cOld, cNew )          -> lOk (default conn)      */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_RENAMEFILE )
{
    ADSHANDLE  hConn;
    const char *szOld;
    const char *szNew;
    UNSIGNED32 ulRc   = 1;

    if( hb_pcount() >= 3 )
    {
        hConn = ( ADSHANDLE ) hb_parnint( 1 );
        szOld = hb_parc( 2 );
        szNew = hb_parc( 3 );
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        szOld = hb_parc( 1 );
        szNew = hb_parc( 2 );
    }

    if( szOld && szNew )
        ulRc = oads_RenameFile( hConn, ( UNSIGNED8 * ) szOld,
                                       ( UNSIGNED8 * ) szNew );
    hb_retl( ulRc == 0 );
}

/* ------------------------------------------------------------------ */
/*  OADS_GetFileSize( hConn, cName ) -> nSize                         */
/*  OADS_GetFileSize( cName )          -> nSize (default conn)        */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_GETFILESIZE )
{
    ADSHANDLE  hConn;
    const char *szName;
    UNSIGNED32 ulSize  = 0;

    if( hb_pcount() >= 2 )
    {
        hConn  = ( ADSHANDLE ) hb_parnint( 1 );
        szName = hb_parc( 2 );
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        szName = hb_parc( 1 );
    }

    if( szName )
        oads_GetFileSize( hConn, ( UNSIGNED8 * ) szName, &ulSize );
    hb_retnl( ( long ) ulSize );
}

/* ------------------------------------------------------------------ */
/*  OADS_GetFileDate( hConn, cName ) -> cDate                         */
/*  OADS_GetFileDate( cName )          -> cDate (default conn)        */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_GETFILEDATE )
{
    ADSHANDLE  hConn;
    const char *szName;
    UNSIGNED8  aucDate[ 32 ] = { 0 };
    UNSIGNED16 usLen   = sizeof( aucDate );

    if( hb_pcount() >= 2 )
    {
        hConn  = ( ADSHANDLE ) hb_parnint( 1 );
        szName = hb_parc( 2 );
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        szName = hb_parc( 1 );
    }

    if( szName )
        oads_GetFileDate( hConn, ( UNSIGNED8 * ) szName, aucDate, &usLen );
    hb_retclen( ( char * ) aucDate, usLen );
}

/* ------------------------------------------------------------------ */
/*  OADS_GetFileTime( hConn, cName ) -> cTime                         */
/*  OADS_GetFileTime( cName )          -> cTime (default conn)        */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_GETFILETIME )
{
    ADSHANDLE  hConn;
    const char *szName;
    UNSIGNED8  aucTime[ 32 ] = { 0 };
    UNSIGNED16 usLen   = sizeof( aucTime );

    if( hb_pcount() >= 2 )
    {
        hConn  = ( ADSHANDLE ) hb_parnint( 1 );
        szName = hb_parc( 2 );
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        szName = hb_parc( 1 );
    }

    if( szName )
        oads_GetFileTime( hConn, ( UNSIGNED8 * ) szName, aucTime, &usLen );
    hb_retclen( ( char * ) aucTime, usLen );
}

/* ------------------------------------------------------------------ */
/*  OADS_DirMake( hConn, cPath ) -> lOk                               */
/*  OADS_DirMake( cPath )          -> lOk (default conn)              */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_DIRMAKE )
{
    ADSHANDLE  hConn;
    const char *szPath;
    UNSIGNED32 ulRc    = 1;

    if( hb_pcount() >= 2 )
    {
        hConn  = ( ADSHANDLE ) hb_parnint( 1 );
        szPath = hb_parc( 2 );
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        szPath = hb_parc( 1 );
    }

    if( szPath )
        ulRc = oads_DirMake( hConn, ( UNSIGNED8 * ) szPath );
    hb_retl( ulRc == 0 );
}

/* ------------------------------------------------------------------ */
/*  OADS_DirRemove( hConn, cPath ) -> lOk                             */
/*  OADS_DirRemove( cPath )          -> lOk (default conn)            */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_DIRREMOVE )
{
    ADSHANDLE  hConn;
    const char *szPath;
    UNSIGNED32 ulRc    = 1;

    if( hb_pcount() >= 2 )
    {
        hConn  = ( ADSHANDLE ) hb_parnint( 1 );
        szPath = hb_parc( 2 );
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        szPath = hb_parc( 1 );
    }

    if( szPath )
        ulRc = oads_DirRemove( hConn, ( UNSIGNED8 * ) szPath );
    hb_retl( ulRc == 0 );
}

/* ------------------------------------------------------------------ */
/*  OADS_DirExist( hConn, cPath ) -> lExists                          */
/*  OADS_DirExist( cPath )          -> lExists (default conn)         */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_DIREXIST )
{
    ADSHANDLE  hConn;
    const char *szPath;
    UNSIGNED16 usExists = 0;

    if( hb_pcount() >= 2 )
    {
        hConn  = ( ADSHANDLE ) hb_parnint( 1 );
        szPath = hb_parc( 2 );
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        szPath = hb_parc( 1 );
    }

    if( szPath )
        oads_DirExist( hConn, ( UNSIGNED8 * ) szPath, &usExists );
    hb_retl( usExists != 0 );
}

/* ------------------------------------------------------------------ */
/*  OADS_Directory( hConn, cMask, nAttr ) -> aDir                   */
/*  OADS_Directory( cMask, nAttr )          -> aDir (default conn)  */
/*  Harbour Directory() shape: { {cName,nSize,dDate,cTime,cAttr}..} */
/*  cAttr carries "D" (directory) and/or "R" (readonly) — the only  */
/*  bits the engine reports; "" for a plain file. Empty array when */
/*  nothing matches or on error.                                   */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_DIRECTORY )
{
    ADSHANDLE  hConn;
    const char *szMask;
    UNSIGNED16 usAttr;
    UNSIGNED32 ulLen   = 0;
    unsigned char *buf;
    UNSIGNED32 ulRc;
    UNSIGNED32 off;
    PHB_ITEM pArray;

    if( hb_pcount() >= 3 )
    {
        hConn  = ( ADSHANDLE ) hb_parnint( 1 );
        szMask = hb_parc( 2 );
        usAttr = ( UNSIGNED16 ) hb_parni( 3 );
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        szMask = hb_parc( 1 );
        usAttr = ( UNSIGNED16 ) hb_parni( 2 );
    }

    pArray = hb_itemArrayNew( 0 );

    if( szMask )
        oads_Directory( hConn, ( UNSIGNED8 * ) szMask, usAttr, NULL, &ulLen );

    if( ulLen == 0 || ulLen > 1024 * 1024 )
    {
        hb_itemReturnRelease( pArray );
        return;
    }

    buf = ( unsigned char * ) hb_xgrab( ulLen );
    ulRc = oads_Directory( hConn, ( UNSIGNED8 * ) szMask, usAttr,
                           buf, &ulLen );
    if( ulRc == 0 )
    {
        /* Packed wire layout, little-endian (see pack_dir_entry in   */
        /* src/engine/server_fs.cpp): u16 namelen, name, u64 size,    */
        /* u16 year, mon, day, hh, mm, ss, u32 attr.                  */
        off = 0;
        while( off + 2 <= ulLen )
        {
            UNSIGNED16 n;
            double dSize;
            UNSIGNED16 year;
            unsigned mon, day, hh, mm, ss;
            UNSIGNED32 attr;
            char szTime[ 9 ];
            char szAttr[ 3 ];
            PHB_ITEM pEntry;
            int i;

            n = ( UNSIGNED16 ) ( buf[ off ] | ( buf[ off + 1 ] << 8 ) );
            off += 2;
            if( off + ( UNSIGNED32 ) n + 8 + 2 + 5 + 4 > ulLen )
                break;  /* truncated tail — stop, keep what we have */

            pEntry = hb_itemArrayNew( 5 );
            hb_arraySetCL( pEntry, 1, ( char * ) buf + off, n );
            off += n;

            dSize = 0.0;
            for( i = 7; i >= 0; --i )
                dSize = dSize * 256.0 + ( double ) buf[ off + i ];
            hb_arraySetND( pEntry, 2, dSize );
            off += 8;

            year = ( UNSIGNED16 ) ( buf[ off ] | ( buf[ off + 1 ] << 8 ) );
            off += 2;
            mon = buf[ off++ ];
            day = buf[ off++ ];
            hh  = buf[ off++ ];
            mm  = buf[ off++ ];
            ss  = buf[ off++ ];
            hb_arraySetDL( pEntry, 3,
                             hb_dateEncode( year, mon, day ) );
            hb_snprintf( szTime, sizeof( szTime ), "%02u:%02u:%02u",
                         hh, mm, ss );
            hb_arraySetC( pEntry, 4, szTime );

            attr = ( UNSIGNED32 ) buf[ off ] |
                   ( ( UNSIGNED32 ) buf[ off + 1 ] << 8 ) |
                   ( ( UNSIGNED32 ) buf[ off + 2 ] << 16 ) |
                   ( ( UNSIGNED32 ) buf[ off + 3 ] << 24 );
            off += 4;
            szAttr[ 0 ] = '\0';
            if( attr & 0x10 )
                strcat( szAttr, "D" );
            if( attr & 0x01 )
                strcat( szAttr, "R" );
            hb_arraySetC( pEntry, 5, szAttr );

            hb_arrayAddForward( pArray, pEntry );
            hb_itemRelease( pEntry );
        }
    }
    hb_xfree( buf );
    hb_itemReturnRelease( pArray );
}

/* ------------------------------------------------------------------ */
/*  OAds_FExist( hConn, cFileName ) -> lExists                         */
/*  OAds_FExist( cFileName )          -> lExists (default conn)        */
/*  Alias for OAds_CheckExistence, file-specific naming                */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_FEXIST )
{
    ADSHANDLE  hConn;
    const char *szName;
    UNSIGNED16 usExists = 0;

    if( hb_pcount() >= 2 )
    {
        hConn  = ( ADSHANDLE ) hb_parnint( 1 );
        szName = hb_parc( 2 );
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        szName = hb_parc( 1 );
    }

    if( szName )
        oads_CheckExistence( hConn, ( UNSIGNED8 * ) szName, &usExists );
    hb_retl( usExists != 0 );
}

/* ------------------------------------------------------------------ */
/*  Distributed mutex service (server-wide named mutexes).            */
/*  Remote connections only (ADS_REMOTE_SERVER).                      */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/*  OAds_MutexCreate( hConn, cName ) -> lOk                            */
/*  OAds_MutexCreate( cName )          -> lOk (default conn)           */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_MUTEXCREATE )
{
    ADSHANDLE  hConn;
    const char *szName;
    UNSIGNED32 ulRc    = 1;

    if( hb_pcount() >= 2 )
    {
        hConn  = ( ADSHANDLE ) hb_parnint( 1 );
        szName = hb_parc( 2 );
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        szName = hb_parc( 1 );
    }

    if( szName )
        ulRc = AdsMutexCreate( hConn, ( UNSIGNED8 * ) szName );
    hb_retl( ulRc == 0 );
}

/* ------------------------------------------------------------------ */
/*  OAds_MutexLock( hConn, cName, nTimeoutMs ) -> lOk                  */
/*  OAds_MutexLock( cName, nTimeoutMs )          -> lOk (default conn) */
/*  nTimeoutMs: milliseconds to wait, 0 = wait forever                */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_MUTEXLOCK )
{
    ADSHANDLE  hConn;
    const char *szName;
    UNSIGNED32 ulTimeOut;
    UNSIGNED32 ulRc      = 1;

    if( hb_pcount() >= 3 )
    {
        hConn     = ( ADSHANDLE ) hb_parnint( 1 );
        szName    = hb_parc( 2 );
        ulTimeOut = ( UNSIGNED32 ) hb_parnint( 3 );
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        szName    = hb_parc( 1 );
        ulTimeOut = ( UNSIGNED32 ) hb_parnint( 2 );
    }

    if( szName )
        ulRc = AdsMutexLock( hConn, ( UNSIGNED8 * ) szName, ulTimeOut );
    hb_retl( ulRc == 0 );
}

/* ------------------------------------------------------------------ */
/*  OAds_MutexTryLock( hConn, cName ) -> lLocked                       */
/*  OAds_MutexTryLock( cName )          -> lLocked (default conn)      */
/*  Non-blocking: returns .T. only if acquired immediately            */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_MUTEXTRYLOCK )
{
    ADSHANDLE  hConn;
    const char *szName;
    UNSIGNED16 usLocked = 0;

    if( hb_pcount() >= 2 )
    {
        hConn  = ( ADSHANDLE ) hb_parnint( 1 );
        szName = hb_parc( 2 );
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        szName = hb_parc( 1 );
    }

    if( szName )
        AdsMutexTryLock( hConn, ( UNSIGNED8 * ) szName, &usLocked );
    hb_retl( usLocked != 0 );
}

/* ------------------------------------------------------------------ */
/*  OAds_MutexUnlock( hConn, cName ) -> lOk                            */
/*  OAds_MutexUnlock( cName )          -> lOk (default conn)           */
/*  Only the owning session can unlock                                 */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_MUTEXUNLOCK )
{
    ADSHANDLE  hConn;
    const char *szName;
    UNSIGNED32 ulRc    = 1;

    if( hb_pcount() >= 2 )
    {
        hConn  = ( ADSHANDLE ) hb_parnint( 1 );
        szName = hb_parc( 2 );
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        szName = hb_parc( 1 );
    }

    if( szName )
        ulRc = AdsMutexUnlock( hConn, ( UNSIGNED8 * ) szName );
    hb_retl( ulRc == 0 );
}

/* ------------------------------------------------------------------ */
/*  OAds_MutexDestroy( hConn, cName ) -> lOk                           */
/*  OAds_MutexDestroy( cName )          -> lOk (default conn)          */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_MUTEXDESTROY )
{
    ADSHANDLE  hConn;
    const char *szName;
    UNSIGNED32 ulRc    = 1;

    if( hb_pcount() >= 2 )
    {
        hConn  = ( ADSHANDLE ) hb_parnint( 1 );
        szName = hb_parc( 2 );
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        szName = hb_parc( 1 );
    }

    if( szName )
        ulRc = AdsMutexDestroy( hConn, ( UNSIGNED8 * ) szName );
    hb_retl( ulRc == 0 );
}

/* ------------------------------------------------------------------ */
/*  OAds_AdsVersion() -> cVersion   ("1.09.28")                        */
/*  Full dotted DLL build parsed from the version description.         */
/*  The SAP-shaped major.minor+letter ("1.9a") drops the patch and     */
/*  can not tell 1.09.27 from 1.09.28 apart.                           */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_ADSVERSION )
{
    UNSIGNED32 ulMajor = 0, ulMinor = 0;
    UNSIGNED8  ucLetter = 0;
    char       szDesc[ 128 ];
    UNSIGNED16 usLen = ( UNSIGNED16 ) sizeof( szDesc );
    char       szVer[ 32 ];
    const char *p;
    size_t     n;

    AdsGetVersion( &ulMajor, &ulMinor, &ucLetter,
                   ( UNSIGNED8 * ) szDesc, &usLen );
    szDesc[ sizeof( szDesc ) - 1 ] = '\0';
    /* Desc is "OpenADS 1.09.28 ACE-compatible engine". */
    if( strncmp( szDesc, "OpenADS ", 8 ) == 0 )
    {
        p = szDesc + 8;
        n = strcspn( p, " " );
        if( n > 0 && n < sizeof( szVer ) )
        {
            memcpy( szVer, p, n );
            szVer[ n ] = '\0';
            hb_retc( szVer );
            return;
        }
    }
    hb_snprintf( szVer, sizeof( szVer ), "%u.%u%c",
                 ( unsigned ) ulMajor,
                 ( unsigned ) ulMinor, ( char ) ucLetter );
    hb_retc( szVer );
}

/* ------------------------------------------------------------------ */
/*  OAds_ServerVersion( hConn ) -> cVersion   ("1.09.28", ""=unknown)  */
/*  OAds_ServerVersion()           -> cVersion (default conn)          */
/*  Dotted build of the serverd behind the connection, so an app can   */
/*  prove WHICH server binary it is talking to.                        */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_SERVERVERSION )
{
    ADSHANDLE  hConn;
    char       szVer[ 64 ];
    UNSIGNED16 usCap = ( UNSIGNED16 ) sizeof( szVer );
    UNSIGNED16 usLen = usCap;
    UNSIGNED32 ulRc;

    if( hb_pcount() >= 1 )
        hConn = ( ADSHANDLE ) hb_parnint( 1 );
    else
        AdsGetDefaultConnection( &hConn );

    ulRc = AdsGetServerVersion( hConn, ( UNSIGNED8 * ) szVer, &usLen );
    szVer[ sizeof( szVer ) - 1 ] = '\0';
    if( ulRc != 0 )
        hb_retc( "" );
    else if( usLen < usCap )
        hb_retc( szVer );
    else
        hb_retclen( szVer, usCap - 1 );
}

/* AdsZipFiles / AdsUnzipFiles are OpenADS extensions (v1.09.59+).
   Declared here as well so this file still compiles against an older
   ace.h; identical redeclarations are legal C with the new ace.h. */
extern UNSIGNED32 ENTRYPOINT AdsZipFiles( ADSHANDLE   hConnect,
                                          UNSIGNED8 * pucDir,
                                          UNSIGNED8 * pucFiles,
                                          UNSIGNED8 * pucZipName,
                                          UNSIGNED16  usLevel,
                                          UNSIGNED16  usOverwrite,
                                          UNSIGNED8 * pucPassword,
                                          UNSIGNED8 * pucExclude,
                                          UNSIGNED16  usWithPath,
                                          UNSIGNED8 * pucArchive,
                                          UNSIGNED16 *pusArchiveLen,
                                          UNSIGNED32 *pulFiles,
                                          UNSIGNED64 *pullBytes,
                                          UNSIGNED64 *pullArchiveBytes );
extern UNSIGNED32 ENTRYPOINT AdsUnzipFiles( ADSHANDLE   hConnect,
                                            UNSIGNED8 * pucDir,
                                            UNSIGNED8 * pucZip,
                                            UNSIGNED8 * pucPassword,
                                            UNSIGNED16  usOverwrite,
                                            UNSIGNED16  usWithPath,
                                            UNSIGNED32 *pulFiles,
                                            UNSIGNED64 *pullBytes,
                                            UNSIGNED64 *pullArchiveBytes );

/* AdsZipListFiles is an OpenADS extension (v1.09.67+). Declared
   here as well so this file still compiles against an older ace.h;
   an identical redeclaration is legal C when the new ace.h is used. */
extern UNSIGNED32 ENTRYPOINT AdsZipListFiles( ADSHANDLE   hConnect,
                                              UNSIGNED8 * pucZip,
                                              UNSIGNED8 * pucBuffer,
                                              UNSIGNED32 *pulBufLen,
                                              UNSIGNED32 *pulCount );
/* Join a Harbour array of strings with 0x1F separators (0x1F cannot
   occur in a file name on any OS). Returns NULL on non-array input;
   the caller frees with hb_xfree(). Empty arrays join to "". */
static char *oads_join_1f( PHB_ITEM pArray, HB_SIZE *pnOut )
{
    HB_ULONG ulLen, i;
    char *buf, *p;

    if( pArray == NULL || ! HB_IS_ARRAY( pArray ) )
        return NULL;
    ulLen = hb_arrayLen( pArray );
    buf = ( char * ) hb_xgrab( 1 );
    buf[ 0 ] = '\0';
    for( i = 1; i <= ulLen; ++i )
    {
        HB_SIZE nLen = hb_arrayGetCLen( pArray, i );
        const char *sz = hb_arrayGetCPtr( pArray, i );
        size_t cur;
        if( sz == NULL )
            continue;
        cur = strlen( buf );
        buf = ( char * ) hb_xrealloc( buf, cur + ( size_t ) nLen + 2 );
        p = buf + cur;
        if( p != buf )
            *p++ = '\x1F';
        memcpy( p, sz, nLen );
        p += nLen;
        *p = '\0';
    }
    if( pnOut )
        *pnOut = ( HB_SIZE ) strlen( buf );
    return buf;
}

/* ------------------------------------------------------------------ */
/*  OAds_Zip( [hConn,] cSrcDir, aSrcFiles, cZipDir, cZipName          */
/*            [, nLevel [, lOverwrite [, cPassword [, aExclude        */
/*            [, lWithPath ]]]]] )                                   */
/*    -> { nFiles, nBytes, nArchiveBytes, cArchive }, NIL on failure  */
/*  Server-side backup archiving: files stay on the server under      */
/*  --data. cSrcDir/aSrcFiles/cZipDir/cZipName are mandatory; the     */
/*  rest default (6, .F., "", {}, .F.). An empty cZipDir keeps the    */
/*  legacy dated repository (<root>/backup/<name>_YYYYMMDD.zip);      */
/*  otherwise the archive is written verbatim to                      */
/*  <root>/<cZipDir>/<cZipName> — the filename is                     */
/*  application-dependent, no date or extension is added.             */
/*  NOTE: v1.09.59 form (cDir, aFiles, cZipName) is NOT accepted —    */
/*  the 4th positional is now cZipDir, so old calls fail loud (NIL)   */
/*  instead of misfiling archives.                                   */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_ZIP )
{
    ADSHANDLE   hConn;
    const char *szDir, *szZipDir, *szZipName, *szPassword;
    PHB_ITEM    pFiles, pExclude;
    int         nLevel, base;
    UNSIGNED16  usOverwrite, usWithPath;
    char       *szFiles, *szExclude;
    char        szZipArg[ 1024 ];
    char        szArchive[ 512 ];
    UNSIGNED16  usArcLen = ( UNSIGNED16 ) sizeof( szArchive );
    UNSIGNED32  ulFiles = 0;
    UNSIGNED64  ullBytes = 0, ullArcBytes = 0;
    UNSIGNED32  ulRc;
    PHB_ITEM    pRet;

    /* hConn is optional but the arities overlap (4..9 without, 5..10
       with), so sniff the first param: numeric -> explicit handle. */
    if( hb_pcount() >= 5 && hb_param( 1, HB_IT_NUMERIC ) != NULL )
    {
        hConn = ( ADSHANDLE ) hb_parnint( 1 );
        base  = 1;
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        base = 0;
    }
    if( hb_pcount() < base + 4 )
    {
        hb_ret();
        return;
    }
    szDir     = hb_parc( base + 1 );
    pFiles    = hb_param( base + 2, HB_IT_ARRAY );
    szZipDir  = hb_parc( base + 3 );
    szZipName = hb_parc( base + 4 );
    nLevel    = hb_parni( base + 5 );
    if( hb_pcount() < base + 5 || nLevel < 0 || nLevel > 9 )
        nLevel = 6;
    usOverwrite = ( UNSIGNED16 ) ( hb_parl( base + 6 ) ? 1 : 0 );
    szPassword  = hb_parc( base + 7 );
    pExclude    = hb_param( base + 8, HB_IT_ARRAY );
    usWithPath  = ( UNSIGNED16 ) ( hb_parl( base + 9 ) ? 1 : 0 );

    if( szDir == NULL || pFiles == NULL ||
        szZipDir == NULL || szZipName == NULL )
    {
        hb_ret();
        return;
    }
    /* Fold dir+name into the single engine spelling; empty dir keeps
       the legacy dated backup/ repository server-side. */
    if( szZipDir[ 0 ] != '\0' )
        hb_snprintf( szZipArg, sizeof( szZipArg ), "%s/%s",
                     szZipDir, szZipName );
    else
        hb_snprintf( szZipArg, sizeof( szZipArg ), "%s", szZipName );
    szFiles   = oads_join_1f( pFiles, NULL );
    szExclude = oads_join_1f( pExclude, NULL );
    if( szFiles == NULL )
    {
        hb_ret();
        return;
    }
    ulRc = AdsZipFiles( hConn, ( UNSIGNED8 * ) szDir,
                        ( UNSIGNED8 * ) szFiles,
                        ( UNSIGNED8 * ) szZipArg,
                        ( UNSIGNED16 ) nLevel, usOverwrite,
                        ( UNSIGNED8 * ) ( szPassword ? szPassword : "" ),
                        ( UNSIGNED8 * ) ( szExclude ? szExclude : "" ),
                        usWithPath,
                        ( UNSIGNED8 * ) szArchive, &usArcLen,
                        &ulFiles, &ullBytes, &ullArcBytes );
    hb_xfree( szFiles );
    if( szExclude )
        hb_xfree( szExclude );
    if( ulRc != 0 )
    {
        hb_ret();
        return;
    }
    szArchive[ sizeof( szArchive ) - 1 ] = '\0';
    pRet = hb_itemArrayNew( 4 );
    hb_arraySetNL( pRet, 1, ulFiles );
    hb_arraySetND( pRet, 2, ( double ) ullBytes );
    hb_arraySetND( pRet, 3, ( double ) ullArcBytes );
    hb_arraySetC( pRet, 4, szArchive );
    hb_itemReturnRelease( pRet );
}

/* ------------------------------------------------------------------ */
/*  OAds_UnZip( [hConn,] cDirName, cZip [, cPassword [, lOverwrite      */
/*              [, lWithPath ]]] )                                     */
/*    -> { nFiles, nBytes, nArchiveBytes }, NIL on failure             */
/*  Extract a server-side archive (bare names resolve under backup/).  */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_UNZIP )
{
    ADSHANDLE   hConn;
    const char *szDir, *szZip, *szPassword;
    UNSIGNED16  usOverwrite, usWithPath;
    int         base;
    UNSIGNED32  ulFiles = 0;
    UNSIGNED64  ullBytes = 0, ullArcBytes = 0;
    UNSIGNED32  ulRc;
    PHB_ITEM    pRet;

    if( hb_pcount() >= 6 )
    {
        hConn = ( ADSHANDLE ) hb_parnint( 1 );
        base  = 1;
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        base = 0;
    }
    if( hb_pcount() < base + 2 )
    {
        hb_ret();
        return;
    }
    szDir       = hb_parc( base + 1 );
    szZip       = hb_parc( base + 2 );
    szPassword  = hb_parc( base + 3 );
    usOverwrite = ( UNSIGNED16 ) ( hb_parl( base + 4 ) ? 1 : 0 );
    usWithPath  = ( UNSIGNED16 ) ( hb_parl( base + 5 ) ? 1 : 0 );

    if( szDir == NULL || szZip == NULL )
    {
        hb_ret();
        return;
    }
    ulRc = AdsUnzipFiles( hConn, ( UNSIGNED8 * ) szDir,
                          ( UNSIGNED8 * ) szZip,
                          ( UNSIGNED8 * ) ( szPassword ? szPassword : "" ),
                          usOverwrite, usWithPath,
                          &ulFiles, &ullBytes, &ullArcBytes );
    if( ulRc != 0 )
    {
        hb_ret();
        return;
    }
    pRet = hb_itemArrayNew( 3 );
    hb_arraySetNL( pRet, 1, ulFiles );
    hb_arraySetND( pRet, 2, ( double ) ullBytes );
    hb_arraySetND( pRet, 3, ( double ) ullArcBytes );
    hb_itemReturnRelease( pRet );
}

/* ------------------------------------------------------------------ */
/*  OAds_ZipFileCount( hConn, cZip ) -> nFiles (0 on error)            */
/*  OAds_ZipFileCount( cZip )          -> nFiles (default conn)        */
/*  Harbour hb_GetFileCount() parity for server-side archives.        */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_ZIPFILECOUNT )
{
    ADSHANDLE   hConn;
    const char *szZip;
    UNSIGNED32  ulLen = 0, ulCount = 0;

    if( hb_pcount() >= 2 )
    {
        hConn = ( ADSHANDLE ) hb_parnint( 1 );
        szZip = hb_parc( 2 );
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        szZip = hb_parc( 1 );
    }

    if( szZip )
        AdsZipListFiles( hConn, ( UNSIGNED8 * ) szZip,
                         NULL, &ulLen, &ulCount );
    hb_retnint( ( HB_MAXINT ) ulCount );
}

/* Little-endian readers for the packed ZipEntry layout (must match
   engine pack_zip_entry; the buffer always comes from our own
   AdsZipListFiles call sized above, but parse defensively). */
static UNSIGNED16 oads_ziplist_u16( const unsigned char *buf,
                                    UNSIGNED32 *pOff, UNSIGNED32 ulLen,
                                    int *pOk )
{
    UNSIGNED16 v;
    if( *pOff + 2 > ulLen )
    {
        *pOk = 0;
        return 0;
    }
    v = ( UNSIGNED16 ) ( buf[ *pOff ] | ( buf[ *pOff + 1 ] << 8 ) );
    *pOff += 2;
    return v;
}

static UNSIGNED32 oads_ziplist_u32( const unsigned char *buf,
                                    UNSIGNED32 *pOff, UNSIGNED32 ulLen,
                                    int *pOk )
{
    UNSIGNED32 v;
    if( *pOff + 4 > ulLen )
    {
        *pOk = 0;
        return 0;
    }
    v = ( UNSIGNED32 ) buf[ *pOff ] |
        ( ( UNSIGNED32 ) buf[ *pOff + 1 ] << 8 ) |
        ( ( UNSIGNED32 ) buf[ *pOff + 2 ] << 16 ) |
        ( ( UNSIGNED32 ) buf[ *pOff + 3 ] << 24 );
    *pOff += 4;
    return v;
}

static double oads_ziplist_u64( const unsigned char *buf,
                                UNSIGNED32 *pOff, UNSIGNED32 ulLen,
                                int *pOk )
{
    double v = 0.0;
    int i;
    if( *pOff + 8 > ulLen )
    {
        *pOk = 0;
        return 0.0;
    }
    for( i = 7; i >= 0; --i )
        v = v * 256.0 + ( double ) buf[ *pOff + i ];
    *pOff += 8;
    return v;
}

/* ------------------------------------------------------------------ */
/*  OAds_ZipFileList( hConn, cZip [, lVerbose ] ) -> aFiles            */
/*  OAds_ZipFileList( cZip [, lVerbose ] )          -> aFiles          */
/*  Harbour hb_GetFilesInZip() parity: plain form returns file names; */
/*  verbose form returns {cName,nSize,nMethod,nCompSize,nRatio,dDate,  */
/*  cTime,cCRC,nInternalAttr,lCrypted,cComment} rows. Empty array on   */
/*  error. Bare archive names resolve under backup/ like UnZip.       */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_ZIPFILELIST )
{
    ADSHANDLE   hConn;
    const char *szZip;
    int         lVerbose, base;
    UNSIGNED32  ulLen = 0, ulCount = 0, off = 0;
    unsigned char *buf;
    UNSIGNED32  ulRc;
    PHB_ITEM    pArray;
    UNSIGNED32  i;
    int         ok;

    if( hb_pcount() >= 2 && hb_param( 1, HB_IT_NUMERIC ) != NULL )
    {
        hConn = ( ADSHANDLE ) hb_parnint( 1 );
        base  = 1;
    }
    else
    {
        AdsGetDefaultConnection( &hConn );
        base = 0;
    }
    if( hb_pcount() < base + 1 )
    {
        hb_ret();
        return;
    }
    szZip    = hb_parc( base + 1 );
    lVerbose = hb_parl( base + 2 ) ? 1 : 0;

    pArray = hb_itemArrayNew( 0 );

    if( szZip )
        AdsZipListFiles( hConn, ( UNSIGNED8 * ) szZip,
                         NULL, &ulLen, &ulCount );

    if( ulLen == 0 || ulLen > 16 * 1024 * 1024 )
    {
        hb_itemReturnRelease( pArray );
        return;
    }

    buf = ( unsigned char * ) hb_xgrab( ulLen );
    ulRc = AdsZipListFiles( hConn, ( UNSIGNED8 * ) szZip,
                            buf, &ulLen, &ulCount );
    if( ulRc == 0 )
    {
        /* Packed layout (little-endian, see engine pack_zip_entry):
           u16 namelen, name, u64 size, u64 comp, u16 method, u32 crc,
           u16 year, mon, day, hh, mm, ss, u16 internal, u32 external,
           u8 encrypted, u16 commentlen, comment. */
        off = 0;
        ok  = 1;
        for( i = 0; i < ulCount && ok; ++i )
        {
            UNSIGNED16 n, m, y, ia, cl;
            double dSize, dComp;
            UNSIGNED32 crc, ea;
            unsigned mon, day, hh, mm, ss, enc;
            const char *szName, *szComment;
            PHB_ITEM pEntry;

            n = oads_ziplist_u16( buf, &off, ulLen, &ok );
            if( ! ok || off + n > ulLen )
                break;
            szName = ( const char * ) buf + off;
            off += n;
            dSize = oads_ziplist_u64( buf, &off, ulLen, &ok );
            dComp = oads_ziplist_u64( buf, &off, ulLen, &ok );
            m     = oads_ziplist_u16( buf, &off, ulLen, &ok );
            crc   = oads_ziplist_u32( buf, &off, ulLen, &ok );
            y     = oads_ziplist_u16( buf, &off, ulLen, &ok );
            if( ! ok || off + 5 > ulLen )
                break;
            mon = buf[ off++ ];
            day = buf[ off++ ];
            hh  = buf[ off++ ];
            mm  = buf[ off++ ];
            ss  = buf[ off++ ];
            ia  = oads_ziplist_u16( buf, &off, ulLen, &ok );
            ea  = oads_ziplist_u32( buf, &off, ulLen, &ok );
            (void) ea;  /* external attrs: kept on the wire for future use */
            if( ! ok || off + 1 > ulLen )
                break;
            enc = buf[ off++ ] ? 1 : 0;
            cl  = oads_ziplist_u16( buf, &off, ulLen, &ok );
            if( ! ok || off + cl > ulLen )
                break;
            szComment = ( const char * ) buf + off;
            off += cl;

            if( ! lVerbose )
            {
                PHB_ITEM pName = hb_itemPutCL( NULL, szName, n );
                hb_arrayAddForward( pArray, pName );
                hb_itemRelease( pName );
            }
            else
            {
                char szTime[ 9 ];
                char szCrc[ 9 ];
                int nRatio = 0;
                pEntry = hb_itemArrayNew( 11 );
                hb_arraySetCL( pEntry, 1, szName, n );
                hb_arraySetND( pEntry, 2, dSize );
                hb_arraySetNI( pEntry, 3, m );
                hb_arraySetND( pEntry, 4, dComp );
                if( dSize > 0.0 )
                    nRatio = ( int ) ( dComp * 100.0 / dSize + 0.5 );
                hb_arraySetNI( pEntry, 5, nRatio );
                hb_arraySetDL( pEntry, 6,
                               y >= 1980 ? hb_dateEncode( y, mon, day )
                                         : 0 );
                hb_snprintf( szTime, sizeof( szTime ), "%02u:%02u:%02u",
                             hh, mm, ss );
                hb_arraySetC( pEntry, 7, szTime );
                hb_snprintf( szCrc, sizeof( szCrc ), "%08X",
                             ( unsigned ) crc );
                hb_arraySetC( pEntry, 8, szCrc );
                hb_arraySetNI( pEntry, 9, ia );
                hb_arraySetL( pEntry, 10, enc ? HB_TRUE : HB_FALSE );
                hb_arraySetCL( pEntry, 11, szComment, cl );
                hb_arrayAddForward( pArray, pEntry );
                hb_itemRelease( pEntry );
            }
        }
    }
    hb_xfree( buf );
    hb_itemReturnRelease( pArray );
}
