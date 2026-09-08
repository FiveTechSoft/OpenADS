/*
 * openads_ado_bridge.prg — FiveWin Class B shim (TDataBase:SqlQuery path).
 *
 * Drop-in module: patch FWH adofuncs.prg per adofuncs_openads.patch, then
 * #include or link this unit before adofuncs in your .hbp.
 *
 * Activation:
 *   FW_OpenAdoConnection( "OPENADS,mariadb://user@host/db" )
 *   FW_OpenAdoConnection( { "OPENADS", "mariadb://..." } )
 *   set OPENADS_ADO_BRIDGE=1  +  OPENADS_CONNECT_URI or OPENADS_DATA_DIR
 *
 * Requires: Harbour rddads + OpenADS openace64.dll on PATH.
 */

#include "ads.ch"
#include "hbclass.ch"

#define OPENADS_ADO_TAG  "OPENADS"

/* --- URI / env parsing --------------------------------------------------- */

STATIC FUNCTION OpenAds_Env( cName, cDefault )
   LOCAL c := GetEnv( cName )
   IF Empty( c )
      c := cDefault
   ENDIF
RETURN c

STATIC FUNCTION OpenAds_ParseBridgeSpec( xSpec, cUri, cUser, cPass )

   LOCAL cSpec := ""
   LOCAL nAt
   LOCAL cTail

   cUri  := ""
   cUser := ""
   cPass := ""

   IF ValType( xSpec ) == "A"
      IF Len( xSpec ) >= 1 .AND. ValType( xSpec[ 1 ] ) == "C" .AND. ;
         Upper( AllTrim( xSpec[ 1 ] ) ) == OPENADS_ADO_TAG
         IF Len( xSpec ) >= 2 .AND. ValType( xSpec[ 2 ] ) == "C"
            cUri := AllTrim( xSpec[ 2 ] )
         ENDIF
         IF Len( xSpec ) >= 3 .AND. ValType( xSpec[ 3 ] ) == "C"
            cUser := AllTrim( xSpec[ 3 ] )
         ENDIF
         IF Len( xSpec ) >= 4 .AND. ValType( xSpec[ 4 ] ) == "C"
            cPass := AllTrim( xSpec[ 4 ] )
         ENDIF
      ENDIF
   ELSEIF ValType( xSpec ) == "C"
      cSpec := AllTrim( xSpec )
      IF Upper( Left( cSpec, Len( OPENADS_ADO_TAG ) + 1 ) ) == ;
         OPENADS_ADO_TAG + ","
         cTail := SubStr( cSpec, Len( OPENADS_ADO_TAG ) + 2 )
         nAt   := At( ",", cTail )
         IF nAt > 0
            cUri  := AllTrim( Left( cTail, nAt - 1 ) )
            cTail := SubStr( cTail, nAt + 1 )
            nAt   := At( ",", cTail )
            IF nAt > 0
               cUser := AllTrim( Left( cTail, nAt - 1 ) )
               cPass := AllTrim( SubStr( cTail, nAt + 1 ) )
            ELSE
               cUser := AllTrim( cTail )
            ENDIF
         ELSE
            cUri := AllTrim( cTail )
         ENDIF
      ENDIF
   ENDIF

   IF Empty( cUri )
      IF OpenAds_Env( "OPENADS_ADO_BRIDGE", "" ) == "1"
         cUri := OpenAds_Env( "OPENADS_CONNECT_URI", "" )
         IF Empty( cUri )
            cUri := OpenAds_Env( "OPENADS_DATA_DIR", "." )
         ENDIF
         IF Empty( cUser )
            cUser := OpenAds_Env( "OPENADS_CONNECT_USER", "" )
         ENDIF
         IF Empty( cPass )
            cPass := OpenAds_Env( "OPENADS_CONNECT_PASSWORD", "" )
         ENDIF
      ENDIF
   ENDIF

RETURN ! Empty( cUri )

