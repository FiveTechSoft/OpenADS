/* Mutation conformance uses disposable unique scratch directories.
 * A file remains at the scratch root while DirUnbuild is tested, preventing
 * Harbour's upward empty-directory removal from leaving that test root.
 * Run with oads_vf.c and the same Harbour libraries used by the application.
 */
#include "fileio.ch"
PROCEDURE Main(cUri)
   LOCAL h:=VF87Connect(cUri), a, b, n, nBad:=0, nSeed:=hb_RandomInt(1,2147483647)
   IF h<=0
      OutStd("CONNECT FAILED"+hb_eol())
      ErrorLevel(1)
      QUIT
   ENDIF
   a:=ProbeNative(hb_DirSepAdd(hb_DirTemp())+"vf87-reference-"+hb_ntos(nSeed))
   b:=ProbeOads(OAds_vfPath(h,"vf87-remote-"+hb_ntos(nSeed)))
   a[1][1]:=.F. // A prefixed remote path is intentionally not local.
   FOR n := 1 TO Len( a )
      IF hb_ValToExp( a[n] ) != hb_ValToExp( b[n] )
         OutStd( "DIFF " + hb_ntos(n) + " " + hb_ValToExp(a[n]) + " / " + hb_ValToExp(b[n]) + hb_eol() )
         nBad++
      ENDIF
   NEXT
   OutStd( "VF87-LOCAL cases="+hb_ntos(Len(a))+" differences="+hb_ntos(nBad)+hb_eol() )
   ErrorLevel( nBad )
RETURN

