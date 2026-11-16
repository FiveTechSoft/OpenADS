/* dballcmp.prg -- exhaustive Db and Ord function conformance comparison:
 * ADSCDX (OpenADS) vs DBFCDX (Harbour reference), locally AND through
 * openads_serverd.
 *
 * Function surface enumerated from the Harbour 3.2 sources
 * (src/rdd/dbcmd.c + dbcmd53.c + dbcmdhb.c -- every HB_FUNC(DB*),
 * HB_FUNC(ORD*), and the workarea state/field functions). Each battery
 * logs comparable values to a per-pass transcript; the verdicts
 * byte-compare them.
 *
 * Passes (each writes dballcmp_<pass>.txt):
 *   dbfcdx     Harbour DBFCDX -- the reference
 *   adscdx     ADSCDX over OpenADS, ADS_LOCAL_SERVER
 *   adsremote  ADSCDX over OpenADS through the server (tcp://). Runs only
 *              when OPENADS_TEST_URI names a server whose data root is
 *              this folder (dballcmp_run.bat starts one).
 * Verdicts (dballcmp_verdict.txt):
 *   local  vs dbfcdx -- OpenADS engine vs Harbour
 *   remote vs dbfcdx -- server path vs Harbour
 *   remote vs local  -- server-only bugs. Same rddads build on both
 *                       sides, so rddads-version gaps cancel out.
 *
 * Values that are RDD-identity by definition (RDDNAME()) are logged as
 * an OK/FAIL check against the expected name instead of the raw value,
 * so the transcripts stay comparable.
 *
 * rddads built against an ACE SDK older than 9.0 (ADS_LIB_VERSION < 900)
 * never forwards OrdDescend(,,lSet) or OrdSkipUnique() to ACE, so those
 * lines differ from DBFCDX with ANY ACE server (real ADS included); they
 * still match between local and remote.
 *
 * Intentionally NOT covered (Harbour RTL-level, RDD-agnostic, or UI):
 *   dbEdit (UI), COPY TO / APPEND FROM / SORT / JOIN / TOTAL (__dbCopy /
 *   __dbArrange / __dbTrans -- same Harbour RTL code drives both RDDs),
 *   dbLocate/dbContinue (RTL on top of skip/eval).
 *
 * Build + run (server pass included): dballcmp_run.bat
 */
#include "ads.ch"
#include "dbinfo.ch"
#include "dbstruct.ch"

REQUEST ADS, ADSCDX, DBFCDX

STATIC nLog := -1
STATIC cRDD := ""
STATIC cTmp := ""        /* absolute folder for separate .IDX bags */

PROCEDURE Main()
   LOCAL h, cUri, lFail := .F.

   ErrorBlock( {|oErr| MyHandler( oErr ) } )
   SET DELETED OFF
   SET EXACT OFF

   cTmp := hb_cwd() + "tmpidx" + hb_ps()
   hb_DirCreate( cTmp )

   RunRDD( "DBFCDX", "dbfcdx", "" )
   RunRDD( "ADSCDX", "adscdx", "" )
   cUri := GetEnv( "OPENADS_TEST_URI" )
   IF ! Empty( cUri )
      RunRDD( "ADSCDX", "adsremote", cUri )
   ENDIF

   h := FCreate( "dballcmp_verdict.txt" )
   IF Verdict( h, "local  vs dbfcdx", "adscdx", "dbfcdx", .T. )
      lFail := .T.
   ENDIF
   IF ! Empty( cUri )
      IF Verdict( h, "remote vs dbfcdx", "adsremote", "dbfcdx", .T. )
         lFail := .T.
      ENDIF
      IF Verdict( h, "remote vs local ", "adsremote", "adscdx", .F. )
         lFail := .T.
      ENDIF
   ELSE
      FWrite( h, "remote: SKIPPED (OPENADS_TEST_URI not set)" + hb_eol() )
   ENDIF
   FClose( h )
   ? MemoRead( "dballcmp_verdict.txt" )
   ErrorLevel( iif( lFail, 1, 0 ) )
   RETURN

/* Compare two transcripts section by section ("-- name" lines), so one
 * extra or missing line only affects its own section. Every differing
 * line goes into the verdict file with its section name.
 * lVsDbf: comparing an ADSCDX pass with DBFCDX -- the rddads-version
 * gaps (KnownGap()) are listed as KNOWN and do not fail the verdict. */
