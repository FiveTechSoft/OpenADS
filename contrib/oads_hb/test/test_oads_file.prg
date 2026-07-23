/*
 * test_oads_file.prg
 *
 * Exercises ALL OADS_*() Harbour wrappers (oads_hb.c):
 *   OADS_FCreate, OADS_FOpen, OADS_FClose, OADS_FWrite, OADS_FRead, OADS_FSeek
 *
 * Usage:
 *   set OPENADS_LIB=C:\OpenADS\dist\import-libs\x64\mingw
 *   hbmk2 test_oads_file.hbp
 *   test_oads_file.exe
 *
 * With remote server:
 *   set OPENADS_TEST_REMOTE=tcp://192.168.18.184:6262//Users/anto/OpenADS/data
 *   test_oads_file.exe
 */

#include "ads.ch"

REQUEST ADS, ADSCDX

PROCEDURE Main()

   LOCAL cDir     := hb_DirTemp() + "oads_hb_test"
   LOCAL cFile    := "oas_test.txt"
   LOCAL hConn    := 0
   LOCAL hFile    := 0
   LOCAL nPassed  := 0
   LOCAL nFailed  := 0
   LOCAL cWrite   := "Hello from OADS_HB!"
   LOCAL cRead    := ""
   LOCAL nRead    := 0
   LOCAL nWritten := 0
   LOCAL nPos     := 0
   LOCAL cRemote  := GetEnv( "OPENADS_TEST_REMOTE" )

   ? "=== OADS_HB File Functions Test ==="
   ?

   IF ! Empty( cRemote )
      ? "--- REMOTE MODE ---"
      ? "URI:", cRemote
      ? "(skipping local dir setup, using remote server data dir)"
      ?
      AdsSetServerType( ADS_REMOTE_SERVER )
      AdsSetFileType( ADS_CDX )
      hConn := AdsConnect( cRemote )
   ELSE
      ? "--- LOCAL MODE ---"
      AdsSetServerType( ADS_LOCAL_SERVER )
      AdsSetFileType( ADS_CDX )

      IF ! hb_DirExists( cDir )
         hb_DirBuild( cDir )
      ENDIF

      hConn := AdsConnect( cDir )
   ENDIF

   IF hConn == 0
      ? "FAIL: AdsConnect() returned 0"
      ErrorLevel( 1 )
      QUIT
   ENDIF
   ? "Connected.  hConn =", hConn
   ?

   /* -- 1. OADS_FCreate -- */
   ? "--- OADS_FCreate ---"
   hFile := OADS_FCreate( hConn, cFile, 0 )
   ? "  hFile =", hFile, iif( hFile > 0, "(OK)", "FAIL" )
   IF hFile > 0
      nPassed++
   ELSE
      nFailed++
   ENDIF

   /* -- 2. OADS_FWrite (simple form) -- */
   ? "--- OADS_FWrite ---"
   nWritten := OADS_FWrite( hFile, cWrite )
   ? "  Bytes written:", nWritten, ;
     iif( nWritten == Len( cWrite ), "(OK)", "FAIL" )
   IF nWritten == Len( cWrite )
      nPassed++
   ELSE
      nFailed++
   ENDIF

   /* -- 3. OADS_FWrite (@ form) -- */
   ? "--- OADS_FWrite (@nWritten) ---"
   nWritten := 0
   OADS_FWrite( hFile, " EXTRA", @nWritten )
   ? "  @nWritten =", nWritten, iif( nWritten == 6, "(OK)", "FAIL" )
   IF nWritten == 6
      nPassed++
   ELSE
      nFailed++
   ENDIF

   /* -- 4. OADS_FClose -- */
   ? "--- OADS_FClose ---"
   OADS_FClose( hFile )
   ? "  Closed OK"
   nPassed++

   /* -- 5. OADS_FOpen (read-only) -- */
   ? "--- OADS_FOpen ---"
   hFile := OADS_FOpen( hConn, cFile, 3 )  /* ADS_READONLY = 3 */
   ? "  hFile =", hFile, iif( hFile > 0, "(OK)", "FAIL" )
   IF hFile > 0
      nPassed++
   ELSE
      nFailed++
   ENDIF

   /* -- 6. OADS_FRead (simple form) -- */
   ? "--- OADS_FRead (simple) ---"
   cRead := OADS_FRead( hFile, 100 )
   ? "  Read:", AllTrim( cRead )
   ? "  Content:", AllTrim( cRead ), ;
     iif( AllTrim( cRead ) == cWrite + " EXTRA", "(OK)", "FAIL" )
   IF AllTrim( cRead ) == cWrite + " EXTRA"
      nPassed++
   ELSE
      nFailed++
   ENDIF

   /* -- 7. OADS_FSeek + re-read -- */
   ? "--- OADS_FSeek (SET, 0) ---"
   nPos := OADS_FSeek( hFile, 0, 0 )
   ? "  Pos:", nPos, iif( nPos == 0, "(OK)", "FAIL" )
   IF nPos == 0
      nPassed++
   ELSE
      nFailed++
   ENDIF

   ? "--- OADS_FSeek (SET, 6) ---"
   nPos := OADS_FSeek( hFile, 6, 0 )
   ? "  Pos:", nPos, iif( nPos == 6, "(OK)", "FAIL" )
   IF nPos == 6
      nPassed++
   ELSE
      nFailed++
   ENDIF

   cRead := OADS_FRead( hFile, 100 )
   ? "  After seek(6):", AllTrim( cRead ), ;
     iif( AllTrim( cRead ) == "from OADS_HB! EXTRA", "(OK)", "FAIL" )
   IF AllTrim( cRead ) == "from OADS_HB! EXTRA"
      nPassed++
   ELSE
      nFailed++
   ENDIF

   ? "--- OADS_FSeek (CUR, 2) ---"
   nPos := OADS_FSeek( hFile, 2, 1 )
   ? "  Pos after CUR+2:", nPos, "(info)"
   nPassed++

   ? "--- OADS_FSeek (END, 0) ---"
   nPos := OADS_FSeek( hFile, 0, 2 )
   ? "  Pos at END:", nPos
   nPassed++

   /* -- 8. OADS_FRead (@ form) -- */
   ? "--- OADS_FRead (@form) ---"
   OADS_FSeek( hFile, 0, 0 )
   cRead := Space( 100 )
   nRead := OADS_FRead( hFile, @cRead, 100 )
   ? "  nRead:", nRead, "  content:", Left( cRead, nRead )
   IF nRead == Len( cWrite ) + 6  /* original + " EXTRA" */
      nPassed++
   ELSE
      nFailed++
   ENDIF

   /* -- 9. OADS_FClose (read handle) -- */
   ? "--- OADS_FClose (read handle) ---"
   OADS_FClose( hFile )
   ? "  Closed OK"
   nPassed++

   /* -- 10. Write on readonly must fail -- */
   ? "--- OADS_FOpen (readonly) + write deny ---"
   hFile := OADS_FOpen( hConn, cFile, 3 )
   IF hFile > 0
      nWritten := OADS_FWrite( hFile, "X", 1 )
      ? "  Write on readonly:", nWritten, ;
        iif( nWritten == 0, "(OK - denied)", "FAIL" )
      IF nWritten == 0
         nPassed++
      ELSE
         nFailed++
      ENDIF
      OADS_FClose( hFile )
   ELSE
      ? "  Could not open readonly"
      nFailed++
   ENDIF

   /* -- Cleanup -- */
   IF Empty( cRemote )
      AdsDisconnect()
      FErase( cDir + hb_ps() + cFile )
      hb_DirDelete( cDir )
   ELSE
      AdsDisconnect()
   ENDIF

   /* -- Summary -- */
   ?
   ? "=== RESULTS ==="
   ? "Passed:", nPassed
   ? "Failed:", nFailed
   ? iif( nFailed == 0, "*** ALL TESTS PASSED ***", "*** FAILURES DETECTED ***" )

   ErrorLevel( nFailed )
   RETURN
