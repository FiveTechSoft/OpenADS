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
 IF OAds_vfExists(cBase+"directory-links-ready")
  Check(OAds_vfSave(cBase+"inside-dir/owned-alias.bin","alias body"),"save through inside directory link")
  Check(OAds_vfRename(cBase+"inside-dir/owned-alias.bin",cBase+"inside-dir/owned-renamed.bin")==0,"rename through inside directory link")
  Check(OAds_vfLoad(cBase+"owned-renamed.bin")=="alias body","directory alias names same file")
  Check(OAds_vfErase(cBase+"inside-dir/owned-renamed.bin")==0,"erase through inside directory link")
  Check(OAds_vfDirMake(cBase+"inside-dir/owned-folder")==0,"mkdir through inside directory link")
  Check(OAds_vfDirRemove(cBase+"inside-dir/owned-folder")==0,"rmdir through inside directory link")
  Check(!OAds_vfSave(cBase+"outside-dir/owned-denied.bin","denied"),"outside directory link cannot write")
  Check(OAds_vfErase(cBase+"outside-dir/marker.bin")!=0,"outside directory link cannot erase")
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