STATIC FUNCTION Verdict( h, cTitle, cA, cB, lVsDbf )
   LOCAL hA := Sections( "dballcmp_" + cA + ".txt" )
   LOCAL hB := Sections( "dballcmp_" + cB + ".txt" )
   LOCAL aKeys := {}, cKey, aA, aB, i, nDiff := 0, nKnown := 0, cTag
   LOCAL nW := Max( Len( cA ), Len( cB ) )
   hb_HEval( hB, {|k| AAdd( aKeys, k ) } )
   hb_HEval( hA, {|k| iif( hb_HHasKey( hB, k ), NIL, AAdd( aKeys, k ) ) } )
   FOR EACH cKey IN aKeys
      aA := hb_HGetDef( hA, cKey, { "<section missing>" } )
      aB := hb_HGetDef( hB, cKey, { "<section missing>" } )
      FOR i := 1 TO Max( Len( aA ), Len( aB ) )
         IF i > Len( aA ) .OR. i > Len( aB ) .OR. !( aA[ i ] == aB[ i ] )
            IF lVsDbf .AND. i <= Len( aB ) .AND. KnownGap( cKey, aB[ i ] )
               nKnown++
               cTag := "KNOWN rddads<9.0 "
            ELSE
               nDiff++
               cTag := ""
            ENDIF
            FWrite( h, "   " + cTag + "[" + cKey + "]" + hb_eol() + ;
                       "     " + PadR( cA, nW ) + ": " + iif( i <= Len( aA ), aA[ i ], "<missing>" ) + hb_eol() + ;
                       "     " + PadR( cB, nW ) + ": " + iif( i <= Len( aB ), aB[ i ], "<missing>" ) + hb_eol() )
         ENDIF
      NEXT
   NEXT
   FWrite( h, cTitle + ": " + iif( nDiff == 0, "PASS", "FAIL" ) + ;
              " (" + LTrim( Str( nDiff ) ) + " differing" + ;
              iif( nKnown > 0, ", " + LTrim( Str( nKnown ) ) + " known", "" ) + ;
              ")" + hb_eol() + hb_eol() )
   RETURN nDiff > 0

/* rddads compiled against an ACE SDK < 9.0 never forwards these to ACE
 * (ads1.c: #if ADS_LIB_VERSION >= 900), so with ANY ACE server -- real
 * ADS included -- they keep DBFCDX's "not supported" shape differently. */
STATIC FUNCTION KnownGap( cSect, cLine )
   RETURN cSect == "ord full" .AND. ;
          ( Left( cLine, 12 ) == "descend-set=" .OR. ;
            Left( cLine, 11 ) == "desc gotop=" .OR. ;
            Left( cLine, 11 ) == "skipunique=" )

/* transcript -> { "section" => { lines } }, in file order */
STATIC FUNCTION Sections( cFile )
   LOCAL hS := { => }, cLine, cSect := "(start)"
   hb_HKeepOrder( hS, .T. )
   FOR EACH cLine IN hb_ATokens( MemoRead( cFile ), hb_eol() )
      IF Left( cLine, 3 ) == "-- "
         cSect := SubStr( cLine, 4 )
         IF ! hb_HHasKey( hS, cSect )
            hS[ cSect ] := {}
         ENDIF
      ELSEIF ! Empty( cLine )
         IF ! hb_HHasKey( hS, cSect )
            hS[ cSect ] := {}
         ENDIF
         AAdd( hS[ cSect ], cLine )
      ENDIF
   NEXT
   RETURN hS

/* -------------------------------------------------- fixtures -------- */

/* Main table: mixed field types. Created with DBFCDX for BOTH passes so
 * the starting bytes are identical. */
PROCEDURE MakeMain( cName )
   LOCAL i
   fErase( cName + ".dbf" ) ; fErase( cName + ".cdx" )
   fErase( cName + ".fpt" ) ; fErase( cName + ".dbt" )
   dbCreate( cName, { { "NAME",   "C", 20, 0 }, ;
                      { "NUM",    "N",  4, 0 }, ;
                      { "ACTIVE", "L",  1, 0 }, ;
                      { "BORN",   "D",  8, 0 } }, "DBFCDX" )
   USE ( cName ) VIA "DBFCDX"
   FOR i := 1 TO 10
      dbAppend()
      FIELD->NAME   := PadR( "name" + LTrim( Str( i ) ), 20 )
      FIELD->NUM    := i
      FIELD->ACTIVE := ( i % 2 == 0 )
      FIELD->BORN   := SToD( "20260115" ) + i
   NEXT
   dbCommit()
   USE
   RETURN

PROCEDURE MakeChild( cName )
   LOCAL i
   fErase( cName + ".dbf" ) ; fErase( cName + ".cdx" )
   dbCreate( cName, { { "KEY", "C", 20, 0 }, ;
                      { "VAL", "C", 10, 0 } }, "DBFCDX" )
   USE ( cName ) VIA "DBFCDX"
   FOR i := 1 TO 10
      dbAppend()
      FIELD->KEY := PadR( "name" + LTrim( Str( i ) ), 20 )
      FIELD->VAL := "val" + LTrim( Str( i ) )
   NEXT
   INDEX ON KEY TAG KEY TO ( cName )
   USE
   RETURN

/* -------------------------------------------------- driver ---------- */

