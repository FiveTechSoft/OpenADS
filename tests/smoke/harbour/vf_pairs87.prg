#include "fileio.ch"
PROCEDURE Main(cUri)
 LOCAL h:=VF87Connect(cUri),cLocal:=hb_DirTemp()+hb_ps()+"vf87-pairs-"+hb_ntos(hb_RandomInt(1,2147483647))+".bin",cRemote,m1,m2,extra,a,b,nBad:=0,nCases:=0
 ErrorBlock({|e| OutStd("ERROR "+e:description+" "+e:operation+hb_eol()),ErrorLevel(1),__Quit()})
 IF h<=0
  ErrorLevel(1)
  QUIT
 ENDIF
 cRemote:=OAds_vfPath(h,"vf87-pairs.bin")
 FOR EACH extra IN {0,FO_CREAT+FO_TRUNC}
 FOR EACH m1 IN {0,1,2,3,16,17,18,32,33,34,48,49,50,64,65,66,67}
  FOR EACH m2 IN {0,1,2,3,16,17,18,32,33,34,48,49,50,64,65,66,67}
   a:=Probe(cLocal,m1+extra,m2)
   b:=Probe(cRemote,m1+extra,m2)
   nCases++
   IF hb_ValToExp(a)!=hb_ValToExp(b)
    OutStd("DIFF pair="+hb_ntos(m1+extra)+","+hb_ntos(m2)+" "+hb_ValToExp(a)+" / "+hb_ValToExp(b)+hb_eol())
    nBad++
   ENDIF
  NEXT
 NEXT
 NEXT
 OAds_vfErase(cLocal)
 OAds_vfErase(cRemote)
 VF87Disconnect(h)
 OutStd("VF87-PAIRS cases="+hb_ntos(nCases)+" differences="+hb_ntos(nBad)+hb_eol())
 ErrorLevel(nBad)
RETURN
STATIC FUNCTION Probe(cName,m1,m2)
 LOCAL p,q,a:={},cBuf:=Space(1),n
 OAds_vfSave(cName,"abcdef")
 p:=OAds_vfOpen(cName,m1)
 AAdd(a,{p!=NIL,FError()})
 q:=OAds_vfOpen(cName,m2)
 AAdd(a,{q!=NIL,FError()})
 IF p!=NIL
  n:=OAds_vfReadAt(p,@cBuf,1,0)
  AAdd(a,{n,cBuf,FError()})
  n:=OAds_vfWriteAt(p,"X",1,2)
  AAdd(a,{n,FError()})
 ENDIF
 IF q!=NIL
  cBuf:=Space(1)
  n:=OAds_vfReadAt(q,@cBuf,1,0)
  AAdd(a,{n,cBuf,FError()})
  n:=OAds_vfWriteAt(q,"Y",1,3)
  AAdd(a,{n,FError()})
  OAds_vfClose(q)
 ENDIF
 IF p!=NIL
  OAds_vfClose(p)
 ENDIF
 OAds_vfErase(cName)
RETURN a
