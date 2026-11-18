/* PUBLIC TEST FIXTURE KEY. NEVER SHIP THIS HRB AS REAL AUTH. */
#define TEST_KEY "OpenADS-public-mock-v2-NOT-A-LICENSE-KEY"
FUNCTION OADS_AUTH_V1( cNonce,cBlob,cContext )
   HB_SYMBOL_UNUSED(cNonce)
   HB_SYMBOL_UNUSED(cBlob)
   HB_SYMBOL_UNUSED(cContext)
RETURN "OAUTH"+Chr(1)+Chr(0)+Chr(0)+Chr(0)+Chr(0)+Chr(0)
FUNCTION OADS_AUTH_SERVER_V2( cNonce,cTranscript,cContext )
   HB_SYMBOL_UNUSED(cContext)
   IF Len(cNonce)!=32 .OR. Left(cTranscript,Len("OpenADS-mutual-v2"))!="OpenADS-mutual-v2"
      RETURN ""
   ENDIF
RETURN "SERVER:"+HB_HMAC_SHA256("server"+Chr(0)+cTranscript,TEST_KEY,.T.)
FUNCTION OADS_AUTH_VERIFY_V2( cNonce,cTranscript,cProof )
   LOCAL cExpected:="CLIENT:"+HB_HMAC_SHA256("client"+Chr(0)+cTranscript,TEST_KEY,.T.)
   LOCAL nDiff:=0,n
   IF Len(cNonce)!=32 .OR. Len(cProof)!=Len(cExpected)
      RETURN OADS_AUTH_V1()
   ENDIF
   FOR n:=1 TO Len(cExpected)
      IF SubStr(cProof,n,1)!=SubStr(cExpected,n,1)
         nDiff++
      ENDIF
   NEXT
   IF nDiff!=0
      RETURN OADS_AUTH_V1()
   ENDIF
RETURN "OAUTH"+Chr(1)+Chr(1)+Chr(3)+Chr(0)+"usr"+Chr(2)+Chr(0)+"pw"