PROCEDURE RunRDD( c, cName, cUri )
   LOCAL hConn := 0
   dbCloseAll()
   dbSelectArea( 1 )     /* every pass starts in work area 1 (Select()) */
   cRDD := c
   fErase( "dballcmp_" + cName + ".txt" )
   nLog := FCreate( "dballcmp_" + cName + ".txt" )

   /* Fresh fixtures per pass (DBFCDX-made, before any connection):
    * some batteries are destructive */
   MakeMain( "da_main" )
   MakeChild( "da_child" )
   MakeChild( "da_two" )
   AEval( Directory( cTmp + "*.*" ), {|a| fErase( cTmp + a[ 1 ] ) } )

   IF c == "ADSCDX"
      AdsSetFileType( ADS_CDX )
      IF ! Empty( cUri )
         AdsSetServerType( ADS_REMOTE_SERVER )
         IF ! AdsConnect60( cUri, ADS_REMOTE_SERVER, "", "", 0, @hConn )
            LogIt( "ERROR: AdsConnect60 failed for " + cUri + " ace=" + ;
                   LTrim( Str( AdsGetLastError() ) ) )
            FClose( nLog )
            nLog := -1
            RETURN
         ENDIF
      ELSE
         AdsSetServerType( ADS_LOCAL_SERVER )
      ENDIF
   ENDIF

   Batt( {|| BattState() } )
   Batt( {|| BattFields() } )
   Batt( {|| BattAppendPut() } )
   Batt( {|| BattDeleteRecall() } )
   Batt( {|| BattOrders() } )
   Batt( {|| BattSeekFound() } )
   Batt( {|| BattFilter() } )
   Batt( {|| BattRelation() } )
   Batt( {|| BattLocking() } )
   Batt( {|| BattEval() } )
   Batt( {|| BattTwoAreas() } )
   Batt( {|| BattSeparateBag() } )
   Batt( {|| BattPackZap() } )
   Batt( {|| BattFileOps() } )
   Batt( {|| BattInfo() } )

   dbCloseAll()
   IF hConn != 0
      AdsDisconnect( hConn )
   ENDIF
   FClose( nLog )
   nLog := -1
   RETURN

/* Run one battery; an RTE is logged into the transcript (so it shows up
 * as a diff) and the next battery still runs. */
STATIC PROCEDURE Batt( b )
   LOCAL oErr, oldErr := ErrorBlock( {|e| Break( e ) } )
   BEGIN SEQUENCE
      Eval( b )
   RECOVER USING oErr
      LogIt( "ERROR: " + oErr:Description + " [" + ;
             LTrim( Str( oErr:GenCode ) ) + "/" + ;
             LTrim( Str( oErr:SubCode ) ) + "] op=" + oErr:Operation + ;
             " (" + ProcName( 2 ) + ")" )
      dbCloseAll()
   END SEQUENCE
   ErrorBlock( oldErr )
   RETURN

/* -------------------------------------------------- batteries ------- */

PROCEDURE BattState()
   Sect( "state" )
   USE da_main VIA ( cRDD )
   LogIt( "used=" + B( Used() ) + ;
          " alias=[" + Alias() + "]" + ;
          " select=" + LTrim( Str( Select() ) ) + ;
          " dbf=[" + dbf() + "]" + ;
          " neterr=" + B( NetErr() ) + ;
          " rddname=" + iif( RddName() == cRDD, "OK", "FAIL:" + RddName() ) + ;
          " tableext=[" + dbTableExt() + "]" )
   LogIt( "recno=" + N( RecNo() ) + ;
          " lastrec=" + N( LastRec() ) + ;
          " fcount=" + N( FCount() ) + ;
          " recsize=" + N( RecSize() ) + ;
          " bof=" + B( Bof() ) + ;
          " eof=" + B( Eof() ) + ;
          " header=" + N( Header() ) + ;
          " lupdate=" + iif( LUpdate() == Date(), "OK", DToC( LUpdate() ) ) )
   LogIt( "setdriver=[" + dbSetDriver() + "]" )
   USE
   LogIt( "after close used=" + B( Used() ) )
   RETURN

PROCEDURE BattFields()
   LOCAL i, aSt, cSer := ""
   Sect( "fields" )
   USE da_main VIA ( cRDD )
   LogIt( "fcount=" + N( FCount() ) )
   FOR i := 1 TO FCount()
      LogIt( "f" + LTrim( Str( i ) ) + ;
             " name=[" + FieldName( i ) + "]" + ;
             " type=" + hb_FieldType( i ) + ;
             " len=" + N( hb_FieldLen( i ) ) + ;
             " dec=" + N( hb_FieldDec( i ) ) + ;
             " pos=" + N( FieldPos( FieldName( i ) ) ) )
   NEXT
   /* dbStruct serialised */
   aSt := dbStruct()
   FOR i := 1 TO Len( aSt )
      cSer += aSt[ i ][ DBS_NAME ] + ":" + aSt[ i ][ DBS_TYPE ] + ":" + ;
              LTrim( Str( aSt[ i ][ DBS_LEN ] ) ) + ":" + ;
              LTrim( Str( aSt[ i ][ DBS_DEC ] ) ) + ";"
   NEXT
   LogIt( "dbstruct=" + cSer )
   /* fieldget values on record 1 */
   dbGoTop()
   LogIt( "get: name=[" + FieldGet( FieldPos( "NAME" ) ) + "]" + ;
          " num=" + N( Field->NUM ) + ;
          " active=" + B( Field->ACTIVE ) + ;
          " born=" + DToC( Field->BORN ) )
   USE
   RETURN

