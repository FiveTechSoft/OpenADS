/*
 * oads_hb.c � Harbour HB_FUNC wrappers for the oads_*() C API.
 *
 * Drop this file into your hbmk2 project (or compile as a static lib)
 * to get OADS_FOPEN(), OADS_CHECKEXISTENCE(), OADS_DELETEFILE(), OADS_RENAMEFILE(),
 * OADS_FCREATE(), OADS_FCLOSE(), OADS_FREAD(), OADS_FWRITE(), OADS_FSEEK(),
 * OADS_GETFILESIZE(), OADS_GETFILEDATE(), OADS_GETFILETIME(), OADS_DIRMAKE(),
 * OADS_DIRREMOVE(), OADS_DIREXIST(), OADS_DIRECTORY() callable from Harbour PRG code.
 * DLL / shared library.  The oads_*() functions are themselves aliases
 * for AdsF*() � same implementation, different name for legacy callers.
 *
 * Calling conventions from PRG:
 *
 *   hFile := OADS_FCreate( hConn, cFileName, nAttribute )
 *   hFile := OADS_FOpen( hConn, cFileName, nMode )
 *   lOk   := OADS_FClose( hFile )
 *   nWritten := OADS_FWrite( hFile, cBuffer )          -- writes all bytes
 *   cBuf  := OADS_FRead( hFile, nLen )                  -- returns string
 *   nPos  := OADS_FSeek( hFile, nOffset, nOrigin )      -- 0=SET,1=CUR,2=END
 *   lOk   := OADS_FWrite( hFile, cBuffer, @nWritten )   -- with byte count
 *   nRead := OADS_FRead( hFile, @cBuffer, nLen )         -- @ form
 *
 * Build (hbmk2):
 *   hbmk2 myproject.hbp oads_hb.c -L/path/to/openads/importlib -lace64
 */

#include "hbapi.h"
#include "hbapiitm.h"
#include "openads/ace.h"


/* ------------------------------------------------------------------ */
/*  OADS_FCreate( hConn, cFileName, nAttribute ) -> hFile (0 on fail) */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_FCREATE )
{
    ADSHANDLE  hConn     = ( ADSHANDLE ) hb_parnint( 1 );
    const char *szName   = hb_parc( 2 );
    UNSIGNED16 usAttr    = ( UNSIGNED16 ) hb_parni( 3 );
    ADSHANDLE  hFile     = 0;
    UNSIGNED32 ulRc      = 1;

    if( szName )
        ulRc = oads_FCreate( hConn, ( UNSIGNED8 * ) szName, usAttr, &hFile );
    hb_retnint( ulRc == 0 ? ( HB_MAXINT ) hFile : 0 );
}

/* ------------------------------------------------------------------ */
/*  OADS_FOpen( hConn, cFileName, nMode ) -> hFile (0 on fail)       */
/*  nMode: 0 = read/write (default), 3 = ADS_READONLY                */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_FOPEN )
{
    ADSHANDLE  hConn     = ( ADSHANDLE ) hb_parnint( 1 );
    const char *szName   = hb_parc( 2 );
    UNSIGNED16 usMode    = ( UNSIGNED16 ) hb_parni( 3 );
    ADSHANDLE  hFile     = 0;
    UNSIGNED32 ulRc      = 1;

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
    ADSHANDLE hFile   = ( ADSHANDLE ) hb_parnint( 1 );
    const char *szBuf = hb_parc( 2 );
    UNSIGNED32 ulLen  = ( UNSIGNED32 ) hb_parclen( 2 );
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
        /* OADS_FWrite( hFile, cBuf, @nWritten ) -> lOk */
        hb_stornl( ( long ) ulWritten, 3 );
        hb_retl( ulRc == 0 );
    }
    else
    {
        /* OADS_FWrite( hFile, cBuf ) -> nBytesWritten */
        hb_retnl( ( long ) ulWritten );
    }
}

/* ------------------------------------------------------------------ */
/*  OADS_FRead( hFile, nLen ) -> cData          (simple form)         */
/*  OADS_FRead( hFile, @cBuf, nLen ) -> nRead   (with @ form)        */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_FREAD )
{
    ADSHANDLE hFile    = ( ADSHANDLE ) hb_parnint( 1 );
    UNSIGNED32 ulLen;
    UNSIGNED32 ulRead  = 0;
    char *pBuf;

    if( hb_pcount() >= 3 && HB_ISBYREF( 2 ) )
    {
        /* @cBuf form: OADS_FRead( hFile, @cBuf, nLen ) -> nRead */
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
        /* simple form: OADS_FRead( hFile, nLen ) -> cData */
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
/*  nOrigin: 0 = SEEK_SET (beginning), 1 = SEEK_CUR, 2 = SEEK_END   */
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
/*  OADS_CheckExistence( hConn, cName ) -> lExists (.T./.F.)          */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_CHECKEXISTENCE )
{
    ADSHANDLE  hConn   = ( ADSHANDLE ) hb_parnint( 1 );
    const char *szName = hb_parc( 2 );
    UNSIGNED16 usExists = 0;

    if( szName )
        oads_CheckExistence( hConn, ( UNSIGNED8 * ) szName, &usExists );
    hb_retl( usExists != 0 );
}

/* ------------------------------------------------------------------ */
/*  OADS_DeleteFile( hConn, cName ) -> lOk                            */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_DELETEFILE )
{
    ADSHANDLE  hConn   = ( ADSHANDLE ) hb_parnint( 1 );
    const char *szName = hb_parc( 2 );
    UNSIGNED32 ulRc    = 1;

    if( szName )
        ulRc = oads_DeleteFile( hConn, ( UNSIGNED8 * ) szName );
    hb_retl( ulRc == 0 );
}

