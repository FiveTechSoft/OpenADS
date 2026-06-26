/*
 * qamatrix.prg -- OpenADS differential QA harness (pure Harbour, no ORM).
 *
 * Runs the SAME classic xBase operations against one RDD and writes a
 * normalized, diffable log. Run it once per RDD and diff same-family:
 *     DBFCDX (oracle)  vs  ADSCDX (OpenADS)
 *     DBFNTX (oracle)  vs  ADSNTX (OpenADS)
 * Any differing line = candidate bug ("erros bobos": INDEX ON / REINDEX /
 * SEEK / ascending-descending / scope / filter / pack...).
 *
 * Usage:  qamatrix.exe <RDD> <logfile>
 *         <RDD> in { DBFCDX, DBFNTX, ADSCDX, ADSNTX }
 */

#include "common.ch"
#include "ads.ch"
#include "rddsys.ch"
#include "set.ch"
#include "dbinfo.ch"
#include "ord.ch"

REQUEST ADS, ADSCDX, ADSNTX
REQUEST DBFCDX, DBFNTX, DBFFPT

STATIC s_cLog := ""
STATIC s_cLogFile := ""

PROCEDURE Main( cRdd, cLogFile )

   LOCAL lAds, lCdx, cDir, cDbf, cExt

   DEFAULT cRdd     TO "DBFCDX"
   DEFAULT cLogFile TO "qa_" + cRdd + ".log"
   s_cLogFile := cLogFile

   ErrorBlock( {| e | QaErr( e ) } )

   cRdd := Upper( AllTrim( cRdd ) )
   lAds := ( Left( cRdd, 3 ) == "ADS" )
   lCdx := ( "CDX" $ cRdd )
   cExt := iif( lCdx, ".cdx", ".ntx" )

   QaLog( "### RDD=" + cRdd + " family=" + iif( lCdx, "CDX", "NTX" ) + " ads=" + L2( lAds ) )

   cDir := hb_DirTemp() + "oa_qa_" + cRdd
   hb_DirCreate( cDir )
   cDbf := cDir + hb_ps() + "qa.dbf"

   IF lAds
      AdsSetServerType( ADS_LOCAL_SERVER )
      AdsSetFileType( iif( lCdx, ADS_CDX, ADS_NTX ) )
      IF ! AdsConnect( cDir )
         QaLog( "FATAL: AdsConnect failed DosError=" + LTrim( Str( DosError() ) ) )
         Flush( cLogFile ) ; QUIT
      ENDIF
   ENDIF
   RddSetDefault( cRdd )

   CleanFiles( cDbf, lCdx )

   /* ---------- 1. create + populate -------------------------------- */
   dbCreate( cDbf, { ;
       { "NAME",  "C", 20, 0 }, ;
       { "AGE",   "N",  3, 0 }, ;
       { "SALARY","N", 10, 2 }, ;
       { "HIRED", "D",  8, 0 }, ;
       { "ACTIVE","L",  1, 0 }, ;
       { "NOTES", "M", 10, 0 } }, cRdd )

   USE ( cDbf ) VIA cRdd NEW EXCLUSIVE
   QaLog( "open: alias=" + Alias() + " rddName=" + rddName() )

   Populate()
   QaLog( "reccount after append=" + LTrim( Str( LastRec() ) ) )

   /* ---------- 2. INDEX ON (char, case-sensitive) ------------------ */
   MakeIndex( lCdx, "NAME", "TNAME", cDbf, "" )
   WalkOrder( "TNAME", "idx char NAME asc" )

   /* ---------- 3. INDEX ON UPPER() (case-insensitive) -------------- */
   MakeIndex( lCdx, "UPPER(NAME)", "TUPNAME", cDbf, "" )
   WalkOrder( "TUPNAME", "idx UPPER(NAME) asc" )

   /* ---------- 4. INDEX ON numeric -------------------------------- */
   MakeIndex( lCdx, "AGE", "TAGE", cDbf, "" )
   WalkOrder( "TAGE", "idx AGE asc" )

   /* ---------- 5. INDEX ON date ----------------------------------- */
   MakeIndex( lCdx, "DTOS(HIRED)", "THIRED", cDbf, "" )
   WalkOrder( "THIRED", "idx DTOS(HIRED) asc" )

   /* ---------- 6. conditional index (FOR) ------------------------- */
   MakeIndex( lCdx, "AGE", "TACT", cDbf, "ACTIVE" )
   QaLog( "cond idx FOR ACTIVE: keycount=" + LTrim( Str( OrdKeyCount( "TACT" ) ) ) )

   /* ---------- 7. exact seek hit / miss --------------------------- */
   SetOrd( lCdx, "TNAME" )
   dbSeek( "Bob" )
   QaLog( "seek 'Bob' exact: found=" + L2( Found() ) + " rec=" + LTrim( Str( RecNo() ) ) )
   dbSeek( "Ztop" )
   QaLog( "seek 'Ztop' (miss): found=" + L2( Found() ) + " eof=" + L2( Eof() ) )

   /* ---------- 8. soft seek --------------------------------------- */
   SetOrd( lCdx, "TAGE" )
   SET SOFTSEEK ON
   dbSeek( 31 )
   QaLog( "softseek AGE>=31 lands: age=" + LTrim( Str( FIELD->AGE ) ) + " found=" + L2( Found() ) )
   SET SOFTSEEK OFF

   /* ---------- 9. descending walk --------------------------------- */
   SetOrd( lCdx, "TAGE" )
   dbGoBottom()
   QaLog( "AGE desc (skip -1): " + WalkDir( -1 ) )

   /* ---------- 10. REINDEX ---------------------------------------- */
   OrdListRebuild()
   SetOrd( lCdx, "TNAME" )
   QaLog( "after REINDEX: keycount TNAME=" + LTrim( Str( OrdKeyCount() ) ) + ;
        " reccount=" + LTrim( Str( LastRec() ) ) )
   WalkOrder( "TNAME", "post-reindex NAME asc" )

   ResetState( lCdx )
   /* ---------- 11. SET FILTER ------------------------------------- */
   SetOrd( lCdx, "TAGE" )
   SET FILTER TO AGE >= 30
   dbGoTop()
   QaLog( "filter AGE>=30: " + WalkDir( 1 ) )
   SET FILTER TO
   QaLog( "after clear filter, reccount=" + LTrim( Str( LastRec() ) ) )

   ResetState( lCdx )
   /* ---------- 12. ordScope range --------------------------------- */
   SetOrd( lCdx, "TAGE" )
   OrdScope( 0, 28 )
   OrdScope( 1, 40 )
   dbGoTop()
   QaLog( "scope AGE[28..40]: " + WalkDir( 1 ) )
   OrdScope( 0, NIL )
   OrdScope( 1, NIL )

   ResetState( lCdx )
   /* ---------- 13. LOCATE / CONTINUE ------------------------------ */
   SetOrd( lCdx, "" )         // natural order
   LOCATE FOR FIELD->ACTIVE
   QaLog( "locate ACTIVE: found=" + L2( Found() ) + " rec=" + LTrim( Str( RecNo() ) ) )
   CONTINUE
   QaLog( "continue: found=" + L2( Found() ) + " rec=" + LTrim( Str( RecNo() ) ) )

   /* ---------- 14. APPEND + REPLACE, re-seek (index maint) -------- */
   SetOrd( lCdx, "TNAME" )
   dbAppend()
   FIELD->NAME := "Zeta" ; FIELD->AGE := 50 ; FIELD->ACTIVE := .T.
   FIELD->HIRED := hb_SToD( "20200101" ) ; FIELD->SALARY := 999.99
   dbCommit()
   dbSeek( "Zeta" )
   QaLog( "append+seek 'Zeta': found=" + L2( Found() ) + " age=" + LTrim( Str( FIELD->AGE ) ) )

   /* ---------- 15. DELETE + SET DELETED + PACK -------------------- */
   dbSeek( "Bob" )
   IF Found() ; dbDelete() ; ENDIF
   SET DELETED ON
   dbGoTop()
   QaLog( "with DELETED ON, visible reccount=" + LTrim( Str( CountVisible() ) ) )
   SET DELETED OFF
   PACK
   QaLog( "after PACK: reccount=" + LTrim( Str( LastRec() ) ) )
   SetOrd( lCdx, "TNAME" )
   WalkOrder( "TNAME", "post-pack NAME asc" )

   /* ---------- 16. memo write/read -------------------------------- */
   dbGoTop()
   FIELD->NOTES := "line one" + Chr(13)+Chr(10) + "line two"
   dbCommit()
   QaLog( "memo roundtrip len=" + LTrim( Str( Len( AllTrim( FIELD->NOTES ) ) ) ) + ;
        " empty=" + L2( Empty( FIELD->NOTES ) ) )

   /* ---------- 17. ordKeyNo / ordKeyGoto -------------------------- */
   SetOrd( lCdx, "TAGE" )
   dbGoTop()
   QaLog( "ordKeyNo at top=" + LTrim( Str( OrdKeyNo() ) ) + ;
        " ordKeyCount=" + LTrim( Str( OrdKeyCount() ) ) )

   dbCloseArea()
   IF lAds ; AdsDisconnect() ; ENDIF

   Flush( cLogFile )
   RETURN