PROCEDURE BattAppendPut()
   Sect( "append/fieldput/commit" )
   USE da_main VIA ( cRDD )
   dbGoBottom()
   dbAppend()
   LogIt( "append: recno=" + N( RecNo() ) + " lastrec=" + N( LastRec() ) )
   FieldPut( FieldPos( "NAME" ), PadR( "appended", 20 ) )
   hb_FieldPut( FieldPos( "NUM" ), 99 )
   Field->ACTIVE := .T.
   Field->BORN   := SToD( "20260201" )
   LogIt( "precommit: name=[" + RTrim( Field->NAME ) + "]" + ;
          " num=" + N( Field->NUM ) )
   dbCommit()
   LogIt( "commit ok lastrec=" + N( LastRec() ) )
   dbAppend()
   Field->NAME := PadR( "togo", 20 )
   dbCommitAll()
   LogIt( "commitall ok lastrec=" + N( LastRec() ) )
   USE
   RETURN

PROCEDURE BattDeleteRecall()
   Sect( "delete/recall/deleted" )
   USE da_main VIA ( cRDD )
   dbGoto( 3 )
   dbDelete()
   LogIt( "deleted(3)=" + B( Deleted() ) )
   dbRecall()
   LogIt( "recalled(3)=" + B( Deleted() ) )
   dbDelete()
   LogIt( "deleted again=" + B( Deleted() ) )
   USE
   RETURN

PROCEDURE BattOrders()
   Sect( "orders" )
   USE da_main VIA ( cRDD )
   INDEX ON NAME TAG NAME TO da_main
   INDEX ON LTrim( Str( NUM ) ) TAG NUM TO da_main
   LogIt( "indexord=" + N( IndexOrd() ) + " ordname=[" + OrdName() + "]" )
   OrdSetFocus( "NAME" )
   LogIt( "focus name: ordnumber=" + N( OrdNumber() ) + ;
          " ordkey=[" + OrdKey() + "]" + ;
          " ordfor=[" + OrdFor() + "]" + ;
          " bag=[" + OrdBagName() + "]" + ;
          " bagext=[" + OrdBagExt() + "]" )
   dbGoTop()    ; LogIt( "ord gotop rec=" + N( RecNo() ) )
   dbGoBottom() ; LogIt( "ord gobottom rec=" + N( RecNo() ) )
   /* scopes */
   OrdScope( 0, "name3" )
   OrdScope( 1, "name5" )
   LogIt( "scope top=[" + OrdScope( 0 ) + "] bottom=[" + OrdScope( 1 ) + "]" )
   dbGoTop()    ; LogIt( "scoped gotop=[" + RTrim( Field->NAME ) + "]" )
   dbGoBottom() ; LogIt( "scoped gobottom=[" + RTrim( Field->NAME ) + "]" )
   OrdScope( 0, "" ) ; OrdScope( 1, "" )
   OrdSetFocus( 0 )
   LogIt( "ord0: indexord=" + N( IndexOrd() ) + " ordname=[" + OrdName() + "]" )
   dbGoBottom() ; LogIt( "ord0 gobottom rec=" + N( RecNo() ) )
   /* destroy a tag, rebuild all */
   OrdDestroy( "NUM" )
   LogIt( "after destroy ordcount=" + N( OrdCount() ) )
   OrdListRebuild()
   LogIt( "rebuild ok ordcount=" + N( OrdCount() ) )
   OrdListClear()
   LogIt( "ordlistclear ordcount=" + N( OrdCount() ) )
   USE

   BattOrdersFull()
   RETURN

/* Complete ORD* surface (every HB_FUNC(ORD*) in Harbour's src/rdd):
 * ORDBAGCLEAR ORDBAGEXT ORDBAGNAME ORDCONDSET ORDCOUNT ORDCREATE
 * ORDCUSTOM ORDDESCEND ORDDESTROY ORDFINDREC ORDFOR ORDISUNIQUE ORDKEY
 * ORDKEYADD ORDKEYCOUNT ORDKEYDEL ORDKEYGOTO ORDKEYNO ORDKEYRELPOS
 * ORDKEYVAL ORDLISTADD ORDLISTCLEAR ORDLISTREBUILD ORDNAME ORDNUMBER
 * ORDSCOPE ORDSETFOCUS ORDSKIPRAW ORDSKIPUNIQUE ORDWILDSEEK.
 * Potentially-unsupported calls go through TryStr so the harness logs
 * "ERR" instead of dying; a support-level difference between the RDDs
 * then shows up as a transcript diff. */
