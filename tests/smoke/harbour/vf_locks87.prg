#include "fileio.ch"
PROCEDURE Main(cUri)
 LOCAL h1:=VF87Connect(cUri),h2:=VF87Connect(cUri),p1,p1b,p2,t,lResult:=.F.,cName
 ErrorBlock({|e| OutStd("ERROR "+e:description+" "+e:operation+hb_eol()),ErrorLevel(1),__Quit()})
 Check(h1>0.AND.h2>0,"connect")
 cName:=OAds_vfPath(h1,"locks.bin")
 Check(OAds_vfSave(cName,"abcdef"),"save")
 p1:=OAds_vfOpen(cName,FO_READWRITE+FO_DENYNONE)
 p1b:=OAds_vfOpen(cName,FO_READWRITE+FO_DENYNONE)
 p2:=OAds_vfOpen(OAds_vfPath(h2,"locks.bin"),FO_READWRITE+FO_DENYNONE)
 Check(p1!=NIL.AND.p1b!=NIL.AND.p2!=NIL,"open")
 Check(OAds_vfLock(p1,0,1),"lock1")
 Check(!OAds_vfLock(p2,0,1),"client conflict")
 Check(!OAds_vfLock(p1,0,1,HB_FLX_WAIT),"own WAIT returns")
 OAds_vfClose(p1)
#ifdef __PLATFORM__WINDOWS
 Check(OAds_vfLock(p2,0,1),"Windows close releases its own handle")
 Check(OAds_vfUnlock(p2,0,1),"release Windows probe range")
#else
 Check(!OAds_vfLock(p2,0,1),"Unix first close retains")
#endif
 OAds_vfClose(p1b)
 Check(OAds_vfLock(p2,0,1),"last close releases")
 Check(OAds_vfUnlock(p2,0,1),"unlock2")
 p1:=OAds_vfOpen(cName,FO_READWRITE+FO_DENYNONE)
 Check(OAds_vfLock(p1,1,1),"wait holder")
 t:=hb_threadStart({|| OAds_vfLock(p2,1,1,HB_FLX_WAIT)})
 hb_idleSleep(.15)
 Check(OAds_vfUnlock(p1,1,1),"release wait holder")
 hb_threadJoin(t,@lResult)
 Check(lResult,"wait woke")
 Check(OAds_vfUnlock(p2,1,1),"waiter unlock")
 OAds_vfClose(p1)
 OAds_vfClose(p2)
 Check(OAds_vfErase(cName)==0,"cleanup")
 VF87Disconnect(h1)
 VF87Disconnect(h2)
 OutStd("VF87-TCP-LOCKS PASS"+hb_eol())
RETURN
STATIC PROCEDURE Check(lOk,cWhat)
 IF !lOk
  OutStd("FAIL "+cWhat+" err="+hb_ntos(FError())+hb_eol())
  ErrorLevel(1)
  QUIT
 ENDIF
RETURN
