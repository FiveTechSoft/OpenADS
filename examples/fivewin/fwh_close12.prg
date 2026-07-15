// fwh_close12.prg — repro harness for the community report "closing the
// ~12 tables of an invoice over OpenADS remote takes 4-7 s, and hangs on
// v1.8.13": 12 remote tables (structural .cdx tag + memo .fpt each),
// SET DELETED ON, index order + scope, some edits/appends, then close
// every workarea, timing each DbCloseArea() through the real rddads
// close sequence (GOCOLD -> ORDLISTCLEAR -> CLOSE).
//
// Stage:  fwh_close12 /stage <dir>     (local server; creates INV01..INV12)
// Run:    fwh_close12 /auto            (remote; writes
//                                       %TEMP%\openads_fwh_close12\result.log)
// Server URI: env OPENADS_CLOSE12_URI, default tcp://127.0.0.1:16299/<cwd>

#include "FiveWin.ch"

REQUEST ADSCDX
REQUEST DBFCDX
REQUEST ADSKEYNO, ADSKEYCOUNT, ADSGETRELKEYPOS, ADSSETRELKEYPOS

#define ADS_REMOTE_SERVER 2
#define N_TABLES          12

STATIC cLog := ""

FUNCTION Main( cMode, cArg )

   LOCAL cM    := iif( ValType( cMode ) == "C", Lower( AllTrim( cMode ) ), "" )
   LOCAL cUri  := GetEnv( "OPENADS_CLOSE12_URI" )
   LOCAL hConn := 0
   LOCAL i, cAlias, nT0, nT1, nTotal, nRec

   IF cM == "/stage"
      RETURN StageLocal( iif( Empty( cArg ), ".", cArg ) )
   ENDIF

   IF Empty( cUri )
      LogLine( "FAIL: set OPENADS_CLOSE12_URI" )
      WriteLog()
      RETURN 1
   ENDIF

   SET DELETED ON
   AdsSetFileType( 2 )   // ADS_CDX

   IF ! AdsConnect60( cUri, ADS_REMOTE_SERVER, NIL, NIL, 0, @hConn )
      LogLine( "FAIL: AdsConnect60 " + cUri )
      WriteLog()
      RETURN 1
   ENDIF
   LogLine( "connected: " + cUri )

   // Open the 12 invoice tables: order + scope + a little navigation,
   // one edit and one append each — the state an invoice leaves behind.
   FOR i := 1 TO N_TABLES
      cAlias := "INV" + StrZero( i, 2 )
      USE ( "inv" + StrZero( i, 2 ) ) VIA "ADSCDX" ALIAS ( cAlias ) SHARED NEW
      IF Select( cAlias ) == 0
         LogLine( "FAIL: USE inv" + StrZero( i, 2 ) )
         WriteLog()
         RETURN 1
      ENDIF
      ( cAlias )->( OrdSetFocus( "BYID" ) )
      ( cAlias )->( OrdScope( 0, 1 ) )
      ( cAlias )->( OrdScope( 1, 40 ) )
      ( cAlias )->( DbGoTop() )
      ( cAlias )->( DbSkip( 1 ) )
      ( cAlias )->( DbSkip( 1 ) )
   NEXT
   LogLine( "opened " + AllTrim( Str( N_TABLES ) ) + " tables" )

   FOR i := 1 TO N_TABLES
      cAlias := "INV" + StrZero( i, 2 )
      ( cAlias )->( DbGoTop() )
      ( cAlias )->( DbSkip( 3 ) )
      nRec := ( cAlias )->( RecNo() )
      IF ( cAlias )->( RLock() )
         ( cAlias )->NAME  := "edited " + Time()
         ( cAlias )->NOTES := "memo updated during the invoice pass " + Time()
         ( cAlias )->( DbUnlock() )
      ELSE
         LogLine( "WARN: RLock failed on " + cAlias + " rec " + AllTrim( Str( nRec ) ) )
      ENDIF
      IF ( cAlias )->( DbAppend() )
         ( cAlias )->ID    := 1000 + i
         ( cAlias )->NAME  := "appended"
         ( cAlias )->NOTES := "appended memo"
         ( cAlias )->( DbUnlock() )
      ENDIF
      ( cAlias )->( DbCommit() )
      ( cAlias )->( DbGoTop() )
      ( cAlias )->( DbSkip( 2 ) )
   NEXT
   LogLine( "invoice work done" )

   // The reported failure point: close all the invoice's tables.
   nTotal := hb_MilliSeconds()
   FOR i := 1 TO N_TABLES
      cAlias := "INV" + StrZero( i, 2 )
      nT0 := hb_MilliSeconds()
      ( cAlias )->( DbCloseArea() )
      nT1 := hb_MilliSeconds()
      LogLine( "close " + cAlias + ": " + AllTrim( Str( nT1 - nT0 ) ) + " ms" )
   NEXT
   LogLine( "close all: " + AllTrim( Str( hb_MilliSeconds() - nTotal ) ) + " ms" )

   nT0 := hb_MilliSeconds()
   AdsDisConnect( hConn )
   LogLine( "disconnect: " + AllTrim( Str( hb_MilliSeconds() - nT0 ) ) + " ms" )
   LogLine( "RESULT: complete" )
   WriteLog()
   RETURN 0

//----------------------------------------------------------------------------//

STATIC FUNCTION StageLocal( cDir )

   LOCAL aStru := { { "ID", "N", 8, 0 }, { "NAME", "C", 20, 0 }, ;
                    { "NOTES", "M", 10, 0 } }
   LOCAL i, k, cDbf, cAlias

   AdsSetServerType( 1 )   // ADS_LOCAL_SERVER
   AdsSetFileType( 2 )     // ADS_CDX

   FOR i := 1 TO N_TABLES
      cDbf   := cDir + "\inv" + StrZero( i, 2 ) + ".dbf"
      cAlias := "STG"
      IF File( cDbf )
         LogLine( "stage: " + cDbf + " exists — delete to restage" )
         LOOP
      ENDIF
      DbCreate( cDbf, aStru, "ADSCDX" )
      USE ( cDbf ) VIA "ADSCDX" ALIAS ( cAlias ) EXCLUSIVE NEW
      FOR k := 1 TO 50
         STG->( DbAppend() )
         STG->ID   := k
         STG->NAME := "row " + StrZero( k, 3 )
         IF k % 3 == 0
            STG->NOTES := "memo body long enough to land in the fpt " + StrZero( k, 3 )
         ENDIF
      NEXT
      STG->( DbCommit() )
      INDEX ON STG->ID TAG BYID
      // a deleted row inside the scope, as in the SET DELETED reports
      STG->( DbGoto( 4 ) )
      STG->( DbDelete() )
      STG->( DbCommit() )
      STG->( DbCloseArea() )
      LogLine( "stage: created " + cDbf )
   NEXT
   WriteLog()
   RETURN 0

//----------------------------------------------------------------------------//

STATIC FUNCTION LogLine( cTxt )
   cLog += cTxt + CRLF
   RETURN NIL

STATIC FUNCTION WriteLog()
   LOCAL cDir := GetEnv( "TEMP" ) + "\openads_fwh_close12"
   IF ! lIsDir( cDir ) ; lMkDir( cDir ) ; ENDIF
   MemoWrit( cDir + "\result.log", cLog )
   RETURN NIL
