//-------------------------------------------------------------------//
// mt_create_bench.prg — MT table+CDX create bench (DBFCDX vs ADSCDX)
//
// Unit-test helper invoked by abi_mt_create_vs_dbfcdx_test.cpp.
// Creates MT_COUNT tables (default 40) on MT_THREADS workers (default 4),
// each with 10 fixed rows and a 3-tag CDX. Prints one machine-readable
// RESULT line so the C++ unit test can parse timings.
//
// Env:
//   MT_COUNT=40  MT_THREADS=4  USE_RDD=DBFCDX|ADSCDX
//   MT_DIR=...   (work directory; default hb_DirBase()+"data")
//
// Exit: 0 = PASS (all instances identical size + LastRec==10)
//       1 = FAIL
//
// Policy: every ADSCDX speed claim is compared against a Harbour DBFCDX
// run of the same workload (the C++ driver runs both RDDs).
//-------------------------------------------------------------------//

#include "ads.ch"

REQUEST DBFCDX, ADS, ADSCDX

STATIC s_cRdd
STATIC s_cDir
STATIC s_hMtx
STATIC s_aErrors
STATIC s_nOk

#define RECCOUNT  10

PROCEDURE Main()
   LOCAL nCount   := Val( GetEnv( "MT_COUNT" ) )
   LOCAL nThreads := Val( GetEnv( "MT_THREADS" ) )
   LOCAL cRdd     := Upper( AllTrim( GetEnv( "USE_RDD" ) ) )
   LOCAL cDir     := GetEnv( "MT_DIR" )
   LOCAL nMs, t0, t1, lOk

   IF nCount == 0
      nCount := 40
   ENDIF
   IF nThreads == 0
      nThreads := 4
   ENDIF
   IF Empty( cRdd )
      cRdd := "DBFCDX"
   ENDIF
   IF Empty( cDir )
      cDir := hb_DirBase() + "data"
   ENDIF
   // Ensure trailing separator
   IF Right( cDir, 1 ) != hb_ps()
      cDir += hb_ps()
   ENDIF
   s_cDir := cDir
   s_cRdd := cRdd
   s_hMtx := hb_mutexCreate()
   s_aErrors := {}
   s_nOk := 0

   MakeDirSafe( s_cDir )
   WipeFiles()

   IF cRdd == "ADSCDX"
      rddSetDefault( "ADSCDX" )
      AdsSetFileType( ADS_CDX )
      AdsSetServerType( ADS_LOCAL_SERVER )
   ELSE
      rddSetDefault( "DBFCDX" )
      cRdd := "DBFCDX"
      s_cRdd := cRdd
   ENDIF

   t0 := hb_MilliSeconds()
   lOk := RunWorkers( nCount, nThreads )
   t1 := hb_MilliSeconds()
   nMs := t1 - t0

   IF lOk
      lOk := VerifyAll( nCount )
   ENDIF

   // Machine-readable one-liner for the C++ unit test parser.
   OutStd( "RESULT rdd=" + cRdd + ;
           " count=" + hb_ntos( nCount ) + ;
           " threads=" + hb_ntos( nThreads ) + ;
           " ms=" + hb_ntos( nMs ) + ;
           " ok=" + iif( lOk, "1", "0" ) + ;
           " errors=" + hb_ntos( Len( s_aErrors ) ) + ;
           hb_eol() )

   IF ! Empty( s_aErrors )
      AEval( s_aErrors, {| e | OutErr( "ERR " + e + hb_eol() ) } )
   ENDIF

   ErrorLevel( iif( lOk, 0, 1 ) )
   RETURN


STATIC FUNCTION RunWorkers( nCount, nThreads )
   LOCAL nTh, nFirst, nLast, nPer, aThreads := {}
   LOCAL lOk := .T.

   nPer   := Max( 1, Int( nCount / nThreads ) )
   nFirst := 1
   FOR nTh := 1 TO nThreads
      IF nFirst > nCount
         EXIT
      ENDIF
      nLast := iif( nTh == nThreads, nCount, Min( nCount, nFirst + nPer - 1 ) )
      AAdd( aThreads, hb_threadStart( @MakeTables(), nFirst, nLast ) )
      nFirst := nLast + 1
   NEXT
   AEval( aThreads, {| h | hb_threadJoin( h ) } )

   IF Len( s_aErrors ) > 0
      lOk := .F.
   ENDIF
   RETURN lOk