STATIC FUNCTION OpenAds_AceConnect( cUri, cUser, cPass )

   LOCAL hConn := 0

   IF Empty( cUri )
      RETURN 0
   ENDIF

   IF ! OADS_Connect60( cUri, ADS_LOCAL_SERVER, ;
         iif( Empty( cUser ), NIL, cUser ), ;
         iif( Empty( cPass ), NIL, cPass ), 0, @hConn )
      hConn := 0
   ENDIF

RETURN hConn

/* --- pseudo-ADO field metadata ------------------------------------------- */

CREATE CLASS TOpenAdsFields

   EXPORTED:
   DATA aNames INIT {}

   METHOD New( a ) CONSTRUCTOR
   METHOD Count()        INLINE Len( ::aNames )
   METHOD FieldName( n ) INLINE ::aNames[ n + 1 ]

ENDCLASS

METHOD New( a ) CLASS TOpenAdsFields
   ::aNames := a
   RETURN Self

/* --- pseudo-ADO recordset ------------------------------------------------ */

CREATE CLASS TOpenAdsRecordSet

   EXPORTED:
   DATA hConn    INIT 0
   DATA hStmt    INIT 0
   DATA hCursor   INIT 0
   DATA oFields   INIT NIL
   DATA aNames    INIT {}
   DATA lOpen     INIT .F.

   METHOD New( hConn, cSql ) CONSTRUCTOR
   METHOD Open( cSql )
   METHOD Close()
   METHOD RecordCount()
   METHOD GetRows( nRows, nStart )
   METHOD MoveFirst()
   METHOD IsOpenAds() INLINE .T.

ENDCLASS

METHOD New( hConn, cSql ) CLASS TOpenAdsRecordSet

   ::hConn  := hConn
   ::hStmt  := 0
   ::hCursor := 0
   ::oFields := NIL
   ::aNames := {}
   ::lOpen  := .F.

   IF ! ::Open( cSql )
      RETURN Self
   ENDIF

RETURN Self

METHOD Open( cSql ) CLASS TOpenAdsRecordSet

   LOCAL hCur := 0
   LOCAL nCols := 0
   LOCAL i
   LOCAL cName := Space( 64 )
   LOCAL nLen := 64

   IF ::lOpen
      ::Close()
   ENDIF

   IF ::hConn == 0
      RETURN .F.
   ENDIF

   ::hStmt := OADS_CreateStmt( ::hConn )
   IF ::hStmt == 0
      RETURN .F.
   ENDIF

   IF ! OADS_ExecDirect( ::hStmt, cSql, @hCur )
      AdsCloseSQLStatement( ::hStmt )
      ::hStmt := 0
      RETURN .F.
   ENDIF

   ::hCursor := hCur
   ::lOpen   := .T.

   IF AdsGetNumFields( ::hCursor, @nCols ) != 0
      nCols := 0
   ENDIF

   ::aNames := Array( nCols )
   FOR i := 1 TO nCols
      nLen := 64
      IF AdsGetFieldName( ::hCursor, i, @cName, @nLen ) == 0
         ::aNames[ i ] := AllTrim( Left( cName, nLen ) )
      ELSE
         ::aNames[ i ] := "F" + LTrim( Str( i ) )
      ENDIF
   NEXT

   ::oFields := TOpenAdsFields():New( ::aNames )

RETURN .T.

METHOD Close() CLASS TOpenAdsRecordSet

   IF ::hStmt != 0
      AdsCloseSQLStatement( ::hStmt )
      ::hStmt := 0
   ENDIF
   ::hCursor := 0
   ::lOpen   := .F.

RETURN NIL

METHOD RecordCount() CLASS TOpenAdsRecordSet

   LOCAL n := 0

   IF ::lOpen
      n := OADS_RecCount( ::hCursor )
   ENDIF

RETURN n

METHOD MoveFirst() CLASS TOpenAdsRecordSet

   IF ::lOpen
      AdsGotoTop( ::hCursor )
   ENDIF

RETURN NIL

