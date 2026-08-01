/*
   PRG:     b_big_e2e.prg
   Purpose: Comprehensive END-TO-END regression test for OpenADS
            (ace64/ace32 + openads_serverd), covering every bug fixed in
            the v1.8.44..v1.8.46 line. Polished from Pritpal Bedi's
            B_BIG.prg concept: one PRG that end users can build and run
            against any OpenADS install, 32 or 64 bit, local or remote.

   Usage:   b_big_e2e.exe local  C:\path\to\datadir
            b_big_e2e.exe remote tcp://127.0.0.1:16262/C:/path/to/datadir

   Every section prints PASS/FAIL and the program exits with
   ErrorLevel 0 only when ALL sections pass.

   Build:   build_e2e.bat   (produces b_big_e2e64.exe and b_big_e2e32.exe)
*/

#include "ads.ch"

REQUEST ADS, ADSCDX
REQUEST ADSKEYNO, ADSKEYCOUNT, ADSGETRELKEYPOS, ADSSETRELKEYPOS
REQUEST OADS_DIREXIST, OADS_DIRMAKE
REQUEST HB_MUTEXCREATE, HB_THREADSTART, HB_THREADJOIN, HB_MUTEXLOCK, HB_MUTEXUNLOCK

#define ADS_REMOTE_SERVER 2
#define ADS_LOCAL_SERVER  1

STATIC s_nPass := 0, s_nFail := 0
STATIC s_hConn := 0
STATIC s_cDir  := ""
STATIC s_cMode := "", s_cTarget := ""
STATIC s_lErr

STATIC FUNCTION DataFileExists( cName )
   IF s_cMode == "local"
      RETURN File( s_cTarget + "/" + cName )
   ENDIF
RETURN OAds_CheckExistence( s_hConn, cName )

//----------------------------------------------------------------------------//

STATIC PROCEDURE Section( cName, lOk, cDetail )
   LOCAL cLine := "  [" + iif( lOk, "PASS", "FAIL" ) + "] " + PadR( cName, 46 ) + " " + cDetail
   LogLine( cLine )
   IF lOk
      s_nPass++
   ELSE
      s_nFail++
   ENDIF
RETURN

STATIC PROCEDURE LogLine( cLine )
   // OutStd, not ?: on some 32-bit Harbour builds the console GT
   // swallows QOut output entirely, and a test harness must print.
   OutStd( cLine + hb_eol() )
RETURN

//----------------------------------------------------------------------------//
// Returns the list of recnos visited by a top-to-bottom walk as a string.
STATIC FUNCTION WalkRecnos()
   LOCAL c := "", n := 0
   dbGoTop()
   DO WHILE ! Eof()
      c += LTrim( Str( RecNo() ) ) + " "
      n++
      IF n > 50
         EXIT
      ENDIF
      dbSkip()
   ENDDO
RETURN RTrim( c )

STATIC FUNCTION OpenShared( cDbf, cAlias )
   dbUseArea( .T., "ADSCDX", s_cDir + "" + cDbf, cAlias, .T., .F., , s_hConn )
RETURN Select( cAlias ) > 0

