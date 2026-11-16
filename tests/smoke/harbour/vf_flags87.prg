#include "fileio.ch"
PROCEDURE Main(cUri)
 LOCAL h:=VF87Connect(cUri),cLocal:=hb_DirTemp()+hb_ps()+"vf87-flags-"+hb_ntos(hb_RandomInt(1,2147483647))+".bin",cRemote,extra,mode,exists,a,b,nBad:=0,nCases:=0
 ErrorBlock({|e| OutStd("ERROR "+e:description+" "+e:operation+hb_eol()),ErrorLevel(1),__Quit()})
 IF h<=0
  ErrorLevel(1)
  QUIT
 ENDIF
 cRemote:=OAds_vfPath(h,"vf87-flags.bin")
 FOR EACH extra IN {0,FO_CREAT,FO_CREAT+FO_TRUNC,FO_CREAT+FO_EXCL}
  FOR mode:=0 TO 127
   FOR EACH exists IN {.F.,.T.}
    a:=Probe(cLocal,mode+extra,exists)
    b:=Probe(cRemote,mode+extra,exists)
    nCases++
    IF hb_ValToExp(a)!=hb_ValToExp(b)
      OutStd("DIFF flags="+hb_ntos(mode+extra)+" exists="+hb_ValToExp(exists)+" "+hb_ValToExp(a)+" / "+hb_ValToExp(b)+hb_eol())
      nBad++
    ENDIF
   NEXT
  NEXT
 NEXT
 OAds_vfErase(cLocal)
 OAds_vfErase(cRemote)
 VF87Disconnect(h)
 OutStd("VF87-FLAGS cases="+hb_ntos(nCases)+" differences="+hb_ntos(nBad)+hb_eol())
 ErrorLevel(nBad)
RETURN
STATIC FUNCTION Probe(cName,nMode,lExists)
 LOCAL p,a:={},cBuffer:=Space(1),n
 IF lExists
  OAds_vfSave(cName,"abcdef")
 ELSE
  OAds_vfErase(cName)
 ENDIF
 p:=OAds_vfOpen(cName,nMode)
 AAdd(a,{p!=NIL,FError()})
 IF p!=NIL
  n:=OAds_vfRead(p,@cBuffer,1)
  AAdd(a,{n,cBuffer,FError()})
  n:=OAds_vfWriteAt(p,"X",1,2)
  AAdd(a,{n,FError()})
  n:=OAds_vfSize(p)
  AAdd(a,{n,FError()})
  OAds_vfClose(p)
 ENDIF
 OAds_vfErase(cName)
RETURN a