METHOD GetRows( nRows, nStart ) CLASS TOpenAdsRecordSet

   hb_default( @nRows, -1 )
   hb_default( @nStart, 0 )

   IF ! ::lOpen .OR. Len( ::aNames ) == 0
      RETURN {}
   ENDIF

   /* The whole row x column fetch runs in C (OADS_FetchCols): one ACE hop
      per cell, reusable buffer, no per-cell Harbour string allocation, and
      EOF detected via AdsAtEOF -- the column-major result is identical to
      the pure-Harbour loop it replaces. */
RETURN OADS_FetchCols( ::hCursor, ::aNames, nRows, nStart )

/* --- pseudo-ADO connection --------------------------------------------- */

CREATE CLASS TOpenAdsConnection

   EXPORTED:
   DATA hConn     INIT 0
   DATA cUri      INIT ""
   DATA nState    INIT 1
   DATA cConnStr  INIT ""

   METHOD New( cUri, cUser, cPass ) CONSTRUCTOR
   METHOD Execute( cSql )
   METHOD Close()
   METHOD State() INLINE ::nState
   METHOD IsOpenAds() INLINE .T.

ENDCLASS

METHOD New( cUri, cUser, cPass ) CLASS TOpenAdsConnection

   ::cUri     := cUri
   ::cConnStr := "OPENADS," + cUri
   ::hConn    := OpenAds_AceConnect( cUri, cUser, cPass )
   ::nState   := iif( ::hConn != 0, 1, 0 )

RETURN Self

METHOD Close() CLASS TOpenAdsConnection

   IF ::hConn != 0
      OADS_Disconnect( ::hConn )
      ::hConn := 0
   ENDIF
   ::nState := 0

RETURN NIL

METHOD Execute( cSql ) CLASS TOpenAdsConnection

   LOCAL hStmt := 0
   LOCAL hCur  := 0
   LOCAL nAff  := 0

   IF ::hConn == 0
      RETURN NIL
   ENDIF

   hStmt := OADS_CreateStmt( ::hConn )
   IF hStmt == 0
      RETURN NIL
   ENDIF

   IF ! OADS_ExecDirect( hStmt, cSql, @hCur )
      AdsCloseSQLStatement( hStmt )
      RETURN NIL
   ENDIF

   /* A SELECT-shaped passthrough hands back a navigable cursor; surface its
      row count as ADO RecordsAffected. DML returns no cursor and the
      passthrough exposes no affected-row count, so nAff stays 0. */
   IF ! Empty( hCur )
      nAff := OADS_RecCount( hCur )
   ENDIF

   AdsCloseSQLStatement( hStmt )

RETURN nAff

/* --- entry points called from patched FWH adofuncs.prg ------------------- */

FUNCTION OpenAds_AdoTryBridge( xSpec, lShowError, oErr )

   LOCAL cUri, cUser, cPass
   LOCAL oCn
   LOCAL nErr := 0, cMsg := Space( 256 ), nMsgLen := 256

   hb_default( @lShowError, .F. )

   IF ! OpenAds_ParseBridgeSpec( xSpec, @cUri, @cUser, @cPass )
      RETURN NIL
   ENDIF

   oCn := TOpenAdsConnection():New( cUri, cUser, cPass )
   IF oCn:hConn == 0
      OADS_GetLastError( @nErr, @cMsg, @nMsgLen )
      /* Hand the caller an { code, message } pair (ADO error-object shape)
         when it passed an @oErr var to receive it. */
      oErr := { nErr, AllTrim( Left( cMsg, nMsgLen ) ) }
#ifndef OPENADS_ADO_NO_FWH
      IF lShowError
         MsgAlert( "OpenADS AdsConnect60 failed for URI:" + hb_eol() + cUri + ;
            hb_eol() + AllTrim( Left( cMsg, nMsgLen ) ), "OPENADS ADO bridge" )
      ENDIF
#endif
      RETURN NIL
   ENDIF

RETURN oCn

FUNCTION OpenAds_OpenRecordSet( oCn, cSql )

   LOCAL oRs

   IF ! HB_ISOBJECT( oCn ) .OR. ! oCn:IsOpenAds()
      RETURN NIL
   ENDIF

   oRs := TOpenAdsRecordSet():New( oCn:hConn, cSql )

