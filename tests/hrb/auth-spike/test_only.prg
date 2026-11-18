FUNCTION OADS_AUTH_V1( cChallenge, cBlob, cContext )
   IF cBlob == 'hang'
      DO WHILE .T.
      ENDDO
   ENDIF
   IF cBlob == 'malformed'
      RETURN .T.
   ENDIF
   IF cBlob == cChallenge + 'test-only' .AND. Len(cContext) > 0
      RETURN 'OAUTH' + Chr(1) + Chr(1) + Chr(3) + Chr(0) + 'usr' + Chr(2) + Chr(0) + 'pw'
   ENDIF
RETURN 'OAUTH' + Chr(1) + Chr(0) + Replicate(Chr(0),4)
