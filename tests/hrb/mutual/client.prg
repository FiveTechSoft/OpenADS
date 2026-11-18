/* PUBLIC MOCK ONLY. Never use TEST_KEY in production. */
#define TEST_KEY "OpenADS-public-mock-v2-NOT-A-LICENSE-KEY"
PROCEDURE Main(cUrl,cRequestedPath)
 LOCAL hPending:=0,cNonce:="",cBinding:="",cProof,cTranscript,cExpected,cResponse,hConn
 cProof:=OADS_BEGINMUTUAL(cUrl,"fixture-firm",@hPending,@cNonce,@cBinding)
 IF Empty(cProof) .OR. hPending==0
  ErrorLevel(1)
  RETURN
 ENDIF
 cTranscript:="OpenADS-mutual-v2"+PackField(cNonce)+PackField(cBinding)+PackField("fixture-firm")+PackField(cRequestedPath)
 cExpected:="SERVER:"+HB_HMAC_SHA256("server"+Chr(0)+cTranscript,TEST_KEY,.T.)
 IF !EqualBytes(cProof,cExpected)
  OADS_CANCELAUTH(hPending)
  ErrorLevel(2)
  RETURN
 ENDIF
 cResponse:="CLIENT:"+HB_HMAC_SHA256("client"+Chr(0)+cTranscript,TEST_KEY,.T.)
 hConn:=OADS_COMPLETEAUTH(hPending,cResponse)
 IF hConn==0
  ErrorLevel(3)
  RETURN
 ENDIF
 AdsDisconnect(hConn)
 OutStd("MUTUAL-HARBOUR-PASS"+hb_eol())
RETURN
STATIC FUNCTION PackField(c)
 LOCAL n:=Len(c)
RETURN Chr(n%256)+Chr(Int(n/256)%256)+Chr(Int(n/65536)%256)+Chr(Int(n/16777216)%256)+c
STATIC FUNCTION EqualBytes(a,b)
 LOCAL n,nDiff:=0
 IF Len(a)!=Len(b)
  RETURN .F.
 ENDIF
 FOR n:=1 TO Len(a)
  IF SubStr(a,n,1)!=SubStr(b,n,1)
   nDiff++
  ENDIF
 NEXT
RETURN nDiff==0
