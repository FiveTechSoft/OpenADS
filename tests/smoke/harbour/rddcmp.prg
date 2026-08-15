/* rddcmp.prg -- Rdd function conformance comparison:
 * ADSCDX (OpenADS) vs DBFCDX (Harbour reference).
 *
 * Tests every HB_FUNC whose name starts with "Rdd" (case-insensitive)
 * as found in Harbour's src/rdd/dbcmd.c + workarea.c:
 *   RDDLIST, RDDNAME, RDDREGISTER, RDDSETDEFAULT, RDDSYS,
 *   HB_RDDGETTEMPALIAS, HB_RDDINFO, __RDDPREALLOCATE,
 *   DBSETDRIVER (alias of RDDSETDEFAULT)
 *
 * Each battery logs comparable values to a per-RDD transcript
 * (rddcmp_dbfcdx.txt / rddcmp_adscdx.txt); the verdict byte-compares
 * the two transcripts.
 *
 * Build:  hbmk2 -comp=msvc64 -gtcgi -i<harbour>\contrib\rddads -lrddads
 *            -L<openads>\build\default\src\Release -lace64 rddcmp.prg
 * Run:    rddcmp.exe   (needs ace64.dll on PATH or next to the exe)
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

   cD := Memoread( "rddcmp_dbfcdx.txt" )
   cA := Memoread( "rddcmp_adscdx.txt" )
   h := FCreate( "rddcmp_verdict.txt" )
   IF cD == cA
      FWrite( h, "RDDCMP PASS: ADSCDX transcript identical to DBFCDX" + hb_eol() )
      ? "RDDCMP PASS"
   ELSE
      FWrite( h, "RDDCMP FAIL: transcripts differ" + hb_eol() )
      ? "RDDCMP FAIL"
   ENDIF
   FClose( h )
   RETURN

/* -------------------------------------------------- fixtures -------- */

PROCEDURE BuildFixtures()
   LOCAL i

   fErase( "rd_main.dbf" ); fErase( "rd_main.cdx" )
   dbCreate( "rd_main", { { "NAME", "C", 20, 0 }, ;
                          { "NUM",  "N",  4, 0 } }, "DBFCDX" )
   USE rd_main VIA "DBFCDX"
   FOR i := 1 TO 5
      dbAppend()
      FIELD->NAME := PadR( "name" + LTrim( Str( i ) ), 20 )
      FIELD->NUM  := i
   NEXT
   dbCommit()
   USE
   RETURN

/* -------------------------------------------------- driver ---------- */

PROCEDURE RunRDD( c )
   cRDD := c
   fErase( "rddcmp_" + Lower( c ) + ".txt" )
   nLog := FCreate( "rddcmp_" + Lower( c ) + ".txt" )

   IF c == "ADSCDX"
      AdsSetFileType( ADS_CDX )
      AdsSetServerType( ADS_LOCAL_SERVER )
   ENDIF

   BattRddName()
   BattRddSetDefault()
   BattDbSetDriver()
   BattRddList()
   BattRddRegister()
   BattRddSys()
   BattHbRddGetTempAlias()
   BattHbRddInfo()
   BattRddPreallocate()

   dbCloseAll()
   FClose( nLog )
   nLog := -1
   RETURN

/* -------------------------------------------------- batteries ------- */

/* RDDNAME() -- must return the name of the active RDD */
PROCEDURE BattRddName()
   Sect( "rddname" )
   USE rd_main VIA ( cRDD )
   LogIt( "rddname=[" + RddName() + "]" + ;
          " match=" + iif( RddName() == cRDD, "OK", "FAIL:" + RddName() ) )

   /* After opening with DBFCDX, switching order to 0 should still report the same RDD */
   dbSetOrder( 0 )
   LogIt( "ord0 rddname=[" + RddName() + "]" + ;
          " match=" + iif( RddName() == cRDD, "OK", "FAIL:" + RddName() ) )
   USE
   RETURN

/* RDDSETDEFAULT() -- get/set the default RDD driver.
 * Calling with no args returns the current default.
 * Calling with an arg sets it and still returns the previous default.
 * We verify: (a) the returned value is a string, (b) setting + reading
 * round-trips correctly, (c) restoring the original works. */
PROCEDURE BattRddSetDefault()
   LOCAL cOrig, cPrev
   Sect( "rddsetdefault" )

   /* Capture the current default before we touch it */
   cOrig := RDDSETDEFAULT()
   LogIt( "current=[" + cOrig + "]" + ;
          " type=" + ValType( cOrig ) )

   /* Set to DBFCDX, read back */
   cPrev := RDDSETDEFAULT( "DBFCDX" )
   LogIt( "set-dbfcdx returned=[" + cPrev + "]" )
   LogIt( "after-set dbfcdx=[" + RDDSETDEFAULT() + "]" )

   /* Set to ADSCDX, read back */
   cPrev := RDDSETDEFAULT( "ADSCDX" )
   LogIt( "set-adscdx returned=[" + cPrev + "]" )
   LogIt( "after-set adscdx=[" + RDDSETDEFAULT() + "]" )

   /* Restore original */
   RDDSETDEFAULT( cOrig )
   LogIt( "restored=[" + RDDSETDEFAULT() + "]" )
   RETURN

/* RDDLIST() -- returns a list of registered RDD drivers.
 * The exact list depends on what is REQUESTed at build time.
 * We log the count and check that DBFCDX and ADSCDX are present. */