STATIC FUNCTION MakeTables( nFirst, nLast )
   LOCAL n, cDbf, cIdx, aRec
   LOCAL aStru := { { "NAME", "C", 19, 0 }, ;
                    { "CITY", "C", 15, 0 }, ;
                    { "AGE" , "N",  3, 0 }, ;
                    { "INS" , "N",  4, 0 }, ;
                    { "RDD" , "C",  3, 0 } }
   LOCAL aData := { { "Alice"  , "Madrid"    }, ;
                    { "Bob"    , "Barcelona" }, ;
                    { "Phillip", "Panipat"   }, ;
                    { "Charlie", "Valencia"  }, ;
                    { "Linda"  , "Iris"      }, ;
                    { "Finland", "Dallas"    }, ;
                    { "Diana"  , "Sevilla"   }, ;
                    { "Lucy"   , "Sevilla"   }, ;
                    { "Jony"   , "Walker"    }, ;
                    { "Edward" , "Bilbao"    } }

   IF s_cRdd == "ADSCDX"
      AdsSetFileType( ADS_CDX )
      AdsSetServerType( ADS_LOCAL_SERVER )
   ENDIF

   FOR n := nFirst TO nLast
      cDbf := RddName( n, ".dbf" )
      cIdx := RddName( n, ".cdx" )
      BEGIN SEQUENCE
         dbCreate( cDbf, aStru, s_cRdd )
         dbCloseArea()
         USE ( cDbf ) EXCLUSIVE NEW VIA ( s_cRdd )
         FOR EACH aRec IN aData
            dbAppend()
            FIELD->NAME := aRec[ 1 ]
            FIELD->CITY := aRec[ 2 ]
            FIELD->AGE  := 20 + aRec:__enumIndex()
            FIELD->INS  := aRec:__enumIndex()
            FIELD->RDD  := s_cRdd
         NEXT
         INDEX ON FIELD->NAME TAG "IDX01" TO ( cIdx )
         INDEX ON FIELD->CITY TAG "IDX02" TO ( cIdx )
         INDEX ON FIELD->INS  TAG "IDX03" TO ( cIdx )
         dbCloseArea()
         hb_mutexLock( s_hMtx )
         s_nOk++
         hb_mutexUnlock( s_hMtx )
      RECOVER USING oErr
         BEGIN SEQUENCE
            dbCloseArea()
         END SEQUENCE
         hb_mutexLock( s_hMtx )
         AAdd( s_aErrors, hb_ntos( n ) + ": " + ;
            iif( ValType( oErr ) == "O", hb_ValToStr( oErr:description ) + ;
                 " (" + hb_ValToStr( oErr:subCode ) + ")", "unknown" ) )
         hb_mutexUnlock( s_hMtx )
      END SEQUENCE
   NEXT
   RETURN NIL


STATIC FUNCTION VerifyAll( nCount )
   LOCAL n, nDbf, nIdx, nRec, nDbf0 := -1, nIdx0 := -1
   LOCAL lOk := .T.

   FOR n := 1 TO nCount
      nDbf := hb_FSize( TableName( n ) )
      nIdx := hb_FSize( IndexName( n ) )
      nRec := -1
      BEGIN SEQUENCE
         USE ( RddName( n, ".dbf" ) ) READONLY SHARED NEW VIA ( s_cRdd )
         nRec := LastRec()
         dbCloseArea()
      RECOVER
         nRec := -1
         BEGIN SEQUENCE
            dbCloseArea()
         END SEQUENCE
      END SEQUENCE

      IF n == 1
         nDbf0 := nDbf
         nIdx0 := nIdx
      ENDIF
      IF nDbf <= 0 .OR. nIdx <= 0 .OR. nRec != RECCOUNT .OR. ;
         nDbf != nDbf0 .OR. nIdx != nIdx0
         lOk := .F.
         hb_mutexLock( s_hMtx )
         AAdd( s_aErrors, "verify fail inst=" + hb_ntos( n ) + ;
            " dbf=" + hb_ntos( nDbf ) + " cdx=" + hb_ntos( nIdx ) + ;
            " rec=" + hb_ntos( nRec ) )
         hb_mutexUnlock( s_hMtx )
         IF Len( s_aErrors ) > 20
            EXIT
         ENDIF
      ENDIF
   NEXT

   // Append summary sizes for the C++ parser
   OutStd( "SIZES dbf=" + hb_ntos( nDbf0 ) + ;
           " cdx=" + hb_ntos( nIdx0 ) + ;
           " recs=" + hb_ntos( RECCOUNT ) + hb_eol() )
   RETURN lOk


STATIC FUNCTION RddName( n, cExt )
   RETURN s_cDir + "mt" + PadL( hb_ntos( n ), 4, "0" ) + cExt

STATIC FUNCTION TableName( n )
   RETURN RddName( n, ".dbf" )

STATIC FUNCTION IndexName( n )
   RETURN RddName( n, ".cdx" )


STATIC FUNCTION WipeFiles()
   LOCAL a, f
   a := Directory( s_cDir + "mt*.*" )
   FOR EACH f IN a
      FErase( s_cDir + f[ 1 ] )
   NEXT
   RETURN NIL


STATIC FUNCTION MakeDirSafe( cDir )
   LOCAL c := cDir
   IF Right( c, 1 ) == hb_ps()
      c := Left( c, Len( c ) - 1 )
   ENDIF
   IF ! hb_DirExists( c )
      MakeDir( c )
   ENDIF
   RETURN NIL
