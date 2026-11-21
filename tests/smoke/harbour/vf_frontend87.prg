/* Read-only local Harbour/OAds_vf front-end conformance.
 * Run from the repository root. Only error operation symbol prefixes differ
 * by design because Harbour reports the called OADS_* function name.
 */
#include "fileio.ch"
PROCEDURE Main()
   LOCAL a := NativeProbe(), b := OadsProbe(), nBad := 0, n
   FOR n := 1 TO Len(a)
      IF hb_ValToExp(a[n]) != hb_ValToExp(b[n])
         OutStd("DIFF "+hb_ntos(n)+" "+hb_ValToExp(a[n])+" / "+hb_ValToExp(b[n])+hb_eol())
         nBad++
      ENDIF
   NEXT
   OutStd("VF87-READONLY cases="+hb_ntos(Len(a))+" differences="+hb_ntos(nBad)+hb_eol())
   ErrorLevel(nBad)
RETURN

FUNCTION NativeProbe()
   LOCAL a := {}, p, cBuf, nAttr, ts, oErr
   LOCAL cName := "include/openads/ace.h", cDest
   AAdd(a,{hb_vfIsLocal(cName),hb_vfExists(cName,@cDest),cDest,FError()})
   AAdd(a,{hb_vfNameExists(cName),hb_vfDirExists("include/openads"),FError()})
   AAdd(a,{hb_vfSize(cName),hb_vfSize(cName,.F.),FError()})
   AAdd(a,{hb_vfAttrGet(cName,@nAttr),nAttr,FError()})
   AAdd(a,{hb_vfTimeGet(cName,@ts),ts,FError()})
   AAdd(a,{hb_vfLoad(cName,17),FError()})
   AAdd(a,{Len(hb_vfDirectory("include/openads/ace.h")),hb_vfDirSpace(".",3)>0})
   p := hb_vfOpen(@cName,FO_READ+FO_DENYNONE)
   AAdd(a,{ValType(p),FError()})
   cBuf := Space(5)
   AAdd(a,{hb_vfRead(p,@cBuf,3),cBuf,FError()})
   AAdd(a,{hb_vfSeek(p,0,FS_SET),hb_vfReadLen(p,7),FError()})
   cBuf := Space(5)
   AAdd(a,{hb_vfReadAt(p,@cBuf,4,1),cBuf,hb_vfSeek(p,0,FS_RELATIVE),FError()})
   AAdd(a,{hb_vfEof(p),hb_vfSize(p),FError()})
   AAdd(a,{hb_vfConfig(p,HB_VF_IONAME),hb_vfConfig(p,999,123),ValType(hb_vfHandle(p))})
   AAdd(a,{hb_vfFlush(p,.T.),hb_vfCommit(p),FError()})
   AAdd(a,{hb_vfSeek(p,5368709123,FS_SET),hb_vfSeek(p,0,FS_RELATIVE),FError()})
   AAdd(a,{hb_vfClose(p),FError()})
   BEGIN SEQUENCE WITH {|e| Break(e)}
      hb_vfClose(p)
   RECOVER USING oErr
      AAdd(a,{oErr:genCode,oErr:subCode,StrTran(oErr:operation,"OADS_","HB_")})
   END SEQUENCE
   BEGIN SEQUENCE WITH {|e| Break(e)}
      hb_vfOpen(NIL)
   RECOVER USING oErr
      AAdd(a,{oErr:genCode,oErr:subCode,StrTran(oErr:operation,"OADS_","HB_")})
   END SEQUENCE
RETURN a

FUNCTION OadsProbe()
   LOCAL a := {}, p, cBuf, nAttr, ts, oErr
   LOCAL cName := "include/openads/ace.h", cDest
   AAdd(a,{OAds_vfIsLocal(cName),OAds_vfExists(cName,@cDest),cDest,FError()})
   AAdd(a,{OAds_vfNameExists(cName),OAds_vfDirExists("include/openads"),FError()})
   AAdd(a,{OAds_vfSize(cName),OAds_vfSize(cName,.F.),FError()})
   AAdd(a,{OAds_vfAttrGet(cName,@nAttr),nAttr,FError()})
   AAdd(a,{OAds_vfTimeGet(cName,@ts),ts,FError()})
   AAdd(a,{OAds_vfLoad(cName,17),FError()})
   AAdd(a,{Len(OAds_vfDirectory("include/openads/ace.h")),OAds_vfDirSpace(".",3)>0})
   p := OAds_vfOpen(@cName,FO_READ+FO_DENYNONE)
   AAdd(a,{ValType(p),FError()})
   cBuf := Space(5)
   AAdd(a,{OAds_vfRead(p,@cBuf,3),cBuf,FError()})
   AAdd(a,{OAds_vfSeek(p,0,FS_SET),OAds_vfReadLen(p,7),FError()})
   cBuf := Space(5)
   AAdd(a,{OAds_vfReadAt(p,@cBuf,4,1),cBuf,OAds_vfSeek(p,0,FS_RELATIVE),FError()})
   AAdd(a,{OAds_vfEof(p),OAds_vfSize(p),FError()})
   AAdd(a,{OAds_vfConfig(p,HB_VF_IONAME),OAds_vfConfig(p,999,123),ValType(OAds_vfHandle(p))})
   AAdd(a,{OAds_vfFlush(p,.T.),OAds_vfCommit(p),FError()})
   AAdd(a,{OAds_vfSeek(p,5368709123,FS_SET),OAds_vfSeek(p,0,FS_RELATIVE),FError()})
   AAdd(a,{OAds_vfClose(p),FError()})
   BEGIN SEQUENCE WITH {|e| Break(e)}
      OAds_vfClose(p)
   RECOVER USING oErr
      AAdd(a,{oErr:genCode,oErr:subCode,StrTran(oErr:operation,"OADS_","HB_")})
   END SEQUENCE
   BEGIN SEQUENCE WITH {|e| Break(e)}
      OAds_vfOpen(NIL)
   RECOVER USING oErr
      AAdd(a,{oErr:genCode,oErr:subCode,StrTran(oErr:operation,"OADS_","HB_")})
   END SEQUENCE
RETURN a