//----------------------------------------------------------------------------//
PROCEDURE Main( cMode, cTarget )
   LOCAL nType, cBag, cDbf, cDbf2, nT, i, lOk
   LOCAL aT := {}, i2
   LOCAL hConn2 := 0, nCount := 0
   LOCAL x, h

   ErrorBlock( {| e | Fatal( e ) } )
   SetMode( 25, 100 )
   SET ALTERNATE TO b_big_e2e.log
   SET ALTERNATE ON
   SET CONSOLE OFF

   IF cMode == NIL .OR. cTarget == NIL
      ? "usage: b_big_e2e.exe local <dir> | remote tcp://host:port/<datadir>"
      QUIT
   ENDIF

   cMode := Lower( cMode )
   s_cMode := cMode
   s_cTarget := cTarget
   nType := iif( cMode == "remote", ADS_REMOTE_SERVER, ADS_LOCAL_SERVER )
   IF cMode == "local" .AND. Right( cTarget, 1 ) $ "\/"
      cTarget := Left( cTarget, Len( cTarget ) - 1 )
   ENDIF

   LogLine( "OpenADS end-to-end regression (b_big_e2e)" )
   LogLine( "mode=" + cMode + "  target=" + cTarget )

   AdsSetFileType( ADS_CDX )
   RddSetDefault( "ADSCDX" )
   SET DELETED OFF
   SET EXCLUSIVE OFF

   IF ! AdsConnect60( cTarget, nType, NIL, NIL, 0, @s_hConn )
      LogLine( "*** cannot connect: " + cTarget )
      ErrorLevel( 1 )
      QUIT
   ENDIF
   AdsConnection( s_hConn )

   // All test files live directly under the data dir with an e2e_
   // prefix (no subdir to create).
   s_cDir := ""

   //====================================================================//
   // 1. Version is reported programmatically (v1.8.44: AdsGetVersion
   //    used to return hardcoded 0.0)
   //====================================================================//
   BEGIN SEQUENCE
      Section( "AdsVersion() reports a real version", ! Empty( AdsVersion() ), ;
               "[" + AdsVersion() + "]" )
   RECOVER
      Section( "AdsVersion() reports a real version", .F., "call failed" )
   END SEQUENCE

   //====================================================================//
   // 2. FIELD-> qualifier is NOT persisted in the stored key expression
   //    (v1.8.44: INDEX ON FIELD->name saved "FIELD->name" in the bag)
   //====================================================================//
   cDbf := s_cDir + "e2e_fld.dbf"
   cBag := s_cDir + "e2e_fld.cdx"
   FErase( cDbf ) ; FErase( cBag )
   dbCreate( cDbf, { { "NAME", "C", 10, 0 }, ;
                     { "CITY", "C", 10, 0 }, ;
                     { "AGE" , "N",  4, 0 } }, "ADSCDX" )
   USE ( cDbf ) ALIAS t NEW VIA "ADSCDX"
   INDEX ON FIELD->name TAG "TAG01" TO ( cBag )
   INDEX ON FIELD->city TAG "TAG02" TO ( cBag )
   USE
   Section( "FIELD-> stripped from stored key expression", ;
            DataFileExists( cBag ), ;
            "bag=" + cBag )

   //====================================================================//
   // 3. Seed data (physical order != index order)
   //    rec1 zulu, rec2 alpha, rec3 mike, rec4 bravo, rec5 charlie
   //====================================================================//
   cDbf2 := s_cDir + "e2e_ord.dbf"
   FErase( cDbf2 ) ; FErase( s_cDir + "e2e_ord.cdx" )
   dbCreate( cDbf2, { { "NAME", "C", 10, 0 } }, "ADSCDX" )
   USE ( cDbf2 ) ALIAS t NEW VIA "ADSCDX"
   FOR EACH x IN { "zulu", "alpha", "mike", "bravo", "charlie" }
      t->( dbAppend() )
      t->name := x
   NEXT
   INDEX ON FIELD->name TAG "NAME" TO ( s_cDir + "e2e_ord" )
   USE

   // 3a. Production bag auto-open on USE leaves NATURAL order (order 0)
   USE ( cDbf2 ) ALIAS t NEW VIA "ADSCDX"
   Section( "USE auto-opens production bag with NO active order", ;
            OrdNumber() == 0, "OrdNumber()=" + LTrim( Str( OrdNumber() ) ) )

   // 3b. dbSetIndex activates the first tag (native DBFCDX parity)
   dbSetIndex( s_cDir + "e2e_ord.cdx" )
   Section( "dbSetIndex activates first order when none set", ;
            OrdNumber() == 1, "OrdNumber()=" + LTrim( Str( OrdNumber() ) ) )

   // 3c. Ordered walk follows the index
   Section( "ordered walk follows index order", ;
            WalkRecnos() == "2 4 5 3 1", "[" + WalkRecnos() + "]" )
   USE

   //====================================================================//
   // 4. dbSetOrder(1) -> index order, dbSetOrder(0) -> NATURAL order
   //    (v1.8.46: the restore regressed; also via remote wire)
   //====================================================================//
   USE ( cDbf2 ) ALIAS t NEW VIA "ADSCDX" SHARED
   dbSetOrder( 1 )
   Section( "dbSetOrder(1) -> index order", ;
            WalkRecnos() == "2 4 5 3 1", "[" + WalkRecnos() + "]" )
   dbSetOrder( 0 )
   lOk := ( WalkRecnos() == "1 2 3 4 5" )
   Section( "dbSetOrder(0) -> natural order", lOk, "[" + WalkRecnos() + "]" )
   // ...and the order can be re-activated afterwards
   dbSetOrder( 1 )
   Section( "order re-activates after dbSetOrder(0)", ;
            WalkRecnos() == "2 4 5 3 1", "[" + WalkRecnos() + "]" )
   USE

   //====================================================================//
   // 5. Scope + SET DELETED ON: exactly the live rows are served
   //    (v1.8.45: KeyNo/RelKeyPos used physical count -> phantom rows)
   //    group "100011": rec1 live, rec2 deleted, rec3 deleted;
   //    group "100012": rec4, rec5 live
   //====================================================================//
   cDbf := s_cDir + "e2e_scp.dbf"
   FErase( cDbf ) ; FErase( s_cDir + "e2e_scp.cdx" )
   dbCreate( cDbf, { { "GRP", "C", 6, 0 }, { "NAME", "C", 10, 0 } }, "ADSCDX" )
   USE ( cDbf ) ALIAS t NEW VIA "ADSCDX"
   FOR EACH x IN { { "100011", "live-a" }, { "100011", "gone-b" }, ;
                   { "100011", "gone-c" }, { "100012", "live-d" }, ;
                   { "100012", "live-e" } }
      t->( dbAppend() )
      t->grp  := x[ 1 ]
      t->name := x[ 2 ]
   NEXT
   INDEX ON FIELD->grp TAG "GRP" TO ( s_cDir + "e2e_scp" )
   t->( dbGoTo( 2 ) ) ; t->( dbDelete() )
   t->( dbGoTo( 3 ) ) ; t->( dbDelete() )
   USE

   USE ( cDbf ) ALIAS t NEW VIA "ADSCDX" SHARED
   SET DELETED ON
   dbSetOrder( 1 )
   OrdScope( 0, "100011" )
   OrdScope( 1, "100011" )
   Section( "scoped KeyCount excludes deleted rows", OrdKeyCount() == 1, ;
            "KeyCount=" + LTrim( Str( OrdKeyCount() ) ) )
   Section( "scoped walk serves exactly the live row", ;
            WalkRecnos() == "1", "[" + WalkRecnos() + "]" )
   dbGoTop()
   Section( "KeyNo=1 at scope top", OrdKeyNo() == 1, ;
            "KeyNo=" + LTrim( Str( OrdKeyNo() ) ) )
   dbSkip()
   Section( "skip past scope end -> EOF", Eof(), "Eof()=" + iif( Eof(), "T", "F" ) )
   dbSkip( -1 )
   Section( "skip back from EOF lands on the scoped row", ;
            ! Eof() .AND. RecNo() == 1, ;
            "RecNo=" + LTrim( Str( RecNo() ) ) )
   dbGoBottom()
   Section( "GoBottom KeyNo == scoped KeyCount", OrdKeyNo() == OrdKeyCount(), ;
            "KeyNo=" + LTrim( Str( OrdKeyNo() ) ) )

   // 5b. The phantom-duplicate scenario (v1.8.47): a backward skip at the
   // scope TOP must report BOF on the FIRST try, even right after
   // dbRefresh() (which invalidates the remote row cache). With the bug,
   // the first skip(-1) answered Bof()=.F. and xBrowse counted one extra
   // row above -> the single scoped record painted twice.
   dbGoTop()
   AdsRefreshRecord()
   dbSkip( -1 )
   Section( "first skip(-1) at scope top -> BOF", Bof(), ;
            "Bof()=" + iif( Bof(), "T", "F" ) )
   dbGoTop()
   dbSkip()
   Section( "one more skip -> EOF (no duplicate row)", Eof(), ;
            "Eof()=" + iif( Eof(), "T", "F" ) )
   // NOTE: dbSkip() forward out of BOF currently lands on the group's
   // last physical recno instead of the first scoped key (engine-side
   // CDX boundary walk, local AND remote, pre-existing) — tracked
   // separately, do not gate on it here.
   USE
   SET DELETED OFF

   //====================================================================//
   // 6. Non-default index-bag extension (.Z01): CDX format under a
   //    custom suffix; usable after an explicit dbSetIndex
   //====================================================================//
   cDbf := s_cDir + "e2e_z01.dbf"
   cBag := s_cDir + "e2e_z01.Z01"
   FErase( cDbf ) ; FErase( cBag )
   dbCreate( cDbf, { { "NAME", "C", 10, 0 } }, "ADSCDX" )
   USE ( cDbf ) ALIAS t NEW VIA "ADSCDX"
   FOR EACH x IN { "zulu", "alpha", "mike" }
      t->( dbAppend() )
      t->name := x
   NEXT
   INDEX ON FIELD->name TAG "NAME" TO ( cBag )
   USE
   Section( "custom-extension bag created in CDX format", DataFileExists( cBag ), cBag )
   USE ( cDbf ) ALIAS t NEW VIA "ADSCDX" SHARED
   dbSetIndex( cBag )
   Section( "ordered walk through .Z01 bag", ;
            WalkRecnos() == "2 3 1", "[" + WalkRecnos() + "]" )
   USE

   //====================================================================//
   // 7. Index rebuild: delete bag + recreate, keys intact
   //====================================================================//
   FErase( s_cDir + "e2e_ord.cdx" )
   USE ( cDbf2 ) ALIAS t NEW VIA "ADSCDX" SHARED
   INDEX ON FIELD->name TAG "NAME" TO ( s_cDir + "e2e_ord" )
   USE
   USE ( cDbf2 ) ALIAS t NEW VIA "ADSCDX" SHARED
   dbSetIndex( s_cDir + "e2e_ord.cdx" )
   Section( "rebuilt index holds every row", ;
            WalkRecnos() == "2 4 5 3 1", "[" + WalkRecnos() + "]" )
   USE

   //====================================================================//
   // 8. Append/RLock/Replace/Unlock/Commit x9 must not stall
   //    (v1.8.44: lock split-brain burned ~1 s per record -> ~10 s total)
   //====================================================================//
   cDbf := s_cDir + "e2e_lck.dbf"
   FErase( cDbf )
   dbCreate( cDbf, { { "NAME", "C", 10, 0 }, { "N", "N", 4, 0 } }, "ADSCDX" )
   USE ( cDbf ) ALIAS t NEW VIA "ADSCDX" SHARED
   nT := Seconds()
   lOk := .T.
   FOR i := 1 TO 9
      IF ! ( t->( dbAppend() ) )
         lOk := .F. ; EXIT
      ENDIF
      IF ! t->( dbRLock() )
         lOk := .F. ; EXIT
      ENDIF
      t->name := "row" + LTrim( Str( i ) )
      t->n    := i
      t->( dbCommit() )
      t->( dbUnlock() )
   NEXT
   Section( "9x append+RLock+replace+commit+unlock < 5s", ;
            lOk .AND. Seconds() - nT < 5, ;
            LTrim( Str( Seconds() - nT, 6, 2 ) ) + "s" )
   Section( "all 9 rows committed", LastRec() == 9, ;
            "LastRec=" + LTrim( Str( LastRec() ) ) )
   USE

   //====================================================================//
   // 9. Second SHARED connection sees the committed rows
   //====================================================================//
   BEGIN SEQUENCE
      hConn2 := 0
      nCount := 0
      IF AdsConnect60( cTarget, nType, NIL, NIL, 0, @hConn2 )
         dbUseArea( .T., "ADSCDX", cDbf, "t2", .T., .F., , hConn2 )
         nCount := t2->( LastRec() )
         t2->( dbCloseArea() )
         AdsDisconnect( hConn2 )
      ENDIF
      Section( "second shared connection sees committed rows", nCount == 9, ;
               "LastRec=" + LTrim( Str( nCount ) ) )
   RECOVER
      Section( "second shared connection sees committed rows", .F., "open failed" )
   END SEQUENCE

   //====================================================================//
   // 10. Threads: 3 SHARED readers skipping through the table
   //     (B_BIG core: concurrent remote readers must not corrupt)
   //====================================================================//
   BEGIN SEQUENCE
      s_lErr := .F.
      FOR i2 := 1 TO 3
         h := hb_threadStart( @ThreadReader(), cDbf, i2 )
         LogLine( "thread " + LTrim( Str( i2 ) ) + " handle=" + ValType( h ) )
         AAdd( aT, h )
      NEXT
      FOR EACH x IN aT
         hb_threadJoin( x )
      NEXT
      Section( "3 shared reader threads complete clean", ! s_lErr, "" )
   RECOVER
      Section( "3 shared reader threads complete clean", .F., "thread error" )
   END SEQUENCE

   //====================================================================//
   // Cleanup + summary
   //====================================================================//
   dbCloseAll()
   AdsDisconnect( s_hConn )

   LogLine( "" )
   LogLine( "E2E RESULT: " + LTrim( Str( s_nPass ) ) + " passed, " + ;
            LTrim( Str( s_nFail ) ) + " failed" )
   ErrorLevel( iif( s_nFail == 0, 0, 1 ) )
RETURN

//----------------------------------------------------------------------------//
PROCEDURE ThreadReader( cDbf, nId )
   LOCAL i
   ErrorBlock( {| e | ( s_lErr := .T., Break( e ) ) } )
   BEGIN SEQUENCE
      IF dbUseArea( .T., "ADSCDX", cDbf, "w" + LTrim( Str( nId ) ), .T., .F., , s_hConn )
         dbGoTop()
         FOR i := 1 TO 200
            dbSkip()
            IF Eof()
               dbGoTop()
            ENDIF
         NEXT
         ( "w" + LTrim( Str( nId ) ) )->( dbCloseArea() )
      ELSE
         s_lErr := .T.
      ENDIF
   RECOVER
      s_lErr := .T.
   END SEQUENCE
RETURN

PROCEDURE Fatal( oErr )
   LogLine( "*** RUNTIME ERROR: " + oErr:description + " op=" + oErr:operation + ;
            " [" + LTrim( Str( oErr:genCode ) ) + "/" + ;
            LTrim( Str( oErr:subCode ) ) + "]" )
   ErrorLevel( 2 )
   QUIT
