REQUEST ADS, ADSCDX
PROCEDURE Main()
 LOCAL h := TestStart(), mode, e, caught, defError := ErrorBlock(), callMode
 ErrorBlock( {|err| Break(err)} )
 IF h == 0
  ? "FAIL setup"
  ErrorLevel(1)
  RETURN
 ENDIF
 DbUseArea(.T.,"ADSCDX","CF.DBF","CF",.T.,.F.,NIL,h)
 DbAppend()
 FIELD->NM := "first"
 DbCommit()
 FIELD->NM := "second"
 DbCommit()
 IF AllTrim(FIELD->NM) != "second"
  ? "FAIL second batch"
  ErrorLevel(1)
  RETURN
 ENDIF
 FIELD->NM := "beforeappend"
 DbAppend()
 FIELD->NM := "newrecord"
 DbCommit()
 DbGoto(1)
 IF AllTrim(FIELD->NM) != "beforeappend"
  ? "FAIL append boundary"
  ErrorLevel(1)
  RETURN
 ENDIF
 DbUnlock(); DbCloseArea()
 ? "HARBOUR-SUCCESS-BOUNDARIES-PASS"
 DbUseArea(.T.,"ADSCDX","CF.DBF","CF",.T.,.F.,NIL,h)
 FOR EACH mode IN { 5000, 5063 }
  BEGIN SEQUENCE
   TestCheckResult(mode,32)
  RECOVER USING e
   IF e:canDefault != (mode == 5000)
    ? "FAIL default flag", mode
    ErrorLevel(1)
    RETURN
   ENDIF
   ? "HANDLER-FLAGS-PASS", mode, e:canDefault
  END SEQUENCE
 NEXT
 DbCloseArea()
 FOR EACH mode IN { "commit", "move", "append", "unlock", "close" }
  DbUseArea(.T.,"ADSCDX","CF.DBF","CF",.T.,.F.,NIL,h)
  DbAppend()
  FIELD->NM := "prefix"
  IF ! TestFault()
   ? "FAIL fault setup"
   ErrorLevel(1)
   RETURN
  ENDIF
  caught := .F.
  BEGIN SEQUENCE
   DO CASE
   CASE mode == "commit"
    DbCommit()
   CASE mode == "move"
    DbGoto(1)
   CASE mode == "append"
    DbAppend()
   CASE mode == "unlock"
    DbUnlock()
   CASE mode == "close"
    DbCloseArea()
   ENDCASE
  RECOVER USING e
   caught := .T.
   ? "CAUGHT", mode, e:genCode, e:subCode
  END SEQUENCE
  IF ! caught .OR. ! Used()
   ? "FAIL missing error or lost workarea", mode
   ErrorLevel(1)
   RETURN
  ENDIF
  DbCommit(); DbUnlock(); DbCloseArea()
 NEXT
 FOR EACH mode IN { "skip0-refresh", "samegoto-refresh" }
  DbUseArea(.T.,"ADSCDX","CF.DBF","CF",.T.,.F.,NIL,h)
  DbAppend(); FIELD->NM := "refresh"
  TestArmRefresh(IIf(mode == "skip0-refresh", 0x42, 0x58))
  caught := .F.
  BEGIN SEQUENCE
   IF mode == "skip0-refresh"
    DbSkip(0)
   ELSE
    DbGoto(RecNo())
   ENDIF
  RECOVER USING e
   caught := .T.
   ? "REFRESH-TAIL-CAUGHT", mode, e:subCode
  END SEQUENCE
  IF ! caught
   ? "FAIL refresh-tail error", mode
   ErrorLevel(1)
   RETURN
  ENDIF
  DbCommit(); DbUnlock(); DbCloseArea()
 NEXT
 DbUseArea(.T.,"ADSCDX","CF.DBF","CF",.T.,.F.,NIL,h)
 DbAppend(); FIELD->NM := "5025pending"
 TestArm5025()
 caught := .F.
 BEGIN SEQUENCE
  DbGoto(0)
 RECOVER USING e
  caught := e:subCode == 5025
  ? "DRAIN-5025-CAUGHT", e:genCode, e:subCode
 END SEQUENCE
 IF ! TestRestore5025() .OR. ! caught
  ? "FAIL 5025 drained into Goto compatibility"
  ErrorLevel(1)
  RETURN
 ENDIF
 DbCommit(); DbUnlock(); DbCloseArea()
 DbUseArea(.T.,"ADSCDX","CF.DBF","CF",.T.,.F.,NIL,h)
 TestArmGotoCompat()
 DbGoto(1)
 IF ! TestCheckGotoCompat() .OR. ! Eof()
  ? "FAIL original generic 5025 compatibility"
  ErrorLevel(1)
  RETURN
 ENDIF
 ? "GENERIC-GOTO-5025-COMPAT-PASS"
 DbCloseArea()
 FOR EACH mode IN { "sql-direct", "sql-prepared" }
  DbUseArea(.T.,"ADSCDX","CF.DBF","CF",.T.,.F.,NIL,h)
  DbAppend(); FIELD->NM := "sqlpending"
  IF TestSqlAttach() != 0
   ? "FAIL SQL attach"
   ErrorLevel(1)
   RETURN
  ENDIF
  IF mode == "sql-prepared" .AND. ! AdsPrepareSql("UPDATE CF.DBF SET NM = 'none' WHERE NM = 'absent'")
   ? "FAIL SQL prepare"
   ErrorLevel(1)
   RETURN
  ENDIF
  TestFault()
  caught := .F.
  BEGIN SEQUENCE
   IF mode == "sql-direct"
    AdsExecuteSqlDirect("UPDATE CF.DBF SET NM = 'none' WHERE NM = 'absent'")
   ELSE
    AdsExecuteSql()
   ENDIF
  RECOVER USING e
   caught := e:subCode == 5063
  END SEQUENCE
  IF ! caught .OR. ! TestSqlRetained()
   ? "FAIL SQL close error or lost handles",mode,caught,TestSqlRetained()
   ErrorLevel(1)
   RETURN
  ENDIF
  ? "SQL-CLOSE-FAILURE-RETAINED-PASS",mode
  DbCommit(); DbUnlock(); DbCloseArea()
 NEXT
 DbUseArea(.T.,"ADSCDX","CF.DBF","CF",.T.,.F.,NIL,h)
 IF ! DbAppend() .OR. NetErr()
  ? "FAIL append bool success"
  ErrorLevel(1)
  RETURN
 ENDIF
 APPEND BLANK
 IF NetErr()
  ? "FAIL append command success"
  ErrorLevel(1)
  RETURN
 ENDIF
 DbUnlock()
 ? "APPEND-BOOL-COMMAND-SUCCESS-PASS"
 FOR EACH mode IN { "function", "command" }
  TestAppendFault(1024)
  ErrorBlock( defError )
  caught := .T.
  IF mode == "function"
   caught := ! DbAppend()
  ELSE
   APPEND BLANK
  ENDIF
  ErrorBlock( {|err| Break(err)} )
  IF ! caught .OR. ! NetErr() .OR. TestAppendCalls() != 1
   ? "FAIL append lock boolean/neterr",mode
   ErrorLevel(1)
   RETURN
  ENDIF
  ? "APPEND-LOCK-DEFAULT-PASS",mode,NetErr()
 NEXT
 FOR EACH mode IN { 5037, 5063, 5013, 5000 }
  FOR EACH callMode IN { "function", "command" }
  TestAppendFault(mode)
  caught := .F.
  BEGIN SEQUENCE
   IF callMode == "function"
    DbAppend()
   ELSE
    APPEND BLANK
   ENDIF
  RECOVER USING e
   caught := .T.
   ? "ORDINARY-APPEND-ERROR",callMode,mode,e:genCode,e:subCode,e:canDefault,e:canRetry,NetErr()
  END SEQUENCE
  IF ! caught .OR. TestAppendCalls() != 1
   ? "FAIL append error",mode
   ErrorLevel(1)
   RETURN
  ENDIF
  NEXT
 NEXT
 FOR EACH mode IN { "function", "command" }
  DbAppend(); FIELD->NM := "priorwrite"
  TestFault(); TestAppendFault(0)
  caught := .F.
  BEGIN SEQUENCE
   IF mode == "function"
    DbAppend(.F.)
   ELSE
    APPEND BLANK
   ENDIF
  RECOVER USING e
   caught := e:genCode == 24 .AND. e:subCode == 5063 .AND. ! e:canDefault
  END SEQUENCE
  IF ! caught .OR. TestAppendCalls() != 0 .OR. NetErr()
   ? "FAIL deferred before append",mode
   ErrorLevel(1)
   RETURN
  ENDIF
  ? "DEFERRED-BEFORE-APPEND-PASS",mode
  DbCommit();DbUnlock()
 NEXT
 DbCloseArea()
 TestStop()
 ? "HARBOUR-BATCH-ERROR-PASS"
 RETURN
