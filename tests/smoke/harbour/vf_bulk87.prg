#include "fileio.ch"
PROCEDURE Main(cUri)
 LOCAL h:=VF87Connect(cUri),cDir,cRemote,cLocal:=hb_DirTemp()+hb_ps()+"vf87-local-"+hb_ntos(hb_RandomInt(1,2147483647))+".bin",cBody:=Replicate("0123456789",220000)+Chr(0)+Chr(255),a,cLink,p,cBuf:=Space(10),nEvents:=0
 ErrorBlock({|e| OutStd("ERROR "+e:description+" "+e:operation+hb_eol()),ErrorLevel(1),__Quit()})
 Check(h>0,"connect")
 a:=OAds_vfDirectory(OAds_vfPath(h,"large-directory/*"))
 Check(Len(a)==12000,"directory response greater than one chunk")
 cDir:=OAds_vfPath(h,"bulk87")
 Check(OAds_vfDirMake(cDir)==0,"scratch directory")
 cRemote:=cDir+"/large.bin"
 Check(OAds_vfSave(cRemote,cBody),"chunked save")
 Check(OAds_vfLoad(cRemote)==cBody,"chunked load")
 Check(OAds_vfCopyFileEx(cRemote,cLocal,65536,.T.,{|nDone,nTotal|nEvents++,nDone<=nTotal})==0,"remote to local copy callback")
 Check(nEvents>1.AND.hb_vfLoad(cLocal)==cBody,"local copy body and callbacks")
 Check(hb_vfSave(cLocal,"local to remote"),"local source")
 Check(OAds_vfCopyFileEx(cLocal,cRemote,3,.T.)==0,"local to remote copy")
 Check(OAds_vfLoad(cRemote)=="local to remote","remote result")
 cLink:=cDir+"/link.bin"
 IF OAds_vfLinkSym(cRemote,cLink)==0
   p:=OAds_vfOpen(cLink,FO_READ+FO_DENYNONE)
   Check(p!=NIL,"read through in-root symbolic link")
   Check(OAds_vfRead(p,@cBuf,10)==10.AND.cBuf=="local to r","link content")
   OAds_vfClose(p)
   Check(OAds_vfErase(cLink)==0,"remove link")
 ENDIF
 Check(OAds_vfDirRemove(OAds_vfPath(h,"."))!=0,"jail root cannot be removed")
 Check(!OAds_vfSave(OAds_vfPath(h,"../escape.bin"),"denied"),"save escape refused")
 Check(OAds_vfDirMake(OAds_vfPath(h,"../escape-dir"))!=0,"mkdir escape refused")
 Check(OAds_vfErase(cRemote)==0,"remove remote file")
 Check(OAds_vfDirRemove(cDir)==0,"remove directory")
 Check(hb_vfErase(cLocal)==0,"remove local file")
 VF87Disconnect(h)
 OutStd("VF87-TCP-BULK PASS"+hb_eol())
RETURN
STATIC PROCEDURE Check(lOk,cWhat)
 IF !lOk
  OutStd("FAIL "+cWhat+" err="+hb_ntos(FError())+hb_eol())
  ErrorLevel(1)
  QUIT
 ENDIF
RETURN