FUNCTION ProbeNative( cDir )
   LOCAL a := {}, p, cName := cDir+"/sample.bin", cCopy := cDir+"/copy.bin"
   LOCAL cBuf, nAttr, ts, cTemp, pTemp, nEvents := 0
   LOCAL bCb := {|nDone,nTotal| nEvents++, nDone <= nTotal }
   ErrorBlock({|e| OutStd(e:description+" "+e:operation+hb_eol()),__Quit()})
   IF hb_vfNameExists(cDir)
      OutStd("Scratch path already exists; refusing to overwrite"+hb_eol())
      ErrorLevel(1)
      QUIT
   ENDIF
   hb_vfDirBuild(cDir)
   hb_vfErase(cName)
   hb_vfErase(cCopy)
   AAdd(a,{hb_vfIsLocal(cName),hb_vfExists(cName),FError()})
   p := hb_vfOpen(@cName,FO_CREAT+FO_TRUNC+FO_READWRITE+FO_DENYNONE)
   AAdd(a,{ValType(p),FError()})
   IF p == NIL
      OutStd("Cannot create disposable vf test file "+cDir+" err="+hb_ntos(FError())+hb_eol())
      ErrorLevel(1)
      QUIT
   ENDIF
   AAdd(a,{hb_vfWrite(p,"abcdef"),FError()})
   AAdd(a,{hb_vfSize(p),hb_vfSeek(p,1,FS_SET),FError()})
   cBuf := Space(3)
   AAdd(a,{hb_vfRead(p,@cBuf,2),cBuf,FError()})
   AAdd(a,{hb_vfWriteAt(p,"XYZ",2,1),hb_vfSeek(p,0,FS_RELATIVE),FError()})
   cBuf := Space(4)
   AAdd(a,{hb_vfReadAt(p,@cBuf,3,0),cBuf,hb_vfSeek(p,0,FS_RELATIVE),FError()})
   AAdd(a,{hb_vfSeek(p,0,FS_SET),hb_vfReadLen(p,9),FError()})
   AAdd(a,{hb_vfEof(p),hb_vfTrunc(p,4),hb_vfSize(p),FError()})
   AAdd(a,{hb_vfFlush(p,.T.),hb_vfCommit(p),FError()})
   AAdd(a,{ValType(hb_vfConfig(p,HB_VF_IONAME)),hb_vfConfig(p,999,123),ValType(hb_vfHandle(p))})
   AAdd(a,{hb_vfLock(p,0,1,HB_FLX_EXCLUSIVE),hb_vfLockTest(p,0,1)>0,hb_vfUnlock(p,0,1),FError()})
   AAdd(a,{hb_vfSeek(p,5368709123,FS_SET),hb_vfSize(p),FError()})
   AAdd(a,{hb_vfClose(p),FError()})
   AAdd(a,{hb_vfSize(cName),hb_vfNameExists(cName),hb_vfDirExists(cDir)})
   AAdd(a,{hb_vfCopyFile(cName,cCopy),hb_vfLoad(cCopy),FError()})
   AAdd(a,{hb_vfCopyFileEx(cName,cCopy,2,.T.,bCb),nEvents,hb_vfLoad(cCopy),FError()})
   AAdd(a,{hb_vfSave(cCopy,"0123456789"),hb_vfLoad(cCopy,3),FError()})
   AAdd(a,{hb_vfAttrGet(cName,@nAttr),nAttr,FError()})
   AAdd(a,{hb_vfAttrSet(cName,nAttr),FError()})
   AAdd(a,{hb_vfTimeSet(cName,hb_SToT("20260102123456.789")),FError()})
   AAdd(a,{hb_vfTimeGet(cName,@ts),ts,FError()})
   AAdd(a,{Len(hb_vfDirectory(cDir+"/*","DHS")),hb_vfDirSpace(cDir,3)>0})
   AAdd(a,{hb_vfLink(cName,cDir+"/hard"),hb_vfLinkSym(cDir+"/sample.bin",cDir+"/sym"),LinkLeaf87(hb_vfLinkRead(cDir+"/sym")),FError()})
   AAdd(a,{hb_vfErase(cDir+"/hard"),hb_vfErase(cDir+"/sym"),FError()})
   AAdd(a,{hb_vfRename(cCopy,cDir+"/renamed"),hb_vfMoveFile(cDir+"/renamed",cCopy),FError()})
   AAdd(a,{hb_vfDirMake(cDir+"/one"),hb_vfDirMake(cDir+"/one"),hb_vfDirRemove(cDir+"/one"),FError()})
   AAdd(a,{hb_vfDirBuild(cDir+"/a/b"),hb_vfDirUnbuild(cDir+"/a/b"),FError()})
   pTemp := hb_vfTempFile(@cTemp,cDir,"tmp",".bin",FC_NORMAL)
   AAdd(a,{ValType(pTemp),Right(cTemp,4),FError()})
   hb_vfClose(pTemp)
   hb_vfErase(cTemp)
   hb_vfErase(cCopy)
   hb_vfErase(cName)
   hb_vfDirRemove(cDir)
RETURN a

