/* navcmp.prg -- navigation conformance comparison: ADSCDX (OpenADS) vs
 * DBFCDX (Harbour reference).
 *
 * Runs an identical battery of navigation operations under both RDDs and
 * writes one transcript per RDD (navcmp_dbfcdx.txt / navcmp_adscdx.txt).
 * The transcripts must be identical line by line; any difference is an
 * OpenADS navigation bug. (Golden master = Harbour DBFCDX.)
 *
 * Covers: dbGoTop, dbGoBottom, dbSkip(+1/-1/+n/-n) at and past both
 * boundaries, dbGoto, Bof/Eof/RecNo at every step, empty table (Limbo),
 * SET DELETED ON/OFF, plain USE with structural CDX (natural order),
 * USE ... INDEX (index order), dbSetOrder(0/1), dbSeek then skip.
 *
 * Console capture on some setups is unreliable, so all output goes to
 * the transcript files; the final verdict is printed AND logged.
 *
 * Build:  hbmk2 -comp=msvc64 -gtcgi -i<harbour>\contrib\rddads -lrddads
 *            -L<openads>\build\default\src\Release -lace64 navcmp.prg
 * Run:    navcmp.exe   (needs ace64.dll on PATH or next to the exe)
 */
#include "ads.ch"

REQUEST ADS, ADSCDX, DBFCDX

STATIC nLog := -1
STATIC cRDD := ""

PROCEDURE Main()
   LOCAL cD, cA, h

   ErrorBlock( {|oErr| MyHandler( oErr ) } )

   SET DELETED OFF

   BuildFixtures()

   RunRDD( "DBFCDX" )
   RunRDD( "ADSCDX" )

   /* Verdict: compare the two transcripts */
   cD := Memoread( "navcmp_dbfcdx.txt" )
   cA := Memoread( "navcmp_adscdx.txt" )
   h := FCreate( "navcmp_verdict.txt" )
   IF cD == cA
      FWrite( h, "NAVCMP PASS: ADSCDX transcript identical to DBFCDX" + hb_eol() )
      ? "NAVCMP PASS"
   ELSE
      FWrite( h, "NAVCMP FAIL: transcripts differ" + hb_eol() )
      ? "NAVCMP FAIL"
   ENDIF
   FClose( h )
   RETURN

/* -------------------------------------------------- fixtures -------- */

PROCEDURE BuildFixtures()
   LOCAL i

   /* Natural-order table: NO structural CDX may exist next to it */
   fErase( "nv_nat.dbf" ); fErase( "nv_nat.cdx" )
   dbCreate( "nv_nat", { { "NAME", "C", 20, 0 }, ;
                         { "NUM",  "N",  4, 0 } }, "DBFCDX" )
   USE nv_nat VIA "DBFCDX"
   FOR i := 1 TO 10
      dbAppend()
      FIELD->NAME := PadR( "name" + LTrim( Str( i ) ), 20 )
      FIELD->NUM  := i
   NEXT
   USE

   /* Table WITH structural CDX (tag on NAME) */
   fErase( "nv_idx.dbf" ); fErase( "nv_idx.cdx" )
   dbCreate( "nv_idx", { { "NAME", "C", 20, 0 }, ;
                         { "NUM",  "N",  4, 0 } }, "DBFCDX" )
   USE nv_idx VIA "DBFCDX"
   FOR i := 1 TO 10
      dbAppend()
      FIELD->NAME := PadR( "name" + LTrim( Str( i ) ), 20 )
      FIELD->NUM  := i
   NEXT
   INDEX ON NAME TAG NAME TO nv_idx
   USE

   /* Empty table */
   fErase( "nv_empty.dbf" ); fErase( "nv_empty.cdx" )
   dbCreate( "nv_empty", { { "NAME", "C", 20, 0 } }, "DBFCDX" )
   RETURN

/* -------------------------------------------------- driver ---------- */

PROCEDURE RunRDD( c )
   cRDD := c
   fErase( "navcmp_" + Lower( c ) + ".txt" )
   nLog := FCreate( "navcmp_" + Lower( c ) + ".txt" )

   IF c == "ADSCDX"
      AdsSetFileType( ADS_CDX )
      AdsSetServerType( ADS_LOCAL_SERVER )
   ENDIF

   BattNatural()
   BattEmpty()
   BattStructural()
   BattExplicitIndex()
   BattDeleted()
   BattSeek()
   BattScope()

   FClose( nLog )
   nLog := -1
   RETURN

/* -------------------------------------------------- batteries ------- */

