/* Compile with kit contrib/oads_hb/oads_hb.c and oads_stats_display.prg. */
FUNCTION ShowServerHealthBox(cServerIP,nPort,cAdminUser,cAdminPass,nTop,nLeft,nBottom,nRight)
LOCAL hStats, nError := 0
hStats := OAds_ServerStatsRemote(cServerIP,nPort,cAdminUser,cAdminPass,@nError)
IF hStats == NIL
   RETURN -nError
ENDIF
/* Coordinates are INSIDE your existing box; no border is drawn here. */
RETURN OAds_ServerStatsShow(hStats,nTop,nLeft,nBottom,nRight)