RETURN iif( oRs:lOpen, oRs, NIL )

/* Minimal RsGetRows equivalent (no FWH link required for console smoke). */

FUNCTION OpenAds_RsGetRows( oRs )

   LOCAL aCols
   LOCAL aRows := {}
   LOCAL nCols, nRows, r, c

   IF ! HB_ISOBJECT( oRs ) .OR. ! oRs:lOpen
      RETURN {}
   ENDIF

   aCols := oRs:GetRows()
   nCols := Len( aCols )
   IF nCols == 0
      RETURN {}
   ENDIF

   nRows := Len( aCols[ 1 ] )
   aRows := Array( nRows )
   FOR r := 1 TO nRows
      aRows[ r ] := Array( nCols )
      FOR c := 1 TO nCols
         aRows[ r ][ c ] := aCols[ c ][ r ]
      NEXT
   NEXT

RETURN aRows

/* --- ACE cursor glue (C) ------------------------------------------------- *
 * rddads exposes only the high-level Ads* helpers (Connect / SQL statement /
 * RecordCount) as Harbour-callable functions. The cursor-level ACE entry
 * points the pseudo-ADO classes drive (GotoTop / Skip / field metadata and
 * value reads, SQL-statement close) are linked here straight from OpenADS'
 * import library, so the shim can walk and read a SQL cursor. This is also
 * the fast lane the row fetch moves into: one ABI hop per cell, in C, with
 * no per-call Harbour string allocation.
 */
#pragma BEGINDUMP

#include "hbapi.h"
#include "hbapiitm.h"
#include "openads/ace.h"
#include <string.h>

HB_FUNC( OADS_CONNECT60 )    /* ( uri, server, user, pass, opts, @hConn ) -> .T./.F. */
{
   ADSHANDLE    hConn = 0;
   const char * szUri = hb_parc( 1 );
   UNSIGNED32   ulRc  = 1;   /* non-zero = error: no URI supplied */
   if( szUri )
      ulRc = AdsConnect60( ( UNSIGNED8 * ) szUri,
                           ( UNSIGNED16 ) hb_parni( 2 ),
                           ( UNSIGNED8 * ) hb_parc( 3 ),
                           ( UNSIGNED8 * ) hb_parc( 4 ),
                           ( UNSIGNED32 ) hb_parnl( 5 ),
                           &hConn );
   hb_stornint( ( HB_MAXINT ) hConn, 6 );
   hb_retl( ulRc == 0 );
}

HB_FUNC( OADS_DISCONNECT )   /* ( hConn ) -> .T./.F. */
{
   hb_retl( AdsDisconnect( ( ADSHANDLE ) hb_parnint( 1 ) ) == 0 );
}

HB_FUNC( OADS_ADSVERSION )   /* () -> full "1.09.27", not SAP "1.9a" */
{
   UNSIGNED32 ulMajor = 0, ulMinor = 0;
   UNSIGNED8  ucLetter = 0;
   char       szDesc[ 128 ];
   UNSIGNED16 usLen = ( UNSIGNED16 ) sizeof( szDesc );
   char       szVer[ 32 ];

   AdsGetVersion( &ulMajor, &ulMinor, &ucLetter, ( UNSIGNED8 * ) szDesc, &usLen );
   szDesc[ sizeof( szDesc ) - 1 ] = '\0';
   /* Desc is "OpenADS 1.09.27 ACE-compatible engine": take the token
      after "OpenADS " so Harbour sees the full dotted build. The
      SAP-shaped major.minor+letter ("1.9a") drops the patch and can
      not tell 1.09.26 from 1.09.27 apart. */
   if( strncmp( szDesc, "OpenADS ", 8 ) == 0 )
   {
      const char * p = szDesc + 8;
      size_t n = strcspn( p, " " );
      if( n > 0 && n < sizeof( szVer ) )
      {
         memcpy( szVer, p, n );
         szVer[ n ] = '\0';
         hb_retc( szVer );
         return;
      }
   }
   hb_snprintf( szVer, sizeof( szVer ), "%u.%u%c",
                ( unsigned ) ulMajor, ( unsigned ) ulMinor, ( char ) ucLetter );
   hb_retc( szVer );
}

