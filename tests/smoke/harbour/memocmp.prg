/* memocmp.prg -- memo field binary conformance: memos written through
 * ADSCDX (OpenADS) byte-compared against memos written through Harbour
 * DBFCDX.
 *
 * Two phases:
 *   1. BUILD: creates the same table (C,N,L,D + MEMO) with the same rows
 *      and memo payloads once per RDD (mc_dbfcdx.* / mc_adscdx.*).
 *      Payloads exercise: empty memo, short text, text with CRLF, a
 *      payload crossing the memo block boundary, and a large (>4KB) one.
 *   2. COMPARE: byte-level diff of the two .dbf files and the two memo
 *      files (.fpt/.dbt -- whichever each RDD produced), then a
 *      value-level cross-read (open the ADSCDX files with DBFCDX and
 *      vice versa) so format-level differences that still interoperate
 *      are reported separately from hard corruption.
 *
 * Verdict: memocmp_verdict.txt
 *   PASS  = files byte-identical AND cross-read values equal
 *   DIFF  = byte differences (offsets listed in memocmp_diff.txt) but
 *           cross-read values equal (format-level divergence)
 *   FAIL  = values differ on cross-read (real incompatibility)
 *
 * Build/run: same hbmk2 line as navcmp.prg with this file.
 */
#include "ads.ch"

REQUEST ADS, ADSCDX, DBFCDX

STATIC nLog := -1

PROCEDURE Main()
   LOCAL cD, cA, cVerdict, h

   ErrorBlock( {|oErr| MyHandler( oErr ) } )

   BuildWith( "DBFCDX" )
   BuildWith( "ADSCDX" )

   nLog := FCreate( "memocmp_diff.txt" )

   cVerdict := "PASS"
   /* Byte-level: data file */
   IF ! BinEq( "mc_dbfcdx.dbf", "mc_adscdx.dbf", "DBF" )
      cVerdict := "DIFF"
   ENDIF
   /* Byte-level: memo file, whichever extension each RDD used */
   cD := MemoExt( "mc_dbfcdx" )
   cA := MemoExt( "mc_adscdx" )
   IF cD != cA
      LogIt( "memo container differs: DBFCDX uses " + cD + ;
             ", ADSCDX uses " + cA )
      cVerdict := "DIFF"
   ELSEIF ! BinEq( "mc_dbfcdx" + cD, "mc_adscdx" + cA, "MEMO" )
      cVerdict := "DIFF"
   ENDIF

   /* Value-level interop: swap RDDs and re-read every memo */
   IF ! CrossRead( "mc_adscdx",  "DBFCDX" ) .OR. ;
      ! CrossRead( "mc_dbfcdx",  "ADSCDX" )
      cVerdict := "FAIL"
   ENDIF

   FClose( nLog )
   nLog := -1

   h := FCreate( "memocmp_verdict.txt" )
   FWrite( h, "MEMOCMP " + cVerdict + hb_eol() )
   FClose( h )
   ? "MEMOCMP", cVerdict
   RETURN

/* -------------------------------------------------- fixtures -------- */

STATIC FUNCTION Payloads()
   LOCAL a := {}
   AAdd( a, "" )                                /* empty */
   AAdd( a, "short memo" )                      /* short */
   AAdd( a, "line1" + hb_eol() + "line2" + hb_eol() + "line3" )
   AAdd( a, Replicate( "block-boundary-test-", 40 ) )      /* ~800 bytes */
   AAdd( a, Replicate( "large-payload-", 400 ) )           /* ~6KB */
   AAdd( a, "trailing spaces kept   " )
   RETURN a

PROCEDURE BuildWith( cRDD )
   LOCAL cBase := "mc_" + Lower( cRDD )
   LOCAL aPay  := Payloads()
   LOCAL i

   fErase( cBase + ".dbf" ) ; fErase( cBase + ".cdx" )
   fErase( cBase + ".fpt" ) ; fErase( cBase + ".dbt" )

   IF cRDD == "ADSCDX"
      AdsSetFileType( ADS_CDX )
      AdsSetServerType( ADS_LOCAL_SERVER )
   ENDIF

   dbCreate( cBase, { { "NAME",   "C", 20, 0 }, ;
                      { "NUM",    "N",  4, 0 }, ;
                      { "ACTIVE", "L",  1, 0 }, ;
                      { "BORN",   "D",  8, 0 }, ;
                      { "NOTES",  "M", 10, 0 } }, cRDD )
   USE ( cBase ) VIA ( cRDD )
   FOR i := 1 TO Len( aPay )
      dbAppend()
      FIELD->NAME   := PadR( "row" + LTrim( Str( i ) ), 20 )
      FIELD->NUM    := i
      FIELD->ACTIVE := ( i % 2 == 0 )
      FIELD->BORN   := SToD( "20260115" ) + i
      FIELD->NOTES  := aPay[ i ]
   NEXT
   dbCommit()
   USE
   RETURN

