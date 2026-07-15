// xbpaint_delscope.prg — faithful xBrowse PAINT-pattern walk over a remote
// scoped/ordered table with SET DELETED ON, sized > 200 records so FWH's
// xBrowse takes the AdsGetRelKeyPos scrollbar path (the 20-row demo in
// xbrowse_delscope.prg takes the AdsKeyNo path instead).
//
// xBrowse never walks a table linearly: every repaint anchors on the
// current record, skips forward through the visible rows reading fields,
// evaluates the scrollbar block (AdsGetRelKeyPos over the wire), then
// DbGoto()s back to the anchor and advances one row. That GoTo/Skip
// interleave is exactly what the plain down/up walk in xbrowse_delscope
// does NOT exercise, and it stresses the read-ahead queue + consumed
// counter on every cycle.
//
// Stage:  xbpaint_delscope /stage <dir>   (local; 300 rows, TAG BYCODE,
//                                          deleted rows inside the scope)
// Run:    xbpaint_delscope /auto          (remote; log to
//                                          %TEMP%\openads_fwh_xbpaint\result.log)
// Server URI: env OPENADS_XBPAINT_URI, default tcp://127.0.0.1:16299/<stage dir>

#include "FiveWin.ch"

REQUEST ADSCDX
REQUEST DBFCDX
REQUEST ADSKEYNO, ADSKEYCOUNT, ADSGETRELKEYPOS, ADSSETRELKEYPOS

#define ADS_REMOTE_SERVER 2

#define N_ROWS       300
#define SCOPE_TOP    "C0050"
#define SCOPE_BOTTOM "C0150"
#define PAGE_ROWS    10

STATIC cLog := ""

FUNCTION Main( cMode, cArg )

   LOCAL cM    := iif( ValType( cMode ) == "C", Lower( AllTrim( cMode ) ), "" )
   LOCAL cUri  := GetEnv( "OPENADS_XBPAINT_URI" )
   LOCAL hConn := 0
   LOCAL nFail

   IF cM == "/stage"
      RETURN StageLocal( iif( Empty( cArg ), ".", cArg ) )
   ENDIF

   IF Empty( cUri )
      LogLine( "FAIL: set OPENADS_XBPAINT_URI" )
      WriteLog()
      RETURN 1
   ENDIF

   SET DELETED ON
   AdsSetFileType( 2 )

   IF ! AdsConnect60( cUri, ADS_REMOTE_SERVER, NIL, NIL, 0, @hConn )
      LogLine( "FAIL: AdsConnect60 " + cUri )
      WriteLog()
      RETURN 1
   ENDIF

   LogServerVersion( cUri )

   USE xbpaint VIA "ADSCDX" ALIAS XP SHARED NEW
   IF Select( "XP" ) == 0
      LogLine( "FAIL: USE xbpaint (remote) — stage first" )
      WriteLog()
      AdsDisConnect( hConn )
      RETURN 1
   ENDIF
   XP->( OrdSetFocus( "BYCODE" ) )
   XP->( OrdScope( 0, SCOPE_TOP ) )
   XP->( OrdScope( 1, SCOPE_BOTTOM ) )

   nFail := PaintWalk()
   nFail += UpWalk()
   nFail += ThumbDrag()

   XP->( DbCloseArea() )
   AdsDisConnect( hConn )
   LogLine( iif( nFail == 0, "RESULT: all checks passed", ;
                 "RESULT: " + AllTrim( Str( nFail ) ) + " check(s) FAILED" ) )
   WriteLog()
   RETURN nFail

//----------------------------------------------------------------------------//
// The xBrowse repaint shape: anchor -> read a page forward (evaluating the
// scrollbar block per row, as Paint() does) -> back to anchor -> advance 1.
// Every anchor visited this way must be a live, in-scope, strictly
// ascending key, each exactly once — a duplicate where a deleted row sits
// is precisely the community-reported symptom.