HB_FUNC( OADS_SERVERVERSION )   /* ( hConn ) -> server "1.09.27", "" = unknown */
{
   char       szVer[ 64 ];
   UNSIGNED16 usCap = ( UNSIGNED16 ) sizeof( szVer );
   UNSIGNED16 usLen = usCap;
   UNSIGNED32 ulRc;

   ulRc = AdsGetServerVersion( ( ADSHANDLE ) hb_parnint( 1 ),
                               ( UNSIGNED8 * ) szVer, &usLen );
   szVer[ sizeof( szVer ) - 1 ] = '\0';
   if( ulRc != 0 )
      hb_retc( "" );
   else if( usLen < usCap )
      hb_retc( szVer );
   else
      hb_retclen( szVer, usCap - 1 );
}

HB_FUNC( OADS_GETLASTERROR ) /* ( @code, @msg, @len ) -> rc */
{
   UNSIGNED32 ulCode = 0;
   char       szBuf[ 512 ];
   UNSIGNED16 usLen  = ( UNSIGNED16 ) sizeof( szBuf );
   UNSIGNED32 ulRc   = AdsGetLastError( &ulCode,
                                        ( UNSIGNED8 * ) szBuf, &usLen );
   hb_stornl( ( long ) ulCode, 1 );
   /* Store straight into the by-reference @msg: hb_storclen accepts a caller
      var of any prior type (NIL included). Gating on HB_IT_STRING would drop
      the message whenever the caller passes an uninitialised var to receive it. */
   hb_storclen( szBuf, usLen, 2 );
   hb_storni( ( int ) usLen, 3 );
   hb_retnl( ( long ) ulRc );
}

HB_FUNC( ADSOPENTABLE )
{
   ADSHANDLE  hTable = 0;
   UNSIGNED8 *       pAlias = HB_ISNIL( 3 ) ? NULL : ( UNSIGNED8 * ) hb_parc( 3 );
   UNSIGNED32 ulRc   = AdsOpenTable( ( ADSHANDLE ) hb_parnint( 1 ),
                                     ( UNSIGNED8 * ) hb_parc( 2 ),
                                     pAlias,
                                     ( UNSIGNED16 ) hb_parni( 4 ),
                                     ( UNSIGNED16 ) hb_parni( 5 ),
                                     ( UNSIGNED16 ) hb_parni( 6 ),
                                     ( UNSIGNED16 ) hb_parni( 7 ),
                                     ( UNSIGNED16 ) hb_parni( 8 ),
                                     &hTable );
   hb_stornint( ( HB_MAXINT ) hTable, 9 );
   hb_retnl( ( long ) ulRc );
}

HB_FUNC( ADSCLOSETABLE )
{
   hb_retnl( ( long ) AdsCloseTable( ( ADSHANDLE ) hb_parnint( 1 ) ) );
}

HB_FUNC( ADSATEOF )
{
   UNSIGNED16 usEof = 0;
   UNSIGNED32 ulRc  = AdsAtEOF( ( ADSHANDLE ) hb_parnint( 1 ), &usEof );
   hb_storni( ( int ) usEof, 2 );
   hb_retnl( ( long ) ulRc );
}

HB_FUNC( ADSGOTOTOP )
{
   hb_retnl( ( long ) AdsGotoTop( ( ADSHANDLE ) hb_parnint( 1 ) ) );
}

HB_FUNC( ADSSKIP )
{
   hb_retnl( ( long ) AdsSkip( ( ADSHANDLE ) hb_parnint( 1 ),
                               ( SIGNED32 ) hb_parnl( 2 ) ) );
}

HB_FUNC( ADSCLOSESQLSTATEMENT )
{
   hb_retnl( ( long ) AdsCloseSQLStatement( ( ADSHANDLE ) hb_parnint( 1 ) ) );
}

