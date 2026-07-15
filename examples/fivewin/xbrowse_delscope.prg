// xbrowse_delscope.prg — FiveWin xBrowse over a *remote* Advantage table
// with SET DELETED ON and an index scope, through Harbour's rddads →
// OpenADS' openace64.dll → openads_serverd over TCP.
//
// Repro harness for the community report "browse shows a duplicate of
// the previous item in the space where the deleted record exists": the
// table has rows deleted *inside* the scoped range, so a correct browse
// must show each surviving key exactly once and no deleted row.
//
// The table (delscope.dbf, 20 rows CODE C001..C020, structural .cdx TAG
// CODE) is staged on the server on first run through the same remote
// connection; rows 3, 7 and 10 are deleted — 7 and 10 fall inside the
// scope C005..C015. Expected visible rows in scope:
//   C005 C006 C008 C009 C011 C012 C013 C014 C015   (9 rows)
//
// Server URI: env var OPENADS_XS_REMOTE, else the documented dev box.
//
// Build:  build_msvc64.cmd  <openads-ace64-dir>  xbrowse_delscope
// Run:    xbrowse_delscope.exe          (interactive xBrowse window)
//         xbrowse_delscope.exe /auto    (headless walk; writes
//                                        %TEMP%\openads_fwh_delscope\result.log)

#include "FiveWin.ch"
#include "xbrowse.ch"

REQUEST ADSCDX
REQUEST DBFCDX
// FWH's xBrowse SetRDD() macro-builds bKeyNo/bKeyCount for ADS workareas
// referencing these by name; the linker would dead-strip them otherwise.
REQUEST ADSKEYNO, ADSKEYCOUNT, ADSGETRELKEYPOS, ADSSETRELKEYPOS

#define ADS_REMOTE_SERVER 2
#define ADS_CDX           2

#define SCOPE_TOP    "C005"
#define SCOPE_BOTTOM "C015"

STATIC cLog := ""

FUNCTION Main( cMode, cStageDir )

   LOCAL oWnd, oBrw
   LOCAL cM    := iif( ValType( cMode ) == "C", Lower( AllTrim( cMode ) ), "" )
   LOCAL lAuto := ( cM == "/auto" )
   LOCAL cUri  := GetEnv( "OPENADS_XS_REMOTE" )
   LOCAL hConn := 0
   LOCAL rc

   IF Empty( cUri ) ; cUri := "tcp://192.168.18.184:16262//tmp/openads_mac" ; ENDIF

   // Real-world rddads/FWH apps run SET DELETED ON *before* connecting —
   // that ordering is part of what the server must honour.
   SET DELETED ON

   AdsSetFileType( ADS_CDX )

   // /stage [dir]: build delscope.dbf/.cdx locally (ADS_LOCAL_SERVER) so
   // the files can be copied into the server's data dir — the wire
   // protocol has no remote CreateTable yet. Writes result.log too.
   IF cM == "/stage"
      rc := StageLocal( iif( Empty( cStageDir ), ".", cStageDir ) )
      WriteLog()
      RETURN rc
   ENDIF

   IF ! AdsConnect60( cUri, ADS_REMOTE_SERVER, NIL, NIL, 0, @hConn )
      MsgStop( "AdsConnect60 failed (remote)" + CRLF + "URI: " + cUri )
      RETURN 1
   ENDIF

   // Browse the staged table the way an end-user app does: shared,
   // ordered by CODE, scoped to C005..C015, SET DELETED ON.
   rc := ErrorBlock( {| e | Break( e ) } )
   BEGIN SEQUENCE
      USE delscope VIA "ADSCDX" ALIAS DS SHARED NEW
   RECOVER
   END SEQUENCE
   ErrorBlock( rc )
   IF Select( "DS" ) == 0
      LogLine( "FAIL: USE delscope (remote) — stage it first: " + ;
               "xbrowse_delscope /stage, then copy delscope.dbf/.cdx " + ;
               "into the server data dir" )
      IF lAuto ; WriteLog() ; ELSE ; MsgStop( cLog ) ; ENDIF
      AdsDisConnect( hConn )
      RETURN 1
   ENDIF
   DS->( OrdSetFocus( "CODE" ) )
   DS->( OrdScope( 0, SCOPE_TOP ) )
   DS->( OrdScope( 1, SCOPE_BOTTOM ) )
   DS->( DbGoTop() )

   IF lAuto
      rc := AutoWalk()
      DS->( DbCloseArea() )
      AdsDisConnect( hConn )
      WriteLog()
      RETURN rc
   ENDIF

   DEFINE WINDOW oWnd FROM 1, 1 TO 28, 110 ;
      TITLE "OpenADS remote — SET DELETED ON + scope " + SCOPE_TOP + ".." + SCOPE_BOTTOM + ;
            "  (expect 9 rows: C005 C006 C008 C009 C011..C015)  |  " + AdsVersion()

   @ 0, 0 XBROWSE oBrw OF oWnd ;
      ALIAS "DS" AUTOCOLS AUTOSORT ;
      CELL LINES NOBORDER FOOTERS

   oBrw:CreateFromCode()
   oWnd:oClient := oBrw

   ACTIVATE WINDOW oWnd ;
      ON INIT  ( oBrw:SetFocus(), oBrw:GoTop(), oBrw:Refresh() ) ;
      ON RESIZE ( oBrw:adjust(), oBrw:Refresh() )

   DS->( DbCloseArea() )
   AdsDisConnect( hConn )
   RETURN 0

