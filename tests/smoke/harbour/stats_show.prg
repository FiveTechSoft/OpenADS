/* hbmk2 stats_show.prg ../../../contrib/oads_hb/oads_stats_display.prg -gtstd -mt */
#include "hbgtinfo.ch"
#ifdef STATS_SHOW_CTW
REQUEST HB_GT_CTW
#endif
PROCEDURE Main()
LOCAL h := {"version"=>"1.09.72-mtfix31", ;
            "workareas"=>{"current"=>9,"max_used"=>1002,"rejected"=>NIL}, ;
            "max_sessions"=>500,"max_sessions_source"=>"default", ;
            "os_fd_count"=>64}, n, r, c, after, cell
LOCAL nR := 2, nC := 4, color := "W+/B", text := ""
SetMode(25,100)
SetColor("W/N")
FOR r := 0 TO MaxRow()
   DispOutAt(r,0,Replicate(".",MaxCol()+1))
NEXT
SetColor(color)
SetPos(nR,nC)
n := OAds_ServerStatsShow(h,4,10,16,85)
IF n != 7 .OR. Row()!=nR .OR. Col()!=nC .OR. SetColor()!=color
   Fail("count/cursor/color")
ENDIF
FOR r := 0 TO MaxRow()
   FOR c := 0 TO MaxCol()
      cell := TestCell(r,c)
      IF (r<4 .OR. r>16 .OR. c<10 .OR. c>85) .AND. cell[2] != Asc(".")
         Fail("outside rectangle changed")
      ENDIF
      IF r>=4 .AND. r<=16 .AND. c>=10 .AND. c<=85 .AND. cell[1]!=31
         Fail("color changed")
      ENDIF
   NEXT
NEXT
IF TestCell(4,10)[2]!=Asc("S") .OR. TestCell(7,53)[2]!=Asc("9")
   Fail("coordinates/content")
ENDIF
/* Export the actual screen cells as text for visual review. */
FOR r := 0 TO MaxRow()
   FOR c := 0 TO MaxCol()
      text += Chr(TestCell(r,c)[2])
   NEXT
   text += hb_eol()
NEXT
hb_MemoWrit("stats_show_screen.txt",text)
/* A short narrow rectangle clips horizontally and vertically without scroll. */
n := OAds_ServerStatsShow(h,20,90,21,99)
IF n!=2 .OR. TestCell(20,89)[2]!=Asc(".") .OR. TestCell(22,90)[2]!=Asc(".")
   Fail("narrow clipping")
ENDIF
/* Last-row default rectangle, invalid input and empty/NIL input. */
SetPos(MaxRow(),0)
n := OAds_ServerStatsShow(NIL)
IF n!=1 .OR. Row()!=MaxRow() .OR. Col()!=0
   Fail("default rectangle")
ENDIF
after := SaveScreen(0,0,MaxRow(),MaxCol())
IF OAds_ServerStatsShow(h,-1,0,5,10)!=0 .OR. ;
   OAds_ServerStatsShow(h,10,10,5,5)!=0 .OR. ;
   OAds_ServerStatsShow(h,"bad",0,5,5)!=0 .OR. ;
   SaveScreen(0,0,MaxRow(),MaxCol())!=after
   Fail("invalid input changed screen")
ENDIF
#ifdef STATS_SHOW_CTW
/* Current CT window uses local coordinates, not root-screen offsets. */
WSetShadow(0)
n := WOpen(3,12,18,91,.T.)
IF n <= 0
   Fail("window open")
ENDIF
SetColor(color)
SetPos(2,3)
n := OAds_ServerStatsShow(h,1,1,12,76)
IF n!=7 .OR. Row()!=2 .OR. Col()!=3 .OR. ;
   TestCell(1,1)[2]!=Asc("S") .OR. TestCell(4,44)[2]!=Asc("9")
   Fail("window-local coordinates")
ENDIF
WClose()
#endif
OutStd("STATS-SHOW-OK"+hb_eol())
RETURN
STATIC PROCEDURE Fail(cWhy)
OutStd("STATS-SHOW-FAIL: "+cWhy+hb_eol())
ErrorLevel(1)
QUIT
RETURN
#pragma BEGINDUMP
#include "hbapi.h"
#include "hbapiitm.h"
#include "hbapigt.h"
HB_FUNC( TESTCELL )
{
   int color=0; HB_BYTE attr=0; HB_USHORT ch=0;
   PHB_ITEM a=hb_itemArrayNew(2);
   hb_gtGetChar(hb_parni(1),hb_parni(2),&color,&attr,&ch);
   hb_arraySetNI(a,1,color); hb_arraySetNI(a,2,ch);
   hb_itemReturnRelease(a);
}
#pragma ENDDUMP