PROCEDURE BattRddList()
   LOCAL aList
   Sect( "rddlist" )

   aList := RDDLIST( 1 )
   LogIt( "count=" + LTrim( Str( Len( aList ) ) ) + ;
          " type=" + ValType( aList ) )
   LogIt( "has-dbfcdx=" + iif( AScan( aList, {|x| Upper( x ) == "DBFCDX" } ) > 0, "Y", "N" ) + ;
          " has-adscdx=" + iif( AScan( aList, {|x| Upper( x ) == "ADSCDX" } ) > 0, "Y", "N" ) + ;
          " has-ads=" + iif( AScan( aList, {|x| Upper( x ) == "ADS" } ) > 0, "Y", "N" ) )

   /* RDDLIST(0) should also work (all drivers) */
   LogIt( "list0-count=" + LTrim( Str( Len( RDDLIST( 0 ) ) ) ) )
   RETURN

/* DBSETDRIVER() -- alias of RDDSETDEFAULT(), get/set the default RDD.
 * Verifies both functions return the same value. */
PROCEDURE BattDbSetDriver()
   LOCAL cRdd, cDrv
   Sect( "dbsetdriver" )

   cRdd := RDDSETDEFAULT()
   cDrv := DBSETDRIVER()
   LogIt( "rddsetdefault=[" + cRdd + "]" + ;
          " dbsetdriver=[" + cDrv + "]" + ;
          " match=" + iif( cRdd == cDrv, "Y", "N" ) )

   /* set via DBSETDRIVER, read back via RDDSETDEFAULT */
   DBSETDRIVER( "DBFCDX" )
   LogIt( "after-set-dbfcdx rdd=[" + RDDSETDEFAULT() + "]" + ;
          " drv=[" + DBSETDRIVER() + "]" + ;
          " match=" + iif( RDDSETDEFAULT() == DBSETDRIVER(), "Y", "N" ) )

   /* restore */
   DBSETDRIVER( cRdd )
   LogIt( "restored=[" + RDDSETDEFAULT() + "]" )
   RETURN

/* RDDSYS() -- empty stub in nulsys.c, does nothing.
 * Just verify it does not crash or raise an error. */
PROCEDURE BattRddSys()
   Sect( "rddsys" )
   RDDSYS()
   LogIt( "rddsys=noop-ok" )
   RETURN

/* RDDREGISTER() -- register an RDD driver by name.
 * DBFCDX and ADSCDX are already registered at startup; calling
 * RDDREGISTER on them should return 1 (already registered).
 * We test the return code. */
PROCEDURE BattRddRegister()
   Sect( "rddregister" )
   LogIt( "dbfcdx=" + LTrim( Str( RDDREGISTER( "DBFCDX", .T. ) ) ) + ;
          " adscdx=" + LTrim( Str( RDDREGISTER( "ADSCDX", .T. ) ) ) )
   RETURN

/* HB_RDDGETTEMPALIAS() -- Harbour extension: generate a temporary alias.
 * The returned alias must be a non-empty string, and two consecutive
 * calls must return different values. */
PROCEDURE BattHbRddGetTempAlias()
   LOCAL c1, c2
   Sect( "hb_rddgettempalias" )

   c1 := HB_RDDGETTEMPALIAS()
   c2 := HB_RDDGETTEMPALIAS()
   LogIt( "alias1=[" + c1 + "]" + ;
          " alias2=[" + c2 + "]" + ;
          " diff=" + iif( c1 != c2, "Y", "N" ) + ;
          " empty=" + iif( Empty( c1 ), "Y", "N" ) )
   RETURN

/* HB_RDDINFO() -- Harbour extension: send an RDDINFO request.
 * RDDINFO_RDDCOUNT (1) returns the number of registered RDDs.
 * RDDINFO RDDCOUNT should match Len( RDDLIST( 1 ) ). */
PROCEDURE BattHbRddInfo()
   LOCAL nInfo := 0
   Sect( "hb_rddinfo" )

   /* RDDINFO_RDDCOUNT = 1 */
   HB_RDDINFO( 1, @nInfo )
   LogIt( "rddcount=" + LTrim( Str( nInfo ) ) + ;
          " vs-list=" + LTrim( Str( Len( RDDLIST( 1 ) ) ) ) + ;
          " match=" + iif( nInfo == Len( RDDLIST( 1 ) ), "Y", "N" ) )

   /* RDDINFO_RDDNAME (2) on the current workarea should match RddName() */
   USE rd_main VIA ( cRDD )
   nInfo := ""
   HB_RDDINFO( 2, @nInfo )
   LogIt( "rddname-info=[" + nInfo + "]" + ;
          " rddname=[" + RddName() + "]" + ;
          " match=" + iif( Upper( nInfo ) == Upper( RddName() ), "Y", "N" ) )
   USE
   RETURN

/* __RDDPREALLOCATE() -- internal: pre-allocate the RDD node pool.
 * Returns the new pool size. Must be > 0 after the call. */
PROCEDURE BattRddPreallocate()
   LOCAL nSize
   Sect( "rddpreallocate" )

   nSize := __RDDPREALLOCATE( 10 )
   LogIt( "prealloc-10=" + LTrim( Str( nSize ) ) + ;
          " positive=" + iif( nSize > 0, "Y", "N" ) )

   nSize := __RDDPREALLOCATE( 50 )
   LogIt( "prealloc-50=" + LTrim( Str( nSize ) ) + ;
          " positive=" + iif( nSize > 0, "Y", "N" ) )
   RETURN

/* -------------------------------------------------- helpers --------- */

PROCEDURE Sect( c )
   LogIt( "-- " + c )
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