/* ==================== helpers ==================== */

STATIC PROCEDURE Populate()
   LOCAL i
   LOCAL aN := { "Charlie","alice","Bob","dave","Eve","bob","Mary","tom" }
   LOCAL aA := { 30, 25, 40, 22, 35, 28, 45, 31 }
   LOCAL aS := { 5000.50, 3200.00, 8800.25, 2100.10, 6000.00, 2900.00, 9500.75, 4400.40 }
   LOCAL aH := { "20180312","20200115","20100506","20210901","20150722","20220203","20050610","20190830" }
   LOCAL aAct := { .T., .F., .T., .T., .F., .T., .T., .F. }
   FOR i := 1 TO Len( aN )
      dbAppend()
      FIELD->NAME   := aN[ i ]
      FIELD->AGE    := aA[ i ]
      FIELD->SALARY := aS[ i ]
      FIELD->HIRED  := hb_SToD( aH[ i ] )
      FIELD->ACTIVE := aAct[ i ]
   NEXT
   dbCommit()
   RETURN

STATIC PROCEDURE MakeIndex( lCdx, cExpr, cTag, cDbf, cFor )
   LOCAL cCmd
   IF lCdx
      IF Empty( cFor )
         OrdCondSet()
         OrdCreate( hb_FNameExtSet( cDbf, ".cdx" ), cTag, cExpr )
      ELSE
         OrdCondSet( cFor, &( "{||" + cFor + "}" ) )
         OrdCreate( hb_FNameExtSet( cDbf, ".cdx" ), cTag, cExpr, , .F. )
      ENDIF
   ELSE
      // NTX: one index file per order; use tag name as filename stem
      IF Empty( cFor )
         OrdCondSet()
         OrdCreate( cTag, cTag, cExpr )
      ELSE
         OrdCondSet( cFor, &( "{||" + cFor + "}" ) )
         OrdCreate( cTag, cTag, cExpr, , .F. )
      ENDIF
   ENDIF
   RETURN