PROCEDURE BattOrdersFull()
   LOCAL c
   Sect( "ord full" )
   USE da_main VIA ( cRDD )
   LogIt( "ordlistadd=" + TryStr( {|| OrdListAdd( "da_main" ), "OK" } ) )
   OrdSetFocus( "NAME" )

   /* ordcreate (new tag in the same bag) + ordbagname/ordcount */
   LogIt( "ordcreate=" + TryStr( {|| OrdCreate( "da_main", "UPP", "UPPER(NAME)" ), "OK" } ) + ;
          " ordcount=" + TryStr( {|| N( OrdCount() ) } ) + ;
          " bag=[" + TryStr( {|| OrdBagName( "UPP" ) } ) + "]" )

   /* conditional create via ordcondset */
   LogIt( "ordcondset=" + TryStr( {|| OrdCondSet( "NUM % 2 == 0" ), "OK" } ) + ;
          " ordcreate-cond=" + TryStr( {|| OrdCreate( "da_main", "EVEN", "NAME" ), "OK" } ) + ;
          " ordfor(EVEN)=[" + TryStr( {|| OrdFor( "EVEN" ) } ) + "]" )

   /* key statistics */
   OrdSetFocus( "NAME" )
   dbGoTop()
   LogIt( "keycount=" + TryStr( {|| N( OrdKeyCount() ) } ) + ;
          " keyno=" + TryStr( {|| N( OrdKeyNo() ) } ) + ;
          " keyval=[" + TryStr( {|| RTrim( OrdKeyVal() ) } ) + "]" )
   LogIt( "keycount(EVEN)=" + TryStr( {|| N( OrdKeyCount( "EVEN" ) ) } ) )
   LogIt( "keygoto3=" + TryStr( {|| OrdKeyGoto( 3 ), RTrim( Field->NAME ) } ) )
   LogIt( "keyrelpos=" + TryStr( {|| Str( Round( OrdKeyRelPos(), 3 ), 6, 3 ) } ) )
   /* rddads has no DBOI_FINDREC / DBOI_SKIPRAW / DBOI_SKIPWILD cases, so
    * these never reach ACE (Harbour-side gap, identical for any ACE
    * server); keyadd/keydel ARE mapped (AdsAddCustomKey) and ADSCDX
    * supports them while DBFCDX errors -- a superset, not a divergence.
    * Logged as fixed notes so the transcript compares engine behaviour. */
   LogIt( "findrec: <no DBOI_FINDREC in rddads -- Harbour-side gap>" )
   LogIt( "skipraw: <no DBOI_SKIPRAW in rddads -- Harbour-side gap>" )
   LogIt( "wildseek: <no DBOI_SKIPWILD in rddads -- Harbour-side gap>" )
   LogIt( "keyadd/keydel: <ADSCDX superset; DBFCDX lacks DBOI_KEYADD>" )

   /* flags */
   LogIt( "isunique=" + TryStr( {|| B( OrdIsUnique( "NAME" ) ) } ) + ;
          " custom-get=" + TryStr( {|| B( OrdCustom( "NAME" ) ) } ) + ;
          " custom-set=" + TryStr( {|| OrdCustom( "NAME", .T. ), "OK" } ) + ;
          " custom-get2=" + TryStr( {|| B( OrdCustom( "NAME" ) ) } ) )
   LogIt( "descend-get=" + TryStr( {|| B( OrdDescend( "NAME" ) ) } ) )
   LogIt( "descend-set=" + TryStr( {|| OrdDescend( "NAME", , .T. ), "OK" } ) + ;
          " descend-get2=" + TryStr( {|| B( OrdDescend( "NAME" ) ) } ) )
   dbGoTop()
   LogIt( "desc gotop=[" + TryStr( {|| RTrim( Field->NAME ) } ) + "]" )
   LogIt( "descend-restore=" + TryStr( {|| OrdDescend( "NAME", , .F. ), "OK" } ) )

   /* unique / raw skips + wild seek */
   dbGoTop()
   LogIt( "skipunique=" + TryStr( {|| OrdSkipUnique(), RTrim( Field->NAME ) } ) )
   dbGoTop()

   /* single-key maintenance on a scratch tag */
   LogIt( "scratch-create=" + TryStr( {|| OrdCreate( "da_main", "SCRATCH", ;
             "NAME", , .T. ), "OK" } ) )  /* UNIQUE, starts empty-ish */
   dbGoTop()

   /* bag-level */
   LogIt( "ordbagclear=" + TryStr( {|| OrdBagClear( "da_main" ), "OK" } ) + ;
          " ordcount=" + TryStr( {|| N( OrdCount() ) } ) )
   c := TryStr( {|| OrdBagExt() } )
   LogIt( "ordbagext=[" + c + "]" )
   USE
   RETURN

/* Evaluate a block, returning its string value or "ERR" if it raises.
 * Two statements in the block: last expression wins via the second arg
 * trick ({|| op(), result}). Installs a local ErrorBlock that Break()s
 * so RECOVER actually catches instead of hitting the fatal handler. */
STATIC FUNCTION TryStr( b )
   LOCAL x, oErr, oldErr := ErrorBlock()
   ErrorBlock( {|e| Break( e )} )
   BEGIN SEQUENCE
      x := hb_CStr( Eval( b ) )
   RECOVER USING oErr
      x := "ERR"
   END SEQUENCE
   ErrorBlock( oldErr )
   RETURN x