//----------------------------------------------------------------------------//
// /stage: build delscope.dbf + structural .cdx locally through the ADS
// local server: 20 rows, TAG CODE, recnos 3/7/10 deleted. Copy the two
// files into the server's data dir afterwards.

STATIC FUNCTION StageLocal( cDir )

   LOCAL aStru := { { "CODE", "C",  6, 0 }, ;
                    { "NAME", "C", 20, 0 }, ;
                    { "CITY", "C", 15, 0 } }
   LOCAL aCity := { "Madrid", "Barcelona", "Valencia", "Sevilla", "Bilbao" }
   LOCAL cDbf  := cDir + "\delscope.dbf"
   LOCAL i

   AdsSetServerType( 1 )  // ADS_LOCAL_SERVER

   IF File( cDbf )
      LogLine( "stage: " + cDbf + " already exists — delete it to restage" )
      RETURN 1
   ENDIF

   DbCreate( cDbf, aStru, "ADSCDX" )
   USE ( cDbf ) VIA "ADSCDX" ALIAS STG EXCLUSIVE NEW
   IF Select( "STG" ) == 0
      LogLine( "FAIL: stage — cannot create/open " + cDbf )
      RETURN 1
   ENDIF

   FOR i := 1 TO 20
      STG->( DbAppend() )
      STG->CODE := "C" + StrZero( i, 3 )
      STG->NAME := "Customer " + StrZero( i, 3 )
      STG->CITY := aCity[ ( i - 1 ) % Len( aCity ) + 1 ]
   NEXT
   STG->( DbCommit() )

   INDEX ON STG->CODE TAG CODE

   // Rows 7 and 10 are *inside* the scope C005..C015; row 3 is outside.
   FOR EACH i IN { 3, 7, 10 }
      STG->( DbGoto( i ) )
      STG->( DbDelete() )
   NEXT
   STG->( DbCommit() )
   STG->( DbCloseArea() )
   LogLine( "stage: created " + cDbf + " (+.cdx) — 20 rows, TAG CODE, deleted recnos 3/7/10" )
   RETURN 0

//----------------------------------------------------------------------------//
// /auto: walk the scoped, ordered workarea both ways; flag deleted rows,
// duplicates and any deviation from the expected key list.

