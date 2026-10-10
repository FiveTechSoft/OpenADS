PROCEDURE Main( cHost, cPort, cUser, cPass )
LOCAL nError := 0, h, aThreads := {}, i, x
h := OAds_ServerStatsRemote( cHost, Val(cPort), cUser, cPass, @nError )
IF h == NIL .OR. nError != 0 .OR. h["scope"] != "server"
   ErrorLevel(1)
   RETURN
ENDIF
FOR i := 1 TO 4
   AAdd( aThreads, hb_threadStart( {|| Query( cHost, Val(cPort), cUser, cPass )} ) )
NEXT
FOR EACH x IN aThreads
   hb_threadJoin( x, @h )
   IF !h
      ErrorLevel(2)
      RETURN
   ENDIF
NEXT
h := OAds_ServerStatsRemote( cHost, 0, cUser, cPass, @nError )
IF h != NIL .OR. nError == 0
   ErrorLevel(3)
   RETURN
ENDIF
h := OAds_ServerStatsRemote( "tcp://" + cHost, Val(cPort), cUser, cPass, @nError )
IF h != NIL .OR. nError == 0
   ErrorLevel(4)
   RETURN
ENDIF
h := OAds_ServerStatsRemote( cHost, Val(cPort), cUser, "wrong-test-password", @nError )
IF h != NIL .OR. nError == 0
   ErrorLevel(5)
   RETURN
ENDIF
? "REMOTE-HEALTH-WRAPPER-OK"
RETURN
STATIC FUNCTION Query(cHost,nPort,cUser,cPass)
LOCAL h, n := 0
h := OAds_ServerStatsRemote(cHost,nPort,cUser,cPass,@n)
RETURN h != NIL .AND. n == 0
