#include "fileio.ch"
#include "directry.ch"
PROCEDURE Main(cUri,cNativeRoot)
 LOCAL h:=VF87Connect(cUri),cBase,a,b,n,p,aNative
 ErrorBlock({|e| OutStd("ERROR "+e:description+" "+e:operation+hb_eol()),ErrorLevel(1),__Quit()})
 Check(h>0,"connect")
 cBase:=OAds_vfPath(h,"boundary/")
 IF OAds_vfExists(cBase+"links-ready")
  Check(!OAds_vfExists(cBase+"outside-link"),"outside target is not accessible")
  p:=OAds_vfOpen(cBase+"outside-link")
  Check(p==NIL,"outside open fails")
  a:=OAds_vfDirectory(cBase+"*","DHS")
  Check(AScan(a,{|e|e[F_NAME]=="outside-link"})==0,"listing omits outside link metadata")
  n:=AScan(a,{|e|e[F_NAME]=="inside-link"})
  Check(n>0,"listing includes inside link")
  aNative:=hb_vfDirectory(cNativeRoot+"/boundary/inside-link","DHS")
  Check(Len(aNative)==1,"native inside-link metadata fixture")
  Check(a[n][F_SIZE]==aNative[1][F_SIZE],"inside link size matches native platform")
  b:=OAds_vfDirectory(cBase+"inside-link","L")
  Check(Len(b)==1,"explicit L includes inside link")
  Check("L"$b[1][F_ATTR],"explicit L reports link attribute")
  Check(OAds_vfLoad(cBase+"inside-link")=="inside content","inside link remains usable")
 ENDIF
 IF OAds_vfExists(cBase+"fifo-ready")
  p:=OAds_vfOpen(cBase+"fifo",FO_READ+FO_DENYNONE)
  Check(p==NIL,"remote FIFO refused without blocking")
 ENDIF
 Check(!OAds_vfExists(OAds_vfPath(h,"../outside/marker.bin")),"outside explicit path refused")
 Check(!OAds_vfSave("oads18446744073709551617:must-not-exist.bin","denied"),"connection prefix overflow refused")
 Check(!OAds_vfSave("oads0:must-not-exist.bin","denied"),"zero connection prefix refused")
 Check(!OAds_vfSave("oads123invalid:must-not-exist.bin","denied"),"malformed prefix cannot become a local file")
 VF87Disconnect(h)
 OutStd("VF87-TCP-BOUNDARY PASS"+hb_eol())
RETURN
STATIC PROCEDURE Check(lOk,cWhat)
 IF !lOk
  OutStd("FAIL "+cWhat+" err="+hb_ntos(FError())+hb_eol())
  ErrorLevel(1)
  QUIT
 ENDIF
RETURN
