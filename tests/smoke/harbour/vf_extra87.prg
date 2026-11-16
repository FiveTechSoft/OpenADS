#include "fileio.ch"
#include "set.ch"
PROCEDURE Main(cUri)
 LOCAL h1:=VF87Connect(cUri),h2:=VF87Connect(cUri),p1,p1b,p2,cName,cBuf:=Space(1),cRoot:="vf87-extra",cOldDefault:=Set(_SET_DEFAULT),cOldPath:=Set(_SET_PATH)
 ErrorBlock({|e| OutStd("ERROR "+e:description+" "+e:operation+hb_eol()),ErrorLevel(1),__Quit()})
 Check(h1>0.AND.h2>0,"connect")
 Check(OAds_vfDirMake(OAds_vfPath(h1,cRoot))==0,"scratch directory")
 cName:=OAds_vfPath(h1,cRoot+"/locks.bin")
 Check(OAds_vfSave(cName,"abcdef"),"save")
 p1:=OAds_vfOpen(cName,FO_READ+FO_DENYNONE)
 Check(OAds_vfLock(p1,0,1),"readonly shared lock")
 p1b:=OAds_vfOpen(cName,FO_READWRITE+FO_DENYNONE)
 Check(p1b!=NIL,"upgrade while locked")
 Check(OAds_vfWriteAt(p1b,"X",1,3)==1,"write through upgraded descriptor")
 p2:=OAds_vfOpen(OAds_vfPath(h2,cRoot+"/locks.bin"),FO_READWRITE+FO_DENYNONE)
 Check(!OAds_vfLock(p2,0,1),"upgrade preserves original range")
 Check(OAds_vfLock(p1b,1,1),"new exclusive range after upgrade")
 Check(!OAds_vfLock(p2,1,1),"new exclusive range conflicts")
#ifdef __PLATFORM__WINDOWS
 Check(OAds_vfUnlock(p1,0,1),"Windows unlock original handle")
 Check(OAds_vfUnlock(p1b,1,1),"Windows unlock second handle")
#else
 Check(OAds_vfUnlock(p1b,0,2),"Unix merged unlock across descriptors")
#endif
 Check(OAds_vfLock(p2,0,2),"other client sees unlock on both descriptors")
 Check(OAds_vfUnlock(p2,0,2),"cleanup range")
 OAds_vfClose(p1)
 OAds_vfClose(p1b)
 OAds_vfClose(p2)
 Set(_SET_DEFAULT,"vf87-missing")
 Set(_SET_PATH,cRoot)
 cName:=OAds_vfPath(h1,"locks.bin")
 p1:=OAds_vfOpen(@cName,FO_READ+FO_DENYNONE+FO_DEFAULTS)
 Check(p1!=NIL,"SET PATH remote lookup")
 Check(cName==OAds_vfPath(h1,cRoot+hb_ps()+"locks.bin"),"byref resolved name")
 Check(OAds_vfRead(p1,@cBuf,1)==1.AND.cBuf=="a","resolved file content")
 OAds_vfClose(p1)
 Set(_SET_DEFAULT,cOldDefault)
 Set(_SET_PATH,cOldPath)
 Check(OAds_vfErase(cName)==0,"cleanup file")
 Check(OAds_vfDirRemove(OAds_vfPath(h1,cRoot))==0,"cleanup directory")
 VF87Disconnect(h1)
 VF87Disconnect(h2)
 OutStd("VF87-TCP-EXTRA PASS"+hb_eol())
RETURN
STATIC PROCEDURE Check(lOk,cWhat)
 IF !lOk
  OutStd("FAIL "+cWhat+" err="+hb_ntos(FError())+hb_eol())
  ErrorLevel(1)
  QUIT
 ENDIF
RETURN