STATIC FUNCTION PaintWalk()

   LOCAL aSeen  := {}
   LOCAL nFail  := 0
   LOCAL nSafe  := 0
   LOCAL nAnchor, cCode, lDel, nPos
   LOCAL aPage, i

   XP->( DbGoTop() )

   DO WHILE ! XP->( Eof() ) .AND. nSafe++ < 500

      cCode := AllTrim( XP->CODE )
      lDel  := XP->( Deleted() )
      AAdd( aSeen, cCode )

      IF lDel
         LogLine( "FAIL: anchor on DELETED row " + cCode + ;
                  " rec " + AllTrim( Str( XP->( RecNo() ) ) ) )
         nFail++
      ENDIF
      IF ! ( cCode >= SCOPE_TOP .AND. cCode <= SCOPE_BOTTOM )
         LogLine( "FAIL: anchor outside scope: " + cCode )
         nFail++
      ENDIF
      IF Len( aSeen ) > 1 .AND. ! ( cCode > aSeen[ Len( aSeen ) - 1 ] )
         LogLine( "FAIL: not ascending / DUPLICATE at " + cCode + ;
                  " (prev " + aSeen[ Len( aSeen ) - 1 ] + ")" )
         nFail++
      ENDIF

      // paint one page from the anchor
      nAnchor := XP->( RecNo() )
      aPage   := {}
      FOR i := 1 TO PAGE_ROWS
         AAdd( aPage, { AllTrim( XP->CODE ), XP->( Deleted() ) } )
         nPos := XP->( AdsGetRelKeyPos() )      // scrollbar eval, wire op
         IF ValType( nPos ) != "N"
            LogLine( "FAIL: AdsGetRelKeyPos returned " + ValType( nPos ) )
            nFail++
         ENDIF
         XP->( DbSkip( 1 ) )
         IF XP->( Eof() ) ; EXIT ; ENDIF
      NEXT
      FOR i := 2 TO Len( aPage )
         IF aPage[ i ][ 1 ] == aPage[ i - 1 ][ 1 ]
            LogLine( "FAIL: page DUPLICATE " + aPage[ i ][ 1 ] + ;
                     " (anchor " + AllTrim( Str( nAnchor ) ) + " row " + ;
                     AllTrim( Str( i ) ) + ")" )
            nFail++
         ENDIF
         IF aPage[ i ][ 2 ]
            LogLine( "FAIL: page shows DELETED row " + aPage[ i ][ 1 ] )
            nFail++
         ENDIF
      NEXT

      XP->( DbGoto( nAnchor ) )                 // restore, as Paint() does
      XP->( DbSkip( 1 ) )                       // arrow-down
   ENDDO

   LogLine( "anchors visited: " + AllTrim( Str( Len( aSeen ) ) ) + ;
            " first " + iif( Len( aSeen ) > 0, aSeen[ 1 ], "?" ) + ;
            " last " + iif( Len( aSeen ) > 0, ATail( aSeen ), "?" ) )

   // C0050..C0150 inclusive is 101 keys, minus the 10 deleted inside
   // (C0060, C0070, ... C0150 stepping 10) = 91 expected anchors.
   IF Len( aSeen ) != 91
      LogLine( "FAIL: expected 91 anchors, saw " + AllTrim( Str( Len( aSeen ) ) ) )
      nFail++
   ENDIF
   RETURN nFail

//----------------------------------------------------------------------------//
// Arrow-up shape: anchor moves UPWARD (Skip -1) while each repaint still
// reads its page FORWARD from the anchor, then returns. The direction flip
// on every cycle is the hardest case for the read-ahead queue.

STATIC FUNCTION UpWalk()

   LOCAL aSeen := {}
   LOCAL nFail := 0
   LOCAL nSafe := 0
   LOCAL nAnchor, cCode, i

   XP->( DbGoBottom() )

   DO WHILE ! XP->( Bof() ) .AND. nSafe++ < 500

      cCode := AllTrim( XP->CODE )
      AAdd( aSeen, cCode )

      IF XP->( Deleted() )
         LogLine( "FAIL(up): anchor on DELETED row " + cCode )
         nFail++
      ENDIF
      IF ! ( cCode >= SCOPE_TOP .AND. cCode <= SCOPE_BOTTOM )
         LogLine( "FAIL(up): anchor outside scope: " + cCode )
         nFail++
      ENDIF
      IF Len( aSeen ) > 1 .AND. ! ( cCode < aSeen[ Len( aSeen ) - 1 ] )
         LogLine( "FAIL(up): not descending / DUPLICATE at " + cCode + ;
                  " (prev " + aSeen[ Len( aSeen ) - 1 ] + ")" )
         nFail++
      ENDIF

      nAnchor := XP->( RecNo() )
      FOR i := 1 TO 3                        // short page read, forward
         XP->( DbSkip( 1 ) )
         IF XP->( Eof() ) ; EXIT ; ENDIF
         IF XP->( Deleted() )
            LogLine( "FAIL(up): page shows DELETED " + AllTrim( XP->CODE ) )
            nFail++
         ENDIF
      NEXT
      XP->( DbGoto( nAnchor ) )
      XP->( DbSkip( -1 ) )
      IF XP->( Bof() ) ; EXIT ; ENDIF
   ENDDO

   LogLine( "up anchors: " + AllTrim( Str( Len( aSeen ) ) ) + ;
            " first " + iif( Len( aSeen ) > 0, aSeen[ 1 ], "?" ) + ;
            " last " + iif( Len( aSeen ) > 0, ATail( aSeen ), "?" ) )
   IF Len( aSeen ) != 91
      LogLine( "FAIL(up): expected 91 anchors, saw " + AllTrim( Str( Len( aSeen ) ) ) )
      nFail++
   ENDIF
   RETURN nFail