STATIC FUNCTION AutoWalk()

   LOCAL aExpect := { "C005", "C006", "C008", "C009", "C011", ;
                      "C012", "C013", "C014", "C015" }
   LOCAL aDown := {}, aUp := {}
   LOCAL nFail := 0
   LOCAL i, cSeen

   DS->( DbGoTop() )
   DO WHILE ! DS->( Eof() ) .AND. Len( aDown ) < 100
      AAdd( aDown, { DS->( RecNo() ), AllTrim( DS->CODE ), DS->( Deleted() ) } )
      DS->( DbSkip( 1 ) )
   ENDDO

   DS->( DbGoBottom() )
   DO WHILE ! DS->( Bof() ) .AND. Len( aUp ) < 100
      hb_AIns( aUp, 1, { DS->( RecNo() ), AllTrim( DS->CODE ), DS->( Deleted() ) }, .T. )
      DS->( DbSkip( -1 ) )
      IF DS->( Bof() ) ; EXIT ; ENDIF
   ENDDO

   cSeen := ""
   FOR i := 1 TO Len( aDown )
      cSeen += iif( i == 1, "", " " ) + aDown[ i ][ 2 ] + ;
               "(" + AllTrim( Str( aDown[ i ][ 1 ] ) ) + ;
               iif( aDown[ i ][ 3 ], ",DELETED", "" ) + ")"
   NEXT
   LogLine( "walk down: " + cSeen )

   cSeen := ""
   FOR i := 1 TO Len( aUp )
      cSeen += iif( i == 1, "", " " ) + aUp[ i ][ 2 ] + ;
               "(" + AllTrim( Str( aUp[ i ][ 1 ] ) ) + ;
               iif( aUp[ i ][ 3 ], ",DELETED", "" ) + ")"
   NEXT
   LogLine( "walk up  : " + cSeen )

   nFail += CheckWalk( "down", aDown, aExpect )
   nFail += CheckWalk( "up"  , aUp  , aExpect )

   IF nFail == 0
      LogLine( "OK: all checks passed — no deleted rows, no duplicates, expected scoped keys both directions" )
   ELSE
      LogLine( "FAIL: " + AllTrim( Str( nFail ) ) + " check(s) failed" )
   ENDIF
   RETURN iif( nFail == 0, 0, 1 )

//----------------------------------------------------------------------------//

STATIC FUNCTION CheckWalk( cDir, aWalk, aExpect )

   LOCAL nFail := 0
   LOCAL i

   FOR i := 1 TO Len( aWalk )
      IF aWalk[ i ][ 3 ]
         LogLine( "FAIL(" + cDir + "): DELETED row visible: " + aWalk[ i ][ 2 ] + ;
              " recno " + AllTrim( Str( aWalk[ i ][ 1 ] ) ) )
         nFail++
      ENDIF
      IF i > 1 .AND. aWalk[ i ][ 1 ] == aWalk[ i - 1 ][ 1 ]
         LogLine( "FAIL(" + cDir + "): DUPLICATE row (Tim's symptom): " + aWalk[ i ][ 2 ] + ;
              " recno " + AllTrim( Str( aWalk[ i ][ 1 ] ) ) + " served twice in a row" )
         nFail++
      ENDIF
   NEXT

   IF Len( aWalk ) != Len( aExpect )
      LogLine( "FAIL(" + cDir + "): " + AllTrim( Str( Len( aWalk ) ) ) + ;
           " rows walked, expected " + AllTrim( Str( Len( aExpect ) ) ) )
      nFail++
   ELSE
      FOR i := 1 TO Len( aExpect )
         IF aWalk[ i ][ 2 ] != aExpect[ i ]
            LogLine( "FAIL(" + cDir + "): row " + AllTrim( Str( i ) ) + " is " + ;
                 aWalk[ i ][ 2 ] + ", expected " + aExpect[ i ] )
            nFail++
         ENDIF
      NEXT
   ENDIF
   RETURN nFail

//----------------------------------------------------------------------------//

STATIC FUNCTION LogLine( cLine )
   cLog += cLine + CRLF
   ? cLine
   RETURN nil

STATIC FUNCTION WriteLog()
   LOCAL cDir := TempFolder() + "\openads_fwh_delscope"
   IF ! lIsDir( cDir ) ; MakeDir( cDir ) ; ENDIF
   MemoWrit( cDir + "\result.log", cLog )
   RETURN nil

STATIC FUNCTION TempFolder()
   LOCAL c := GetEnv( "TEMP" )
   IF Empty( c ) ; c := GetEnv( "TMP" ) ; ENDIF
   IF Empty( c ) ; c := "C:\Temp" ; ENDIF
   RETURN c