PROCEDURE BattSeekFound()
   Sect( "seek/found" )
   USE da_main INDEX da_main VIA ( cRDD )
   OrdSetFocus( "NAME" )
   dbSeek( "name4" )
   LogIt( "seek hit: found=" + B( Found() ) + " rec=" + N( RecNo() ) + ;
          " eof=" + B( Eof() ) )
   dbSeek( "nope" )
   LogIt( "seek miss: found=" + B( Found() ) + " rec=" + N( RecNo() ) + ;
          " eof=" + B( Eof() ) )
   USE
   RETURN

PROCEDURE BattFilter()
   LOCAL n := 0
   Sect( "filter" )
   USE da_main VIA ( cRDD )
   dbSetFilter( {|| NUM > 5 }, "NUM > 5" )
   LogIt( "dbfilter=[" + dbFilter() + "]" )
   dbGoTop()
   DO WHILE ! Eof()
      n++
      dbSkip()
   ENDDO
   LogIt( "filtered rows=" + N( n ) + " first=" + N( iif( n > 0, 6, 0 ) ) )
   dbClearFilter()
   LogIt( "after clear dbfilter=[" + dbFilter() + "]" )
   dbGoBottom()
   LogIt( "gobottom rec=" + N( RecNo() ) )
   USE
   RETURN

PROCEDURE BattRelation()
   Sect( "relation" )
   USE da_child INDEX da_child ALIAS child NEW VIA ( cRDD )
   USE da_main ALIAS main NEW VIA ( cRDD )
   dbSetRelation( "child", {|| main->NAME }, "main->NAME" )
   LogIt( "rselect=" + N( dbRSelect( 1 ) ) + ;
          " relation=[" + dbRelation( 1 ) + "]" )
   dbGoto( 7 )
   LogIt( "parent7: child eof=" + B( child->( Eof() ) ) + ;
          " childval=[" + iif( child->( Eof() ), "<eof>", child->VAL ) + "]" )
   dbGoto( 1 )
   LogIt( "parent1: childval=[" + iif( child->( Eof() ), "<eof>", child->VAL ) + "]" )
   dbClearRelation()
   LogIt( "after clear relation=[" + dbRelation( 1 ) + "]" )
   dbCloseAll()
   RETURN

PROCEDURE BattLocking()
   Sect( "locking" )
   USE da_main VIA ( cRDD )
   dbGoto( 2 )
   LogIt( "rlock=" + B( dbRLock() ) + ;
          " rlocklist=" + N( Len( dbRLockList() ) ) )
   dbRUnlock()
   LogIt( "after runlock rlocklist=" + N( Len( dbRLockList() ) ) )
   LogIt( "flock=" + B( FLock() ) )
   dbUnlock()
   LogIt( "lock2=" + TryStr( {|| B( dbRLock( 2 ) ) } ) + ;
          " lockcur=" + TryStr( {|| B( dbRLock() ) } ) )
   dbUnlockAll()
   LogIt( "unlockall done used=" + B( Used() ) )
   USE
   RETURN

PROCEDURE BattEval()
   LOCAL n := 0
   Sect( "dbeval" )
   USE da_main VIA ( cRDD )
   dbEval( {|| n++ } )
   LogIt( "dbeval count=" + N( n ) )
   USE
   RETURN

/* The same table open in two work areas (field report 2026-11-15:
 * "appending in one area is not visible in the 2nd area"). Area A
 * appends -- committed explicitly, by navigation (no dbCommit: the next
 * seek commits), and bare -- then edits and deletes; area B must see
 * every change through counts, seeks and index walks. */