/* Natural order: no index anywhere near the table */
PROCEDURE BattNatural()
   Sect( "natural: open state" )
   USE nv_nat VIA ( cRDD )
   State( "open" )

   Sect( "natural: GoTop / GoBottom" )
   dbGoTop()    ; State( "gotop" )
   dbGoBottom() ; State( "gobottom" )

   Sect( "natural: skip past EOF" )
   dbSkip()     ; State( "skip+1 @last" )
   dbSkip()     ; State( "skip+1 @eof" )
   dbSkip( 5 )  ; State( "skip+5 @eof" )
   dbSkip( -1 ) ; State( "skip-1 @eof" )
   State( "after back-from-eof" )

   Sect( "natural: skip past BOF" )
   dbGoTop()    ; State( "gotop again" )
   dbSkip( -1 ) ; State( "skip-1 @first" )
   dbSkip( -1 ) ; State( "skip-1 @bof" )
   dbSkip( 1 )  ; State( "skip+1 @bof" )

   Sect( "natural: dbGoto" )
   dbGoto( 5 )  ; State( "goto 5" )
   dbGoto( 1 )  ; State( "goto 1" )
   dbGoto( 10 ) ; State( "goto lastrec" )

   Sect( "natural: long skips" )
   dbGoTop()    ; State( "gotop" )
   dbSkip( 4 )  ; State( "skip+4" )
   dbSkip( -2 ) ; State( "skip-2" )
   dbSkip( 100 )  ; State( "skip+100" )
   dbSkip( -100 ) ; State( "skip-100 @eof" )
   USE
   RETURN

/* Empty table: BOF+EOF (Limbo) behaviour.
 * NOTE: the two skip steps are normalised. Harbour's rddads wrapper
 * (ads1.c adsSkip) never forwards a skip to ACE when the workarea is
 * unpositioned -- it just sets the direction flag locally and leaves
 * the opposite one untouched, so Bof()/Eof() after a skip on an empty
 * table differ from DBFCDX no matter what the ACE server answers
 * (native SAP ADS included). The engine-level transition IS verified
 * in tests/unit/abi_navigation_test.cpp, where AdsSkip really reaches
 * the engine. */
PROCEDURE BattEmpty()
   Sect( "empty: open + nav" )
   USE nv_empty VIA ( cRDD )
   State( "open" )
   dbGoTop()    ; State( "gotop" )
   dbGoBottom() ; State( "gobottom" )
   dbSkip()
   LogIt( "skip+1: <rddads does not forward unpositioned skips to ACE>" )
   dbSkip( -1 )
   LogIt( "skip-1: <rddads does not forward unpositioned skips to ACE>" )
   USE
   RETURN

/* Plain USE of a table that has a structural CDX: controlling order
 * must stay NATURAL until the app picks a tag (Pritpal's GoBottom bug) */
PROCEDURE BattStructural()
   Sect( "structural: plain use keeps natural order" )
   USE nv_idx VIA ( cRDD )
   State( "open" )
   LogIt( "ordcount=" + LTrim( Str( OrdCount() ) ) + ;
          " ordname=[" + OrdName() + "]" )
   dbGoBottom() ; State( "gobottom" )
   dbGoTop()    ; State( "gotop" )
   dbSkip( 3 )  ; State( "skip+3" )

   Sect( "structural: setorder 1 then 0" )
   dbSetOrder( 1 )
   LogIt( "ordname=[" + OrdName() + "]" )
   dbGoTop()    ; State( "gotop ord1" )
   dbGoBottom() ; State( "gobottom ord1" )
   dbSetOrder( 0 )
   LogIt( "ordname=[" + OrdName() + "]" )
   dbGoBottom() ; State( "gobottom ord0" )
   dbGoTop()    ; State( "gotop ord0" )
   USE
   RETURN

/* USE ... INDEX: controlling order active from open */
PROCEDURE BattExplicitIndex()
   Sect( "index: use with index clause" )
   USE nv_idx INDEX nv_idx VIA ( cRDD )
   State( "open" )
   LogIt( "ordname=[" + OrdName() + "]" )
   dbGoTop()    ; State( "gotop" )
   dbGoBottom() ; State( "gobottom" )
   dbSkip()     ; State( "skip+1 @last key" )
   dbSkip( -1 ) ; State( "skip-1 @eof" )
   USE
   RETURN

