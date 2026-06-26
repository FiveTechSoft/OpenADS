/*
 * repro.prg -- minimal isolated reproducers for the CDX divergences found
 * by qamatrix. Each test is self-contained (fresh table) so there is no
 * state cascade. Run on DBFCDX (oracle) and ADSCDX (OpenADS); diff output.
 *
 * Usage: repro.exe <RDD> <logfile>   (RDD = DBFCDX | ADSCDX)
 */
#include "common.ch"
#include "ads.ch"
#include "set.ch"
#include "ord.ch"

REQUEST ADS, ADSCDX
REQUEST DBFCDX, DBFFPT

STATIC s_cLog := ""
STATIC s_cFile := ""
STATIC s_cRdd := ""
STATIC s_cDir := ""
STATIC s_lAds := .F.

PROCEDURE Main( cRdd, cFile )
   DEFAULT cRdd  TO "DBFCDX"
   DEFAULT cFile TO "repro_" + cRdd + ".log"
   s_cRdd  := Upper( AllTrim( cRdd ) )
   s_cFile := cFile
   s_lAds  := ( Left( s_cRdd, 3 ) == "ADS" )
   ErrorBlock( {| e | Trap( e ) } )

   s_cDir := hb_DirTemp() + "oa_repro_" + s_cRdd
   hb_DirCreate( s_cDir )
   IF s_lAds
      AdsSetServerType( ADS_LOCAL_SERVER )
      AdsSetFileType( ADS_CDX )
      AdsConnect( s_cDir )
   ENDIF
   RddSetDefault( s_cRdd )

   Lg( "## RDD=" + s_cRdd )
   Test_A_CondIndex()
   Test_B_FilterOrder()
   Test_C_Scope()
   Test_D_OrderReset()

   IF s_lAds ; AdsDisconnect() ; ENDIF
   Flush()
   RETURN

/* A: conditional index FOR -- key count AND walk must include only ACTIVE. */
STATIC PROCEDURE Test_A_CondIndex()
   Fresh( "ta" )
   OrdCondSet( "ACTIVE", {|| ACTIVE } )
   OrdCreate( hb_FNameExtSet( DbInfo( DBI_FULLPATH ), ".cdx" ), "TC", "AGE", , .F. )
   OrdSetFocus( "TC" )
   Lg( "A cond-FOR: keycount=" + LTrim(Str(OrdKeyCount())) + " walk=[" + Walk() + "]" )
   dbCloseArea()
   RETURN

/* B: SET FILTER while an index order is active -- walk must follow index. */
STATIC PROCEDURE Test_B_FilterOrder()
   Fresh( "tb" )
   OrdCondSet()
   OrdCreate( hb_FNameExtSet( DbInfo( DBI_FULLPATH ), ".cdx" ), "TAGE", "AGE" )
   OrdSetFocus( "TAGE" )
   SET FILTER TO AGE >= 30
   dbGoTop()
   Lg( "B filter+idx: walk=[" + Walk() + "]" )
   SET FILTER TO
   dbCloseArea()
   RETURN

/* C: ordScope range on a numeric index. */
STATIC PROCEDURE Test_C_Scope()
   Fresh( "tc" )
   OrdCondSet()
   OrdCreate( hb_FNameExtSet( DbInfo( DBI_FULLPATH ), ".cdx" ), "TAGE", "AGE" )
   OrdSetFocus( "TAGE" )
   OrdScope( 0, 28 )
   OrdScope( 1, 40 )
   dbGoTop()
   Lg( "C scope[28..40]: walk=[" + Walk() + "]" )
   OrdScope( 0, NIL ) ; OrdScope( 1, NIL )
   dbCloseArea()
   RETURN

/* D: OrdSetFocus(0) must drop to natural (recno) order. */
STATIC PROCEDURE Test_D_OrderReset()
   Fresh( "td" )
   OrdCondSet()
   OrdCreate( hb_FNameExtSet( DbInfo( DBI_FULLPATH ), ".cdx" ), "TAGE", "AGE" )
   OrdSetFocus( "TAGE" )
   OrdSetFocus( 0 )
   dbGoTop()
   Lg( "D after OrdSetFocus(0): ordNum=" + LTrim(Str(OrdNumber())) + ;
       " topRec=" + LTrim(Str(RecNo())) + " walk=[" + Walk() + "]" )
   dbCloseArea()
   RETURN

/* ---- helpers ---- */
STATIC PROCEDURE Fresh( cStem )
   LOCAL cDbf := s_cDir + hb_ps() + cStem + ".dbf"
   LOCAL i
   LOCAL aN := { "Charlie","alice","Bob","dave","Eve","bob","Mary","tom" }
   LOCAL aA := { 30, 25, 40, 22, 35, 28, 45, 31 }
   LOCAL aAct := { .T., .F., .T., .T., .F., .T., .T., .F. }
   IF File( cDbf )
      FErase( cDbf ) ; FErase( hb_FNameExtSet( cDbf, ".cdx" ) )
   ENDIF
   dbCreate( cDbf, { { "NAME","C",20,0 }, { "AGE","N",3,0 }, { "ACTIVE","L",1,0 } }, s_cRdd )
   USE ( cDbf ) VIA s_cRdd NEW EXCLUSIVE
   FOR i := 1 TO Len( aN )
      dbAppend()
      FIELD->NAME := aN[i] ; FIELD->AGE := aA[i] ; FIELD->ACTIVE := aAct[i]
   NEXT
   dbCommit()
   RETURN

STATIC FUNCTION Walk()
   LOCAL c := ""
   dbGoTop()
   DO WHILE ! Eof()
      c += AllTrim( FIELD->NAME ) + "(" + LTrim(Str(FIELD->AGE)) + ") "
      dbSkip()
   ENDDO
   RETURN AllTrim( c )

STATIC PROCEDURE Lg( c )
   s_cLog += c + Chr(10)
   IF ! Empty( s_cFile ) ; MemoWrit( s_cFile, s_cLog ) ; ENDIF
   RETURN

STATIC PROCEDURE Flush()
   MemoWrit( s_cFile, s_cLog )
   RETURN

STATIC FUNCTION Trap( oErr )
   LOCAL d := iif( ValType(oErr)=="O" .AND. ValType(oErr:description)=="C", oErr:description, "?" )
   Lg( "RUNTIME-ERROR: " + d + " sub=" + LTrim(Str(iif(ValType(oErr)=="O".AND.ValType(oErr:subCode)=="N",oErr:subCode,-1))) )
   Flush()
   ErrorLevel( 2 ) ; QUIT
   RETURN .F.