STATIC PROCEDURE SetOrd( lCdx, cTag )
   IF Empty( cTag )
      OrdSetFocus( 0 )
   ELSE
      OrdSetFocus( cTag )
   ENDIF
   RETURN

STATIC PROCEDURE WalkOrder( cTag, cLabel )
   OrdSetFocus( cTag )
   dbGoTop()
   QaLog( cLabel + ": " + WalkDir( 1 ) )
   RETURN

STATIC FUNCTION WalkDir( nStep )
   LOCAL cOut := ""
   DO WHILE ! Eof() .AND. ! Bof()
      cOut += AllTrim( FIELD->NAME ) + "(" + LTrim( Str( FIELD->AGE ) ) + ") "
      dbSkip( nStep )
   ENDDO
   RETURN AllTrim( cOut )

STATIC FUNCTION CountVisible()
   LOCAL n := 0
   dbGoTop()
   DO WHILE ! Eof()
      n++
      dbSkip()
   ENDDO
   RETURN n

STATIC PROCEDURE CleanFiles( cDbf, lCdx )
   IF File( cDbf )
      FErase( cDbf )
      FErase( hb_FNameExtSet( cDbf, ".cdx" ) )
      FErase( hb_FNameExtSet( cDbf, ".fpt" ) )
      FErase( hb_FNameExtSet( cDbf, ".dbt" ) )
   ENDIF
   RETURN

STATIC PROCEDURE QaLog( cLine )
   s_cLog += cLine + Chr(10)
   IF ! Empty( s_cLogFile )
      MemoWrit( s_cLogFile, s_cLog )   // incremental: survives hang/crash
   ENDIF
   RETURN

STATIC FUNCTION L2( lVal )
   RETURN iif( lVal, "T", "F" )

/* Headless error trap: record the runtime error (no GUI alert box) and quit. */
STATIC FUNCTION QaErr( oErr )
   LOCAL cDesc := ""
   IF ValType( oErr ) == "O"
      cDesc := iif( ValType(oErr:description)=="C", oErr:description, "" ) + ;
               " [op=" + iif(ValType(oErr:operation)=="C", oErr:operation, "") + ;
               " sub=" + LTrim(Str(iif(ValType(oErr:subCode)=="N", oErr:subCode, -1))) + "]"
   ENDIF
   QaLog( "RUNTIME-ERROR: " + cDesc )
   Flush( s_cLogFile )
   ErrorLevel( 2 )
   QUIT
   RETURN .F.

/* Clear all positional/order state so independent sections don't cascade. */
STATIC PROCEDURE ResetState( lCdx )
   HB_SYMBOL_UNUSED( lCdx )
   dbClearFilter()
   OrdScope( 0, NIL )
   OrdScope( 1, NIL )
   OrdSetFocus( 0 )
   SET SOFTSEEK OFF
   SET DELETED OFF
   dbGoTop()
   RETURN

STATIC PROCEDURE Flush( cLogFile )
   MemoWrit( cLogFile, s_cLog )
   RETURN
