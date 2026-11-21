/* Argument-shape conformance for the OAds_* file family (mtfix88).
 *
 * Rule sheet under test:
 *  - a leading connection handle counts only when it is a LIVE one;
 *  - a dead first slot (0, .F., a stale numeric) means "no handle":
 *    documented parameters start at argument 2, default connection;
 *  - a string first argument IS the first documented parameter;
 *  - extra trailing arguments are always tolerated, never an error,
 *    and are never filled back when the function documents no
 *    by-reference out-parameter;
 *  - the server's own answer decides the result.
 *
 * Run against a live server: fs_args88 auth://... (same URI form as the
 * vf_remote87 probes; VF87Connect lives in vf_transport87.c).
 */
PROCEDURE Main( cUri )
   LOCAL h := VF87Connect( cUri )
   LOCAL nBad := 0, cDir, cName, cCopy, cMissing, cKeep, cTmp2, x, hf
   IF h <= 0
      OutStd( "CONNECT FAILED" + hb_eol() )
      ErrorLevel( 1 )
      QUIT
   ENDIF
   cDir     := "fs88-" + hb_ntos( hb_RandomInt( 1, 2147483647 ) )
   cName    := cDir + "/probe.bin"
   cCopy    := cDir + "/copy.bin"
   cMissing := cDir + "/missing.bin"
   ErrorBlock( {| e | OutStd( e:description + " " + e:operation + hb_eol() ), __Quit() } )

   /* fixture: one directory, one 4-byte file, created the standard way */
   ACheck( OAds_DirMake( h, cDir ),                                  "setup DirMake", @nBad )
   ACheck( OAds_FSave( h, cName, "1234" ),                           "setup FSave", @nBad )

   /* ---- FExist: every call form agrees, existing file ---- */
   ACheck(   OAds_FExist( cName ),                                   "FExist(c)", @nBad )
   ACheck(   OAds_FExist( h, cName ),                                "FExist(h,c)", @nBad )
   x := "KEEP"
   ACheck(   OAds_FExist( h, cName, @x ) .AND. x == "KEEP",          "FExist(h,c,@x) + no undocumented fill", @nBad )
   x := "KEEP"
   ACheck(   OAds_FExist( cName, @x ) .AND. x == "KEEP",             "FExist(c,@x) + no undocumented fill", @nBad )
   ACheck(   OAds_FExist( 0, cName ),                                "FExist(0,c) dead slot", @nBad )
   ACheck(   OAds_FExist( .F., cName, @x ),                          "FExist(.F.,c,@x) dead slot", @nBad )
   ACheck(   OAds_FExist( 424242, cName ),                           "FExist(stale,c) -> default", @nBad )
   ACheck(   OAds_FExist( h ),                                       "FExist(h) no name -> .F. expected", @nBad, .F. )

   /* ---- FExist on a missing file: .F. everywhere, never an error ---- */
   ACheck(   OAds_FExist( cMissing ),                                "FExist(miss c)", @nBad, .F. )
   ACheck(   OAds_FExist( h, cMissing ),                             "FExist(miss h,c)", @nBad, .F. )
   ACheck(   OAds_FExist( h, cMissing, @x ),                         "FExist(miss h,c,@x)", @nBad, .F. )
   ACheck(   OAds_FExist( 0, cMissing, @x ),                         "FExist(miss 0,c,@x)", @nBad, .F. )
   ACheck(   FError() == 0,                                          "FError clean after not-exists", @nBad )

   /* ---- CheckExistence mirrors FExist ---- */
   ACheck(   OAds_CheckExistence( h, cName, @x ),                    "CheckExistence(h,c,@x)", @nBad )
   ACheck(   OAds_CheckExistence( cName, @x ),                       "CheckExistence(c,@x)", @nBad )
   ACheck(   OAds_CheckExistence( .F., cName ),                      "CheckExistence(.F.,c)", @nBad )

   /* ---- metadata getters ---- */
   ACheck(   OAds_GetFileSize( cName ) == 4,                         "GetFileSize(c)", @nBad )
   ACheck(   OAds_GetFileSize( h, cName ) == 4,                      "GetFileSize(h,c)", @nBad )
   ACheck(   OAds_GetFileSize( 0, cName, @x ) == 4,                  "GetFileSize(0,c,@x)", @nBad )
   ACheck(   ! Empty( OAds_GetFileDate( h, cName ) ),                "GetFileDate(h,c)", @nBad )
   ACheck(   ! Empty( OAds_GetFileDate( cName ) ),                   "GetFileDate(c)", @nBad )
   ACheck(   ! Empty( OAds_GetFileTime( h, cName, @x ) ),            "GetFileTime(h,c,@x)", @nBad )

   /* ---- FLoad / FSave ---- */
   ACheck(   OAds_FLoad( cName ) == "1234",                          "FLoad(c)", @nBad )
   ACheck(   OAds_FLoad( h, cName ) == "1234",                       "FLoad(h,c)", @nBad )
   ACheck(   OAds_FSave( 0, cCopy, "ab" ),                           "FSave(0,c,data) dead slot", @nBad )
   ACheck(   OAds_FLoad( h, cCopy ) == "ab",                         "FLoad of dead-slot FSave", @nBad )

   /* ---- CopyFile / RenameFile / DeleteFile ---- */
   ACheck(   OAds_CopyFile( cName, cCopy ),                          "CopyFile(a,b)", @nBad )
   ACheck(   OAds_CopyFile( h, cName, cCopy, @x ),                   "CopyFile(h,a,b,@x)", @nBad )
   ACheck(   OAds_DeleteFile( 0, cCopy ),                            "DeleteFile(0,c)", @nBad )
   ACheck(   OAds_CopyFile( h, cName, cCopy ),                       "CopyFile again", @nBad )
   ACheck(   OAds_RenameFile( cCopy, cCopy + ".r" ),                 "RenameFile(a,b)", @nBad )
   ACheck(   OAds_DeleteFile( h, cCopy + ".r", @x ),                 "DeleteFile(h,c,@x)", @nBad )

   /* ---- directories ---- */
   ACheck(   OAds_DirExist( cDir ),                                  "DirExist(c)", @nBad )
   ACheck(   OAds_DirExist( h, cDir ),                               "DirExist(h,c)", @nBad )
   ACheck(   OAds_DirExist( .F., cDir, @x ),                         "DirExist(.F.,c,@x)", @nBad )
   ACheck(   OAds_DirMake( 0, cDir + "/sub" ),                       "DirMake(0,c)", @nBad )
   ACheck(   OAds_DirRemove( h, cDir + "/sub" ),                     "DirRemove(h,c)", @nBad )
   ACheck(   ValType( OAds_DirSpace( cDir ) ) == "N",                "DirSpace(c)", @nBad )
   ACheck(   ValType( OAds_DirSpace( h, cDir, 1, @x ) ) == "N",      "DirSpace(h,c,1,@x)", @nBad )
   ACheck(   ValType( OAds_Directory( cDir + "/*" ) ) == "A",        "Directory(mask)", @nBad )
   ACheck(   ValType( OAds_Directory( h, cDir + "/*" ) ) == "A",     "Directory(h,mask)", @nBad )

   /* ---- TempFile / SetFileAttr / SetFileTime ---- */
   cKeep := OAds_TempFile( h, cDir, "t88" )
   ACheck(   ! Empty( cKeep ),                                       "TempFile(h,dir,pfx)", @nBad )
   cTmp2 := OAds_TempFile( cDir, "t88", @x )
   ACheck(   ! Empty( cTmp2 ),                                       "TempFile(dir,pfx,@x)", @nBad )
   ACheck(   OAds_SetFileAttr( h, cName, 0 ),                        "SetFileAttr(h,c,0)", @nBad )
   ACheck(   OAds_SetFileAttr( cName, 0, @x ),                       "SetFileAttr(c,0,@x)", @nBad )
   ACheck(   OAds_SetFileTime( h, cName, Date(), "01:02:03" ),       "SetFileTime(h,c,d,t)", @nBad )
   ACheck(   OAds_SetFileTime( cName, Date(), "01:02:03" ),          "SetFileTime(c,d,t)", @nBad )

   /* ---- FOpen / FCreate return real handles in every form ---- */
   hf := OAds_FOpen( cName, 0 )
   ACheck(   hf > 0,                                                 "FOpen(c,0)", @nBad )
   OAds_FClose( hf )
   hf := OAds_FOpen( h, cName, 0, @x )
   ACheck(   hf > 0,                                                 "FOpen(h,c,0,@x)", @nBad )
   OAds_FClose( hf )
   hf := OAds_FOpen( 0, cName, 0 )
   ACheck(   hf > 0,                                                 "FOpen(0,c,0)", @nBad )
   OAds_FClose( hf )

   /* ---- mutex family ---- */
   ACheck(   OAds_MutexCreate( cDir + "/m88" ),                      "MutexCreate(c)", @nBad )
   ACheck(   OAds_MutexTryLock( h, cDir + "/m88" ),                  "MutexTryLock(h,c)", @nBad )
   ACheck(   OAds_MutexUnlock( 0, cDir + "/m88", @x ),               "MutexUnlock(0,c,@x)", @nBad )
   ACheck(   OAds_MutexDestroy( cDir + "/m88" ),                     "MutexDestroy(c)", @nBad )

   /* ---- ServerVersion: 0/1-arg, dead slot ---- */
   ACheck(   ! Empty( OAds_ServerVersion() ),                        "ServerVersion()", @nBad )
   ACheck(   ! Empty( OAds_ServerVersion( h ) ),                     "ServerVersion(h)", @nBad )
   ACheck(   ! Empty( OAds_ServerVersion( 0 ) ),                     "ServerVersion(0)", @nBad )

   /* ---- cleanup ---- */
   OAds_DeleteFile( h, cName )
   OAds_DeleteFile( h, cKeep )
   OAds_DeleteFile( h, cTmp2 )
   OAds_DirRemove( h, cDir )
   VF87Disconnect( h )

   OutStd( "FSARGS88 failures=" + hb_ntos( nBad ) + hb_eol() )
   ErrorLevel( nBad )
RETURN

STATIC PROCEDURE ACheck( lCond, cTag, nBad, lExpect )
   hb_default( @lExpect, .T. )
   IF lCond != lExpect
      OutStd( "FAIL " + cTag + hb_eol() )
      nBad++
   ENDIF
RETURN
