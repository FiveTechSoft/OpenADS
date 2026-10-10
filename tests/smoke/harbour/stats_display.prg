/* Native fixture: hbmk2 stats_display.prg ../../../contrib/oads_hb/oads_stats_display.prg -mt */
PROCEDURE Main()
LOCAL h := {=>}, s, aLines, cLine, cLong := Replicate("label",40)
h["version"] := "1.09.72-mtfix30"
h["workareas"] := {"current"=>298,"max_used"=>1002,"rejected"=>NIL}
h["partial"] := {"current"=>1}
h["max_sessions"] := 0
h["max_sessions_source"] := "environment:OPENADS_SERVER_MAX_SESSIONS"
h["bytes_in"] := 4294967296
h["os_fd_count"] := NIL
h["semantics"] := {"snapshot"=>Replicate("long wording ",20)}
h[cLong] := "label not clipped"
s := OAds_ServerStatsText(h)
IF !(PadL("298",20)+PadL("1002",16)+PadL("Unavailable",16) $ s) .OR. ;
   !(PadL("4294967296",20) $ s) .OR. !("Unlimited (0)" $ s) .OR. ;
   !(PadL("1",20)+PadL("Unavailable",16)+PadL("Unavailable",16) $ s) .OR. ;
   !("label not clipped" $ s) .OR. !(Left(cLong,76) $ s) .OR. ;
   !("Server stats unavailable" $ OAds_ServerStatsText(NIL))
   ErrorLevel(1)
   RETURN
ENDIF
aLines := hb_ATokens(s,hb_eol())
FOR EACH cLine IN aLines
   IF Len(cLine) > 76
      ErrorLevel(2)
      RETURN
   ENDIF
NEXT
? "STATS-DISPLAY-OK"
RETURN