/* ------------------------------------------------------------------ */
/*  OADS_RenameFile( hConn, cOld, cNew ) -> lOk                       */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_RENAMEFILE )
{
    ADSHANDLE  hConn  = ( ADSHANDLE ) hb_parnint( 1 );
    const char *szOld = hb_parc( 2 );
    const char *szNew = hb_parc( 3 );
    UNSIGNED32 ulRc   = 1;

    if( szOld && szNew )
        ulRc = oads_RenameFile( hConn, ( UNSIGNED8 * ) szOld,
                                       ( UNSIGNED8 * ) szNew );
    hb_retl( ulRc == 0 );
}

/* ------------------------------------------------------------------ */
/*  OADS_GetFileSize( hConn, cName ) -> nSize                         */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_GETFILESIZE )
{
    ADSHANDLE  hConn   = ( ADSHANDLE ) hb_parnint( 1 );
    const char *szName = hb_parc( 2 );
    UNSIGNED32 ulSize  = 0;

    if( szName )
        oads_GetFileSize( hConn, ( UNSIGNED8 * ) szName, &ulSize );
    hb_retnl( ( long ) ulSize );
}

/* ------------------------------------------------------------------ */
/*  OADS_GetFileDate( hConn, cName ) -> cDate                         */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_GETFILEDATE )
{
    ADSHANDLE  hConn   = ( ADSHANDLE ) hb_parnint( 1 );
    const char *szName = hb_parc( 2 );
    UNSIGNED8  aucDate[ 32 ] = {};
    UNSIGNED16 usLen   = sizeof( aucDate );

    if( szName )
        oads_GetFileDate( hConn, ( UNSIGNED8 * ) szName, aucDate, &usLen );
    hb_retclen( ( char * ) aucDate, usLen );
}

/* ------------------------------------------------------------------ */
/*  OADS_GetFileTime( hConn, cName ) -> cTime                         */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_GETFILETIME )
{
    ADSHANDLE  hConn   = ( ADSHANDLE ) hb_parnint( 1 );
    const char *szName = hb_parc( 2 );
    UNSIGNED8  aucTime[ 32 ] = {};
    UNSIGNED16 usLen   = sizeof( aucTime );

    if( szName )
        oads_GetFileTime( hConn, ( UNSIGNED8 * ) szName, aucTime, &usLen );
    hb_retclen( ( char * ) aucTime, usLen );
}

/* ------------------------------------------------------------------ */
/*  OADS_DirMake( hConn, cPath ) -> lOk                               */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_DIRMAKE )
{
    ADSHANDLE  hConn  = ( ADSHANDLE ) hb_parnint( 1 );
    const char *szPath = hb_parc( 2 );
    UNSIGNED32 ulRc    = 1;

    if( szPath )
        ulRc = oads_DirMake( hConn, ( UNSIGNED8 * ) szPath );
    hb_retl( ulRc == 0 );
}

/* ------------------------------------------------------------------ */
/*  OADS_DirRemove( hConn, cPath ) -> lOk                             */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_DIRREMOVE )
{
    ADSHANDLE  hConn  = ( ADSHANDLE ) hb_parnint( 1 );
    const char *szPath = hb_parc( 2 );
    UNSIGNED32 ulRc    = 1;

    if( szPath )
        ulRc = oads_DirRemove( hConn, ( UNSIGNED8 * ) szPath );
    hb_retl( ulRc == 0 );
}

/* ------------------------------------------------------------------ */
/*  OADS_DirExist( hConn, cPath ) -> lExists (.T./.F.)                */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_DIREXIST )
{
    ADSHANDLE  hConn   = ( ADSHANDLE ) hb_parnint( 1 );
    const char *szPath = hb_parc( 2 );
    UNSIGNED16 usExists = 0;

    if( szPath )
        oads_DirExist( hConn, ( UNSIGNED8 * ) szPath, &usExists );
    hb_retl( usExists != 0 );
}

/* ------------------------------------------------------------------ */
/*  OADS_Directory( hConn, cMask, nAttr ) -> cBuf                     */
/* ------------------------------------------------------------------ */
HB_FUNC( OADS_DIRECTORY )
{
    ADSHANDLE  hConn  = ( ADSHANDLE ) hb_parnint( 1 );
    const char *szMask = hb_parc( 2 );
    UNSIGNED16 usAttr  = ( UNSIGNED16 ) hb_parni( 3 );
    UNSIGNED32 ulLen   = 0;
    unsigned char *buf;
    UNSIGNED32 ulRc;

    /* First call: get required buffer size */
    if( szMask )
        oads_Directory( hConn, ( UNSIGNED8 * ) szMask, usAttr, NULL, &ulLen );

    if( ulLen == 0 || ulLen > 1024 * 1024 )
    {
        hb_retc( "" );
        return;
    }

    buf = (unsigned char *)hb_xgrab( ulLen );
    ulRc = oads_Directory( hConn, ( UNSIGNED8 * ) szMask, usAttr,
                           buf, &ulLen );
    if( ulRc == 0 )
        hb_retclen( ( char * ) buf, ulLen );
    else
        hb_retc( "" );
    hb_xfree( buf );
}