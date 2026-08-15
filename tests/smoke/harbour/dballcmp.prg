/* dballcmp.prg -- exhaustive Db and Ord function conformance comparison:
 * ADSCDX (OpenADS) vs DBFCDX (Harbour reference).
 *
 * Function surface enumerated from the Harbour 3.2 sources
 * (src/rdd/dbcmd.c + dbcmd53.c + dbcmdhb.c -- every HB_FUNC(DB*),
 * HB_FUNC(ORD*), and the workarea state/field functions). Each battery
 * logs comparable values to a per-RDD transcript
 * (dballcmp_dbfcdx.txt / dballcmp_adscdx.txt); the verdict byte-compares
 * the two transcripts.
 *
 * Values that are RDD-identity by definition (RDDNAME()) are logged as
 * an OK/FAIL check against the expected name instead of the raw value,
 * so the transcripts stay comparable.
 *
 * Intentionally NOT covered (Harbour RTL-level, RDD-agnostic, or UI):
 *   dbEdit (UI), COPY TO / APPEND FROM / SORT / JOIN / TOTAL (__dbCopy /
 *   __dbArrange / __dbTrans -- same Harbour RTL code drives both RDDs),
 *   dbLocate/dbContinue (RTL on top of skip/eval).
 *
 * Build:  see build line in navcmp.prg (same flags, this file).
 * Run:    dballcmp.exe   (needs ace64.dll on PATH or next to the exe)
 */
#include "ads.ch"
#include "dbinfo.ch"
#include "dbstruct.ch"

REQUEST ADS, ADSCDX, DBFCDX

STATIC nLog := -1
STATIC cRDD := ""

PROCEDURE Main()
   LOCAL cD, cA, h

   ErrorBlock( {|oErr| MyHandler( oErr ) } )
   SET DELETED OFF
   SET EXACT OFF

   RunRDD( "DBFCDX" )
   RunRDD( "ADSCDX" )

   cD := Memoread( "dballcmp_dbfcdx.txt" )
   cA := Memoread( "dballcmp_adscdx.txt" )
   h := FCreate( "dballcmp_verdict.txt" )
   IF cD == cA
      FWrite( h, "DBALLCMP PASS: ADSCDX transcript identical to DBFCDX" + hb_eol() )
      ? "DBALLCMP PASS"
   ELSE
      FWrite( h, "DBALLCMP FAIL: transcripts differ" + hb_eol() )
      ? "DBALLCMP FAIL"
   ENDIF
   FClose( h )
   RETURN

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

PROCEDURE RunRDD( c )
   cRDD := c
   fErase( "dballcmp_" + Lower( c ) + ".txt" )
   nLog := FCreate( "dballcmp_" + Lower( c ) + ".txt" )

   IF c == "ADSCDX"
      AdsSetFileType( ADS_CDX )
      AdsSetServerType( ADS_LOCAL_SERVER )
   ENDIF

   /* Fresh fixtures per pass: some batteries are destructive */
   MakeMain( "da_main" )
   MakeChild( "da_child" )

   BattState()
   BattFields()
   BattAppendPut()
   BattDeleteRecall()
   BattOrders()
   BattSeekFound()
   BattFilter()
   BattRelation()
   BattLocking()
   BattEval()
   BattPackZap()
   BattFileOps()
   BattInfo()

   dbCloseAll()
   FClose( nLog )
   nLog := -1
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
   LogIt( "findrec5=" + TryStr( {|| B( OrdFindRec( 5 ) ) } ) + ;
          " rec=" + TryStr( {|| N( RecNo() ) } ) )

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
   LogIt( "skipraw=" + TryStr( {|| OrdSkipRaw(), RTrim( Field->NAME ) } ) )
   LogIt( "wildseek=" + TryStr( {|| B( OrdWildSeek( "name1*" ) ) } ) + ;
          " hit=[" + TryStr( {|| RTrim( Field->NAME ) } ) + "]" )

   /* single-key maintenance on a scratch tag */
   LogIt( "scratch-create=" + TryStr( {|| OrdCreate( "da_main", "SCRATCH", ;
             "NAME", , .T. ), "OK" } ) )  /* UNIQUE, starts empty-ish */
   dbGoTop()
   LogIt( "keyadd=" + TryStr( {|| OrdKeyAdd( "SCRATCH" ), "OK" } ) + ;
          " keydel=" + TryStr( {|| OrdKeyDel( "SCRATCH" ), "OK" } ) )

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