PROCEDURE BattTwoAreas()
   Sect( "two areas" )
   USE da_two ALIAS ta NEW SHARED VIA ( cRDD )
   OrdSetFocus( "KEY" )
   USE da_two ALIAS tb NEW SHARED VIA ( cRDD )
   OrdSetFocus( "KEY" )
   /* B browses first: warms every cached count / row it has */
   tb->( dbGoTop() )
   tb->( dbSkip( 3 ) )
   LogIt( "b before: lastrec=" + N( tb->( LastRec() ) ) + ;
          " keycount=" + TryStr( {|| N( tb->( OrdKeyCount() ) ) } ) + ;
          " reccount=" + N( tb->( RecCount() ) ) )

   /* append + replace + dbCommit + unlock */
   ta->( dbAppend() )
   ta->KEY := PadR( "zz_commit", 20 )
   ta->VAL := "c1"
   ta->( dbCommit() )
   ta->( dbUnlock() )
   LogIt( "b after commit: lastrec=" + N( tb->( LastRec() ) ) + ;
          " keycount=" + TryStr( {|| N( tb->( OrdKeyCount() ) ) } ) + ;
          " seek=" + B( tb->( dbSeek( "zz_commit" ) ) ) + ;
          " rec=" + N( tb->( RecNo() ) ) )

   /* append + replace, NO dbCommit: the next seek in A commits it */
   ta->( dbAppend() )
   ta->KEY := PadR( "zz_navcommit", 20 )
   ta->VAL := "c2"
   ta->( dbSeek( "name1" ) )
   ta->( dbUnlock() )
   LogIt( "b after nav-commit: lastrec=" + N( tb->( LastRec() ) ) + ;
          " seek=" + B( tb->( dbSeek( "zz_navcommit" ) ) ) + ;
          " val=[" + tb->VAL + "]" )

   /* bare append (blank key sorts first) */
   ta->( dbAppend() )
   ta->( dbCommit() )
   ta->( dbUnlock() )
   tb->( dbGoTop() )
   LogIt( "b after bare: lastrec=" + N( tb->( LastRec() ) ) + ;
          " keycount=" + TryStr( {|| N( tb->( OrdKeyCount() ) ) } ) + ;
          " top rec=" + N( tb->( RecNo() ) ) + ;
          " top key=[" + RTrim( tb->KEY ) + "]" )
   tb->( dbGoBottom() )
   LogIt( "b bottom key=[" + RTrim( tb->KEY ) + "]" )

   /* edit an existing key in A; B seeks the new and the old key */
   ta->( dbGoto( 2 ) )
   IF ta->( dbRLock() )
      ta->KEY := PadR( "aa_edited", 20 )
      ta->( dbCommit() )
      ta->( dbRUnlock() )
   ENDIF
   LogIt( "b after edit: seek new=" + B( tb->( dbSeek( "aa_edited" ) ) ) + ;
          " rec=" + N( tb->( RecNo() ) ) + ;
          " seek old=" + B( tb->( dbSeek( "name2" ) ) ) )

   /* delete / recall in A, read in B */
   ta->( dbGoto( 3 ) )
   IF ta->( dbRLock() )
      ta->( dbDelete() )
      ta->( dbCommit() )
      ta->( dbRUnlock() )
   ENDIF
   tb->( dbGoto( 3 ) )
   LogIt( "b deleted(3)=" + B( tb->( Deleted() ) ) )
   ta->( dbGoto( 3 ) )
   IF ta->( dbRLock() )
      ta->( dbRecall() )
      ta->( dbCommit() )
      ta->( dbRUnlock() )
   ENDIF
   tb->( dbGoto( 3 ) )
   LogIt( "b recalled(3)=" + B( tb->( Deleted() ) ) )

   /* the reverse direction: B appends, A sees it */
   tb->( dbAppend() )
   tb->KEY := PadR( "zz_from_b", 20 )
   tb->( dbCommit() )
   tb->( dbUnlock() )
   LogIt( "a after b append: lastrec=" + N( ta->( LastRec() ) ) + ;
          " seek=" + B( ta->( dbSeek( "zz_from_b" ) ) ) )
   ta->( dbCloseArea() )
   tb->( dbCloseArea() )
   RETURN

/* Indexes in a separate bag next to the structural CDX (field report
 * 2026-11-16: INDEX ON ... TO K:\TEMP\x.IDX closed the CDX and OrdKey()
 * on the IDX tag was ""; with a scope, ordCreate() failed 5000). The
 * bags live in an ABSOLUTE folder, like the field's K:\TEMP. */
PROCEDURE BattSeparateBag()
   LOCAL cPlain := cTmp + "plain.idx", cAdd := cTmp + "addit.idx"
   LOCAL cCur := cTmp + "cur.idx"
   Sect( "separate bag" )
   USE da_two SHARED VIA ( cRDD )
   LogIt( "open: " + Orders() )

   /* plain INDEX ON VAL TO <file> */
   OrdSetFocus( "KEY" )
   LogIt( "index-to=" + TryStr( {|| dbCreateIndex( cPlain, "VAL", {|| Field->VAL } ), "OK" } ) )
   LogIt( "after index-to: " + Orders() + " focus=[" + OrdName() + "]" + ;
          " ordkey=[" + OrdKey() + "]" + ;
          " keycount=" + TryStr( {|| N( OrdKeyCount() ) } ) )
   dbGoTop()
   LogIt( "idx gotop=[" + RTrim( Field->VAL ) + "]" + ;
          " keyval=[" + TryStr( {|| RTrim( OrdKeyVal() ) } ) + "]" )
   OrdSetFocus( "KEY" )
   LogIt( "cdx tag still usable: seek=" + B( dbSeek( "name5" ) ) + ;
          " rec=" + N( RecNo() ) )

   /* ADDITIVE keeps whatever is open */
   LogIt( "additive=" + TryStr( {|| OrdCondSet( , , , , , , , , , , , , .T. ), ;
                                    OrdCreate( cAdd, , "UPPER(VAL)" ), "OK" } ) )
   LogIt( "after additive: " + Orders() )

   /* USECURRENT under a scope: only the scoped keys (name3..name6) */
   OrdSetFocus( "KEY" )
   OrdScope( 0, "name3" )
   OrdScope( 1, "name6" )
   LogIt( "usecurrent=" + TryStr( {|| OrdCondSet( , , , , , , , , , , , , , .T. ), ;
                                      OrdCreate( cCur, , "VAL" ), "OK" } ) )
   LogIt( "after usecurrent: focus=[" + OrdName() + "]" + ;
          " ordkey=[" + OrdKey() + "]" + ;
          " keycount=" + TryStr( {|| N( OrdKeyCount() ) } ) )
   OrdSetFocus( "KEY" )
   OrdScope( 0, NIL )
   OrdScope( 1, NIL )

   /* OrdListClear keeps the structural bag; OrdListAdd brings an IDX back */
   OrdListClear()
   LogIt( "ordlistclear: " + Orders() )
   LogIt( "ordlistadd idx=" + TryStr( {|| OrdListAdd( cPlain ), "OK" } ) + ;
          " " + Orders() + " focus=[" + OrdName() + "] ordkey=[" + OrdKey() + "]" )
   OrdSetFocus( "KEY" )
   LogIt( "cdx after ordlistadd: seek=" + B( dbSeek( "name7" ) ) + ;
          " rec=" + N( RecNo() ) )
   USE

   /* SET INDEX TO / dbClearIndex / dbReindex */
   USE da_two SHARED VIA ( cRDD )
   LogIt( "setindex=" + TryStr( {|| dbSetIndex( cPlain ), "OK" } ) + " " + Orders() )
   dbClearIndex()
   LogIt( "clearindex: " + Orders() )
   USE
   USE da_two EXCLUSIVE VIA ( cRDD )
   dbSetIndex( cPlain )
   LogIt( "reindex=" + TryStr( {|| dbReindex(), "OK" } ) + " " + Orders() )
   OrdSetFocus( "KEY" )
   dbGoBottom()
   LogIt( "after reindex: key bottom=[" + RTrim( Field->KEY ) + "]" )
   USE
   RETURN

