REQUEST DBFCDX
REQUEST ADSCDX

#include "ads.ch"

// Dump tag IDX03 (INS) ordered (recno, INS) pairs from both files.
PROCEDURE Main()
   LOCAL n := 0
   LOCAL h := FCreate( "idx03dump.log" )
   AdsSetFileType( ADS_CDX )
   AdsSetServerType( ADS_LOCAL_SERVER )
   USE T_HB INDEX T_HB ALIAS HBX SHARED NEW VIA "DBFCDX"
   USE T_ADS INDEX T_ADS ALIAS ADX SHARED NEW VIA "ADSCDX"
   HBX->( OrdSetFocus( "IDX03" ) )
   ADX->( OrdSetFocus( "IDX03" ) )
   HBX->( dbGoTop() )
   ADX->( dbGoTop() )
   DO WHILE n < 130
      FWrite( h, hb_ntos( n ) + ": HB rec=" + hb_ntos( HBX->( RecNo() ) ) + ;
         " ins=" + hb_ntos( HBX->( INS ) ) + ;
         " | ADS rec=" + hb_ntos( ADX->( RecNo() ) ) + ;
         " ins=" + hb_ntos( ADX->( INS ) ) + hb_eol() )
      n++
      HBX->( dbSkip() )
      ADX->( dbSkip() )
   ENDDO
   FClose( h )
   HBX->( dbCloseArea() )
   ADX->( dbCloseArea() )
   RETURN