HB_FUNC( ADSGETNUMFIELDS )
{
   UNSIGNED16 usFields = 0;
   UNSIGNED32 ulRc = AdsGetNumFields( ( ADSHANDLE ) hb_parnint( 1 ), &usFields );
   hb_storni( ( int ) usFields, 2 );
   hb_retnl( ( long ) ulRc );
}

HB_FUNC( ADSGETFIELDNAME )
{
   char       szName[ 256 ];
   UNSIGNED16 usLen = ( UNSIGNED16 ) sizeof( szName );
   UNSIGNED32 ulRc  = AdsGetFieldName( ( ADSHANDLE ) hb_parnint( 1 ),
                                       ( UNSIGNED16 ) hb_parni( 2 ),
                                       ( UNSIGNED8 * ) szName, &usLen );
   if( ulRc == 0 )
      hb_storclen( szName, usLen, 3 );
   hb_storni( ( int ) usLen, 4 );
   hb_retnl( ( long ) ulRc );
}

HB_FUNC( ADSGETFIELD )
{
   ADSHANDLE  hCursor = ( ADSHANDLE ) hb_parnint( 1 );
   UNSIGNED32 ulLen   = ( UNSIGNED32 ) hb_parnl( 4 );
   char *     pBuf;
   UNSIGNED32 ulRc;

   if( ulLen == 0 )
      ulLen = 4096;
   pBuf = ( char * ) hb_xgrab( ulLen + 1 );
   ulRc = AdsGetField( hCursor, ( UNSIGNED8 * ) hb_parc( 2 ),
                       ( UNSIGNED8 * ) pBuf, &ulLen, ( UNSIGNED16 ) hb_parni( 5 ) );
   if( ulRc == 0 )
      hb_storclen( pBuf, ulLen, 3 );
   hb_stornl( ( long ) ulLen, 4 );
   hb_xfree( pBuf );
   hb_retnl( ( long ) ulRc );
}

/*
 * SQL-statement glue, C ABI direct. rddads' own AdsCreateSQLStatement /
 * AdsExecuteSQLDirect / AdsGetRecordCount wrappers report success but drop
 * the OUT handle/count here (the statement handle comes back 0), so the shim
 * calls the ACE entry points itself -- exactly as OpenADS' own server does.
 */
HB_FUNC( OADS_CREATESTMT )   /* ( hConn ) -> statement handle, 0 on failure */
{
   ADSHANDLE  hStmt = 0;
   UNSIGNED32 ulRc  = AdsCreateSQLStatement( ( ADSHANDLE ) hb_parnint( 1 ), &hStmt );
   hb_retnint( ulRc == 0 ? ( HB_MAXINT ) hStmt : 0 );
}

HB_FUNC( OADS_EXECDIRECT )   /* ( hStmt, cSql, @hCursor ) -> .T./.F. */
{
   ADSHANDLE    hCur  = 0;
   const char * szSql = hb_parc( 2 );
   UNSIGNED32   ulRc  = 1;   /* non-zero = error: no SQL supplied */
   if( szSql )
      ulRc = AdsExecuteSQLDirect( ( ADSHANDLE ) hb_parnint( 1 ),
                                  ( UNSIGNED8 * ) szSql, &hCur );
   hb_stornint( ( HB_MAXINT ) hCur, 3 );
   hb_retl( ulRc == 0 );
}

HB_FUNC( OADS_RECCOUNT )     /* ( hCursor ) -> row count */
{
   UNSIGNED32 ulCount = 0;
   AdsGetRecordCount( ( ADSHANDLE ) hb_parnint( 1 ), 0, &ulCount );
   hb_retnint( ( HB_MAXINT ) ulCount );
}

HB_FUNC( OADS_NAVRECCOUNT )  /* ( hTable ) -> row count via ACE (not rddads) */
{
   UNSIGNED32 ulCount = 0;
   if( AdsGetRecordCount( ( ADSHANDLE ) hb_parnint( 1 ), 0, &ulCount ) != 0 )
      ulCount = 0;
   hb_retnint( ( HB_MAXINT ) ulCount );
}