/* -------------------------------------------------- compare --------- */

STATIC FUNCTION MemoExt( cBase )
   IF File( cBase + ".fpt" )
      RETURN ".fpt"
   ENDIF
   IF File( cBase + ".dbt" )
      RETURN ".dbt"
   ENDIF
   RETURN "<none>"

/* Byte-compare two files; log every mismatching region. */
STATIC FUNCTION BinEq( cF1, cF2, cTag )
   LOCAL h1, h2, n1, n2, cB1, cB2, lEq := .T.
   LOCAL nOff := 0, nShown := 0

   h1 := FOpen( cF1, 0 )
   h2 := FOpen( cF2, 0 )
   IF h1 < 0 .OR. h2 < 0
      LogIt( cTag + ": cannot open " + cF1 + " / " + cF2 )
      IF h1 >= 0 ; FClose( h1 ) ; ENDIF
      IF h2 >= 0 ; FClose( h2 ) ; ENDIF
      RETURN .F.
   ENDIF
   n1 := FSeek( h1, 0, 2 ) ; FSeek( h1, 0, 0 )
   n2 := FSeek( h2, 0, 2 ) ; FSeek( h2, 0, 0 )
   IF n1 != n2
      LogIt( cTag + ": size differs " + LTrim( Str( n1 ) ) + ;
             " vs " + LTrim( Str( n2 ) ) )
      lEq := .F.
   ENDIF
   cB1 := Space( 4096 ) ; cB2 := Space( 4096 )
   DO WHILE .T.
      n1 := FRead( h1, @cB1, 4096 )
      n2 := FRead( h2, @cB2, 4096 )
      IF n1 <= 0 .AND. n2 <= 0
         EXIT
      ENDIF
      IF n1 != n2 .OR. !( Left( cB1, n1 ) == Left( cB2, n2 ) )
         IF nShown < 5
            LogIt( cTag + ": bytes differ at offset " + ;
                   LTrim( Str( nOff ) ) )
            nShown++
         ENDIF
         lEq := .F.
      ENDIF
      nOff += Max( n1, n2 )
      IF n1 <= 0 .OR. n2 <= 0
         EXIT
      ENDIF
   ENDDO
   FClose( h1 ) ; FClose( h2 )
   IF lEq
      LogIt( cTag + ": byte-identical" )
   ENDIF
   RETURN lEq

/* Open cBase with cRDD and log hashable memo values; compare against
 * the values the producing RDD wrote. */
STATIC FUNCTION CrossRead( cBase, cRDD )
   LOCAL aPay := Payloads()
   LOCAL i, lOk := .T., cV

   IF cRDD == "ADSCDX"
      AdsSetFileType( ADS_CDX )
      AdsSetServerType( ADS_LOCAL_SERVER )
   ENDIF
   USE ( cBase ) VIA ( cRDD )
   IF NetErr()
      LogIt( "crossread: " + cRDD + " cannot open " + cBase )
      RETURN .F.
   ENDIF
   FOR i := 1 TO Len( aPay )
      dbGoto( i )
      cV := Field->NOTES
      IF !( cV == aPay[ i ] )
         LogIt( "crossread " + cRDD + " on " + cBase + ;
                ": row " + LTrim( Str( i ) ) + " memo value differs" + ;
                " (len " + LTrim( Str( Len( cV ) ) ) + " vs " + ;
                LTrim( Str( Len( aPay[ i ] ) ) ) + ")" )
         lOk := .F.
      ENDIF
   NEXT
   USE
   LogIt( "crossread " + cRDD + " on " + cBase + ": " + ;
          iif( lOk, "values OK", "VALUES DIFFER" ) )
   RETURN lOk

/* -------------------------------------------------- logging --------- */

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