//----------------------------------------------------------------------------//
// Scrollbar thumb drag: AdsSetRelKeyPos(pos) must land on a live, in-scope
// row (xBrowse paints the page from wherever this puts the cursor).

STATIC FUNCTION ThumbDrag()

   LOCAL nFail := 0
   LOCAL nPos, cCode

   FOR nPos := 1 TO 9
      XP->( AdsSetRelKeyPos( nPos / 10 ) )
      cCode := AllTrim( XP->CODE )
      IF XP->( Eof() )
         LogLine( "FAIL(thumb): pos " + AllTrim( Str( nPos / 10, 4, 1 ) ) + ;
                  " landed on EOF" )
         nFail++
         LOOP
      ENDIF
      IF XP->( Deleted() )
         LogLine( "FAIL(thumb): pos " + AllTrim( Str( nPos / 10, 4, 1 ) ) + ;
                  " landed on DELETED row " + cCode + ;
                  " rec " + AllTrim( Str( XP->( RecNo() ) ) ) )
         nFail++
      ENDIF
      IF ! ( cCode >= SCOPE_TOP .AND. cCode <= SCOPE_BOTTOM )
         LogLine( "FAIL(thumb): pos " + AllTrim( Str( nPos / 10, 4, 1 ) ) + ;
                  " landed outside scope: " + cCode + ;
                  " rec " + AllTrim( Str( XP->( RecNo() ) ) ) )
         nFail++
      ENDIF
   NEXT
   RETURN nFail

//----------------------------------------------------------------------------//

STATIC FUNCTION StageLocal( cDir )

   LOCAL aStru := { { "CODE", "C", 5, 0 }, { "NAME", "C", 20, 0 } }
   LOCAL cDbf  := cDir + "\xbpaint.dbf"
   LOCAL i

   AdsSetServerType( 1 )
   AdsSetFileType( 2 )

   IF File( cDbf )
      LogLine( "stage: " + cDbf + " exists — delete to restage" )
      WriteLog()
      RETURN 1
   ENDIF

   DbCreate( cDbf, aStru, "ADSCDX" )
   USE ( cDbf ) VIA "ADSCDX" ALIAS STG EXCLUSIVE NEW
   FOR i := 1 TO N_ROWS
      STG->( DbAppend() )
      STG->CODE := "C" + StrZero( i, 4 )
      STG->NAME := "Customer " + StrZero( i, 4 )
   NEXT
   STG->( DbCommit() )
   INDEX ON STG->CODE TAG BYCODE

   // ten deleted rows INSIDE the scope C0050..C0150, one outside
   FOR i := 60 TO 150 STEP 10
      STG->( DbGoto( i ) )
      STG->( DbDelete() )
   NEXT
   STG->( DbGoto( 20 ) )
   STG->( DbDelete() )
   STG->( DbCommit() )
   STG->( DbCloseArea() )
   LogLine( "stage: created " + cDbf + " (300 rows, TAG BYCODE, deleted " + ;
            "recnos 60..150/10 + 20)" )
   WriteLog()
   RETURN 0

//----------------------------------------------------------------------------//
// Ask the SERVER which build it is (AdsMgGetInstallInfo answers from the
// server's HelloAck since 1.8.14; an old serverd reports "OpenADS 0.3.2").
// This is the line to check whenever a fix "doesn't help": nine times out
// of ten an old openads_serverd is still running.

STATIC FUNCTION LogServerVersion( cUri )

   LOCAL cHost := cUri, aInfo, nAt

   // tcp://host:port/dir -> host:port
   IF Lower( Left( cHost, 6 ) ) == "tcp://" ; cHost := SubStr( cHost, 7 ) ; ENDIF
   nAt := At( "/", cHost )
   IF nAt > 0 ; cHost := Left( cHost, nAt - 1 ) ; ENDIF

   IF AdsMgConnect( cHost ) == 0
      aInfo := AdsMgGetInstallInfo()
      IF ValType( aInfo ) == "A" .AND. Len( aInfo ) >= 3
         LogLine( "server version: " + aInfo[ 3 ] )
      ELSE
         LogLine( "server version: (AdsMgGetInstallInfo failed)" )
      ENDIF
      AdsMgDisconnect()
   ELSE
      LogLine( "server version: (AdsMgConnect failed for " + cHost + ")" )
   ENDIF
   RETURN NIL

//----------------------------------------------------------------------------//

STATIC FUNCTION LogLine( cTxt )
   cLog += cTxt + CRLF
   RETURN NIL

STATIC FUNCTION WriteLog()
   LOCAL cDir := GetEnv( "TEMP" ) + "\openads_fwh_xbpaint"
   IF ! lIsDir( cDir ) ; lMkDir( cDir ) ; ENDIF
   MemoWrit( cDir + "\result.log", cLog )
   RETURN NIL
