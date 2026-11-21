PROCEDURE Main()
 LOCAL a:=Probe(.F.),b:=Probe(.T.)
 IF hb_ValToExp(a)!=hb_ValToExp(b)
  OutStd("FAIL socket alias "+hb_ValToExp(a)+" / "+hb_ValToExp(b)+hb_eol())
  ErrorLevel(1)
 ELSE
  OutStd("VF87-SOCKET-MEMBER PASS"+hb_eol())
 ENDIF
RETURN
STATIC FUNCTION Probe(lOads)
 LOCAL e,p,a
 BEGIN SEQUENCE WITH {|error|Break(error)}
  IF lOads
   p:=OAds_vfFromSocket(NIL)
  ELSE
   p:=hb_vfFromSocket(NIL)
  ENDIF
  a:={ValType(p)}
 RECOVER USING e
  a:={e:genCode,e:subCode,StrTran(e:operation,"OADS_","HB_")}
 END SEQUENCE
RETURN a