/*
 * OADS_FetchCols( hCursor, aFieldNames, nRows, nStart ) -> array of columns.
 * The pseudo-ADO GetRows hot path, in C: walk the cursor from the top
 * (optionally skipping nStart), read every cell with one reusable buffer,
 * and return a column-major array { col1[], col2[], ... }. nRows < 0 means
 * "all remaining rows". EOF is detected with AdsAtEOF so the loop always
 * terminates (the Harbour AdsSkip return alone does not flag EOF).
 */
HB_FUNC( OADS_FETCHCOLS )
{
   ADSHANDLE  hCursor = ( ADSHANDLE ) hb_parnint( 1 );
   PHB_ITEM   pNames  = hb_param( 2, HB_IT_ARRAY );
   long       lRows   = hb_parnl( 3 );
   long       lStart  = hb_parnl( 4 );
   HB_SIZE    nCols   = pNames ? hb_arrayLen( pNames ) : 0;
   PHB_ITEM   pResult = hb_itemArrayNew( nCols );
   HB_SIZE    c;
   long       lRead   = 0;
   UNSIGNED16 usEof   = 0;
   UNSIGNED32 ulLen;
   UNSIGNED32 ulCap   = 8192;
   char *     pBuf;
   PHB_ITEM   pVal;

   if( nCols == 0 )
   {
      hb_itemReturnRelease( pResult );
      return;
   }

   /* one growable array per column */
   for( c = 1; c <= nCols; c++ )
   {
      PHB_ITEM pCol = hb_itemArrayNew( 0 );
      hb_arraySet( pResult, c, pCol );
      hb_itemRelease( pCol );
   }

   /* Size the reusable cell buffer to the widest column up front. On this
      engine AdsGetField copies up to the caller's buffer capacity and still
      returns success when it has to truncate -- it neither raises a "value
      truncated" code nor reports the length it needed -- so a fixed 8192
      buffer would silently clip wider CHARACTER columns. AdsGetFieldLength
      gives the declared width with one ACE hop per column, done once before
      the row walk; 8192 stays the floor and a ceiling caps a pathological
      width. (Memo *content* longer than the buffer is still clipped: a memo
      field's declared length is its block-pointer width, not the payload size,
      and the ACE contract here exposes no payload length at this layer -- a
      content-aware read would need AdsGetMemoLength and is out of scope here.) */
   for( c = 1; c <= nCols; c++ )
   {
      const char * szName = hb_arrayGetCPtr( pNames, c );
      UNSIGNED32   ulFLen = 0;
      if( szName &&
          AdsGetFieldLength( hCursor, ( UNSIGNED8 * ) szName, &ulFLen ) == 0 &&
          ulFLen + 1 > ulCap )
         ulCap = ulFLen + 1;
   }
   if( ulCap > 16u * 1024u * 1024u )   /* 16 MB hard ceiling */
      ulCap = 16u * 1024u * 1024u;

   AdsGotoTop( hCursor );
   if( lStart > 0 )
      AdsSkip( hCursor, ( SIGNED32 ) lStart );

   AdsAtEOF( hCursor, &usEof );

   pBuf = ( char * ) hb_xgrab( ulCap );

   while( ! usEof && ( lRows < 0 || lRead < lRows ) )
   {
      for( c = 1; c <= nCols; c++ )
      {
         const char * szName = hb_arrayGetCPtr( pNames, c );
         ulLen = ulCap;
         if( szName &&
             AdsGetField( hCursor, ( UNSIGNED8 * ) szName,
                          ( UNSIGNED8 * ) pBuf, &ulLen, 0 ) == 0 )
            pVal = hb_itemPutCL( NULL, pBuf, ulLen );
         else
            pVal = hb_itemPutCL( NULL, NULL, 0 );
         hb_arrayAdd( hb_arrayGetItemPtr( pResult, c ), pVal );
         hb_itemRelease( pVal );
      }
      lRead++;
      AdsSkip( hCursor, 1 );
      AdsAtEOF( hCursor, &usEof );
   }

   hb_xfree( pBuf );
   hb_itemReturnRelease( pResult );
}

#pragma ENDDUMP