FUNCTION ProbeOads( cDir )
   LOCAL a := {}, p, cName := cDir+"/sample.bin", cCopy := cDir+"/copy.bin"
   LOCAL cBuf, nAttr, ts, cTemp, pTemp, nEvents := 0
   LOCAL bCb := {|nDone,nTotal| nEvents++, nDone <= nTotal }
   ErrorBlock({|e| OutStd(e:description+" "+e:operation+hb_eol()),__Quit()})
   IF hb_vfNameExists(cDir)
      OutStd("Scratch path already exists; refusing to overwrite"+hb_eol())
      ErrorLevel(1)
      QUIT
   ENDIF
   hb_vfDirBuild(cDir)
   OAds_vfErase(cName)
   OAds_vfErase(cCopy)
   AAdd(a,{OAds_vfIsLocal(cName),OAds_vfExists(cName),FError()})
   p := OAds_vfOpen(@cName,FO_CREAT+FO_TRUNC+FO_READWRITE+FO_DENYNONE)
   AAdd(a,{ValType(p),FError()})
   IF p == NIL
      OutStd("Cannot create disposable vf test file "+cDir+" err="+hb_ntos(FError())+hb_eol())
      ErrorLevel(1)
      QUIT
   ENDIF
   AAdd(a,{OAds_vfWrite(p,"abcdef"),FError()})
   AAdd(a,{OAds_vfSize(p),OAds_vfSeek(p,1,FS_SET),FError()})
   cBuf := Space(3)
   AAdd(a,{OAds_vfRead(p,@cBuf,2),cBuf,FError()})
   AAdd(a,{OAds_vfWriteAt(p,"XYZ",2,1),OAds_vfSeek(p,0,FS_RELATIVE),FError()})
   cBuf := Space(4)
   AAdd(a,{OAds_vfReadAt(p,@cBuf,3,0),cBuf,OAds_vfSeek(p,0,FS_RELATIVE),FError()})
   AAdd(a,{OAds_vfSeek(p,0,FS_SET),OAds_vfReadLen(p,9),FError()})
   AAdd(a,{OAds_vfEof(p),OAds_vfTrunc(p,4),OAds_vfSize(p),FError()})
   AAdd(a,{OAds_vfFlush(p,.T.),OAds_vfCommit(p),FError()})
   AAdd(a,{ValType(OAds_vfConfig(p,HB_VF_IONAME)),OAds_vfConfig(p,999,123),ValType(OAds_vfHandle(p))})
   AAdd(a,{OAds_vfLock(p,0,1,HB_FLX_EXCLUSIVE),OAds_vfLockTest(p,0,1)>0,OAds_vfUnlock(p,0,1),FError()})
   AAdd(a,{OAds_vfSeek(p,5368709123,FS_SET),OAds_vfSize(p),FError()})
   AAdd(a,{OAds_vfClose(p),FError()})
   AAdd(a,{OAds_vfSize(cName),OAds_vfNameExists(cName),OAds_vfDirExists(cDir)})
   AAdd(a,{OAds_vfCopyFile(cName,cCopy),OAds_vfLoad(cCopy),FError()})
   AAdd(a,{OAds_vfCopyFileEx(cName,cCopy,2,.T.,bCb),nEvents,OAds_vfLoad(cCopy),FError()})
   AAdd(a,{OAds_vfSave(cCopy,"0123456789"),OAds_vfLoad(cCopy,3),FError()})
   AAdd(a,{OAds_vfAttrGet(cName,@nAttr),nAttr,FError()})
   AAdd(a,{OAds_vfAttrSet(cName,nAttr),FError()})
   AAdd(a,{OAds_vfTimeSet(cName,hb_SToT("20260102123456.789")),FError()})
   AAdd(a,{OAds_vfTimeGet(cName,@ts),ts,FError()})
   AAdd(a,{Len(OAds_vfDirectory(cDir+"/*","DHS")),OAds_vfDirSpace(cDir,3)>0})
   AAdd(a,{OAds_vfLink(cName,cDir+"/hard"),OAds_vfLinkSym(cDir+"/sample.bin",cDir+"/sym"),LinkLeaf87(OAds_vfLinkRead(cDir+"/sym")),FError()})
   AAdd(a,{OAds_vfErase(cDir+"/hard"),OAds_vfErase(cDir+"/sym"),FError()})
   AAdd(a,{OAds_vfRename(cCopy,cDir+"/renamed"),OAds_vfMoveFile(cDir+"/renamed",cCopy),FError()})
   AAdd(a,{OAds_vfDirMake(cDir+"/one"),OAds_vfDirMake(cDir+"/one"),OAds_vfDirRemove(cDir+"/one"),FError()})
   AAdd(a,{OAds_vfDirBuild(cDir+"/a/b"),OAds_vfDirUnbuild(cDir+"/a/b"),FError()})
   pTemp := OAds_vfTempFileServer(@cTemp,cDir,"tmp",".bin",FC_NORMAL)
   AAdd(a,{ValType(pTemp),Right(cTemp,4),FError()})
   OAds_vfClose(pTemp)
   OAds_vfErase(cTemp)
   OAds_vfErase(cCopy)
   OAds_vfErase(cName)
   OAds_vfDirRemove(cDir)
RETURN a

// Harbour's DOS-name splitter treats extended Windows \\?\ paths as a
// special form. Compare only the final link target name across scratch roots.
STATIC FUNCTION LinkLeaf87(cName)
RETURN SubStr(cName,Max(RAt("/",cName),RAt("\",cName))+1)