/* SET DELETED ON/OFF navigation (records 2 and 10 deleted) */
PROCEDURE BattDeleted()
   Sect( "deleted: off baseline" )
   SET DELETED OFF
   USE nv_nat VIA ( cRDD )
   dbGoto( 2 )  ; dbDelete()
   dbGoto( 10 ) ; dbDelete()
   dbGoTop()    ; State( "gotop del-off" )
   dbGoBottom() ; State( "gobottom del-off" )

   Sect( "deleted: on" )
   SET DELETED ON
   dbGoTop()    ; State( "gotop del-on" )
   dbGoBottom() ; State( "gobottom del-on" )
   dbSkip()     ; State( "skip+1 @last live" )
   dbSkip( -1 ) ; State( "skip-1 @eof" )
   dbGoTop()
   dbSkip( -1 ) ; State( "skip-1 @first live" )
   dbSkip( 1 )  ; State( "skip+1 @bof" )

   Sect( "deleted: off again" )
   SET DELETED OFF
   dbGoTop()    ; State( "gotop del-off2" )
   dbGoBottom() ; State( "gobottom del-off2" )
   USE
   SET DELETED OFF

   /* restore rows for the next RDD pass */
   USE nv_nat VIA ( cRDD )
   Recall All
   USE
   RETURN

/* Seek then navigate from the found position */
PROCEDURE BattSeek()
   Sect( "seek: hit / miss / nav from hit" )
   USE nv_idx INDEX nv_idx VIA ( cRDD )
   dbSeek( "name5" ) ; StateF( "seek name5" )
   dbSkip()          ; State( "skip+1 after hit" )
   dbSeek( "zzzz" )  ; StateF( "seek miss" )
   dbSkip( -1 )      ; State( "skip-1 after miss" )
   USE
   RETURN

/* -------------------------------------------------- logging --------- */

PROCEDURE Sect( c )
   LogIt( "-- " + c )
   RETURN

PROCEDURE State( cTag )
   LogIt( cTag + ;
          ": rec=" + LTrim( Str( RecNo() ) ) + ;
          " lastrec=" + LTrim( Str( LastRec() ) ) + ;
          " eof=" + iif( Eof(), "1", "0" ) + ;
          " bof=" + iif( Bof(), "1", "0" ) + ;
          " key=[" + iif( Eof(), "<eof>", FieldGet( 1 ) ) + "]" )
   RETURN

PROCEDURE StateF( cTag )
   LogIt( cTag + ;
          ": found=" + iif( Found(), "1", "0" ) + ;
          " rec=" + LTrim( Str( RecNo() ) ) + ;
          " eof=" + iif( Eof(), "1", "0" ) + ;
          " bof=" + iif( Bof(), "1", "0" ) )
   RETURN

/* SIXDRIVER SetScope(desde, hasta) pattern: select the client-code
 * index, scope to one value (or a range), then WHILE !EOF() / BROWSE
 * only sees the scoped rows. Harbour mapping: OrdScope(0,x) +
 * OrdScope(1,x) -> AdsSetScope* on the ACE side. */
PROCEDURE BattScope()
   LOCAL n
   Sect( "scope: sixdriver setscope pattern" )
   USE nv_idx INDEX nv_idx VIA ( cRDD )
   OrdSetFocus( "NAME" )

   /* facturas de UN cliente: SetScope(cod, cod) */
   OrdScope( 0, "name5" )
   OrdScope( 1, "name5" )
   n := 0
   dbGoTop()
   DO WHILE ! Eof()
      n++
      LogIt( "  scoped row rec=" + LTrim( Str( RecNo() ) ) + ;
             " name=[" + RTrim( FIELD->NAME ) + "]" )
      dbSkip()
   ENDDO
   LogIt( "scope name5..name5 rows=" + LTrim( Str( n ) ) + ;
          " eof=" + iif( Eof(), "T", "F" ) )

   /* rango: name3..name5 (orden alfabético de claves) */
   OrdScope( 0, "name3" )
   OrdScope( 1, "name5" )
   n := 0
   dbGoTop()
   DO WHILE ! Eof()
      n++
      dbSkip()
   ENDDO
   LogIt( "scope name3..name5 rows=" + LTrim( Str( n ) ) )
   dbGoBottom()
   LogIt( "scoped gobottom=[" + RTrim( FIELD->NAME ) + "]" )

   /* scope fuera de rango: 0 filas, EOF inmediato */
   OrdScope( 0, "zzz" )
   OrdScope( 1, "zzz" )
   dbGoTop()
   LogIt( "scope zzz..zzz eof=" + iif( Eof(), "T", "F" ) + ;
          " bof=" + iif( Bof(), "T", "F" ) )

   /* limpiar: walk completo otra vez */
   OrdScope( 0, "" )
   OrdScope( 1, "" )
   n := 0
   dbGoTop()
   DO WHILE ! Eof()
      n++
      dbSkip()
   ENDDO
   LogIt( "scope cleared rows=" + LTrim( Str( n ) ) )
   USE
   LogIt( "" )
   RETURN

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