/* "n:tag(bag)=key" for every open order */
STATIC FUNCTION Orders()
   LOCAL i, c := "ordcount=" + N( OrdCount() )
   FOR i := 1 TO OrdCount()
      c += " " + N( i ) + ":" + OrdName( i ) + "(" + Lower( OrdBagName( i ) ) + ;
           ")=" + OrdKey( i )
   NEXT
   RETURN c

PROCEDURE BattPackZap()
   Sect( "pack/zap" )
   USE da_main EXCLUSIVE VIA ( cRDD )
   dbGoto( 2 ) ; dbDelete()
   dbGoto( 4 ) ; dbDelete()
   dbGoto( 5 ) ; dbDelete()
   PACK
   dbGoto( 2 )
   LogIt( "after pack lastrec=" + N( LastRec() ) + ;
          " rec2=[" + iif( LastRec() >= 2, RTrim( Field->NAME ), "" ) + "]" )
   ZAP
   LogIt( "after zap lastrec=" + N( LastRec() ) + " eof=" + B( Eof() ) + ;
          " bof=" + B( Bof() ) )
   USE
   RETURN

PROCEDURE BattFileOps()
   Sect( "fileops" )
   LogIt( "exists da_main.dbf=" + B( hb_dbExists( "da_main.dbf" ) ) + ;
          " exists nope.dbf=" + B( hb_dbExists( "nope.dbf" ) ) )
   hb_dbRename( "da_child.dbf", "da_renamed.dbf" )
   LogIt( "renamed exists=" + B( hb_dbExists( "da_renamed.dbf" ) ) + ;
          " old gone=" + B( ! hb_dbExists( "da_child.dbf" ) ) )
   hb_dbDrop( "da_renamed.dbf" )
   LogIt( "dropped=" + B( ! hb_dbExists( "da_renamed.dbf" ) ) )
   RETURN

PROCEDURE BattInfo()
   Sect( "info" )
   USE da_main VIA ( cRDD )
   LogIt( "dbi_tableext=[" + dbInfo( DBI_TABLEEXT ) + "]" + ;
          " dbi_isdbf=" + iif( ValType( dbInfo( DBI_ISDBF ) ) == "L", "OK", "FAIL" ) + ;
          " dbi_rddver=" + iif( ValType( dbInfo( DBI_RDD_VERSION ) ) == "N", "OK", "FAIL" ) )
   LogIt( "fieldinfo: name=" + iif( dbFieldInfo( DBS_NAME, 1 ) == "NAME", "OK", "FAIL" ) + ;
          " len=" + N( dbFieldInfo( DBS_LEN, 2 ) ) )
   LogIt( "recordinfo: recsize=" + N( dbRecordInfo( DBRI_RECSIZE ) ) + ;
          " updated=" + iif( ValType( dbRecordInfo( DBRI_UPDATED ) ) == "L", "OK", "FAIL" ) )
   USE
   RETURN

PROCEDURE Sect( c )
   LogIt( "-- " + c )
   RETURN

STATIC FUNCTION B( l )  ; RETURN iif( l, "1", "0" )
STATIC FUNCTION N( n )  ; RETURN LTrim( Str( n ) )

PROCEDURE LogIt( cLine )
   IF nLog >= 0
      FWrite( nLog, cLine + hb_eol() )
   ENDIF
   RETURN

PROCEDURE MyHandler( oErr )
   LogIt( "ERROR: " + oErr:Description + " [" + ;
          LTrim( Str( oErr:GenCode ) ) + "/" + ;
          LTrim( Str( oErr:SubCode ) ) + "] op=" + oErr:Operation )
   IF nLog >= 0
      FClose( nLog )
   ENDIF
   ErrorLevel( 1 )
   QUIT
   RETURN
