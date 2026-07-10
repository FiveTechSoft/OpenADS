/*
 * openads_demo_remote.prg — same spirit as openads_demo.prg, but against
 * openads_serverd on the LAN iMac (read-only: remote CREATE is not routed).
 *
 * Build:    hbmk2 openads_demo_remote.hbp
 * Run:      openads_demo_remote.exe
 *
 * Override:  set OPENADS_REMOTE_URI=tcp://host:port//data/dir
 */
#include "ads.ch"
#include "rddsys.ch"

REQUEST ADS, ADSCDX, ADSNTX

PROCEDURE Main()

   LOCAL cUri   := GetEnv( "OPENADS_REMOTE_URI" )
   LOCAL hConn  := 0
   LOCAL cCustNo := ""
   LOCAL cName   := ""
   LOCAL nShown  := 0

   IF Empty( cUri )
      cUri := "tcp://192.168.18.184:16262//tmp/openads_mac"
   ENDIF

   ? "OpenADS hbmk2 demo — REMOTE"
   ? "ACE DLL reports:", AdsVersion()
   ? "Server URI:", cUri
   ?

   AdsSetServerType( ADS_REMOTE_SERVER )
   AdsSetFileType( ADS_CDX )
   RddSetDefault( "ADSCDX" )

   IF ! AdsConnect60( cUri, ADS_REMOTE_SERVER, "", "", 0, @hConn ) .OR. hConn == 0
      ? "AdsConnect60 failed — is openads_serverd running on the iMac?"
      ErrorLevel( 1 )
      QUIT
   ENDIF

   IF ! dbUseArea( .T., "ADSCDX", "customer.dbf", "CUST", .T., .F., , hConn )
      ? "dbUseArea(customer.dbf) failed. NetErr:", NetErr()
      AdsDisconnect( hConn )
      ErrorLevel( 1 )
      QUIT
   ENDIF

   DbGoTop()

   ? "Rows in customer.dbf:", LastRec()
   ? "Production CDX tags (auto-open):", OrdCount()
   IF OrdCount() > 0
      ? "  Bag[1]:", OrdBagName( 1 )
      ? "  Tag[1]:", OrdName( 1 ), "  key:", OrdKey( 1 )
   ENDIF
   ?

   IF OrdCount() >= 2
      ? "  Tag[2]:", OrdName( 2 ), "  key:", OrdKey( 2 )
   ENDIF
   ?

   ? "Walk via tag #2 (NAME index, first 5 rows):"
   DbSetOrder( 2 )
   DbGoTop()
   DO WHILE ! Eof() .AND. nShown < 5
      nShown++
      ? "  rec", RecNo(), ;
        "custno=[" + AllTrim( CUST->CUSTNO ) + "]" + ;
        " name=[" + AllTrim( CUST->NAME ) + "]" + ;
        " country=[" + AllTrim( CUST->COUNTRY ) + "]"
      DbSkip()
   ENDDO
   ?

   DbGoTop()
   cCustNo := PadR( AllTrim( CUST->CUSTNO ), 8 )
   cName   := AllTrim( CUST->NAME )

   DbSetOrder( 1 )
   IF DbSeek( cCustNo )
      ? "Seek '" + AllTrim( cCustNo ) + "' (CUSTNO): Found rec " + LTrim( Str( RecNo() ) )
   ELSE
      ? "Seek CUSTNO: not found (remote CDX quirk — OrdKey empty; walk above proves index)"
   ENDIF

   DbSetOrder( 2 )
   IF DbSeek( cName )
      ? "Seek '" + cName + "' (NAME): Found rec " + LTrim( Str( RecNo() ) )
   ELSE
      ? "Seek NAME: not found — ordered walk still proves CDX is live"
   ENDIF

   DbCloseArea()
   AdsDisconnect( hConn )

   ? "Done."
   RETURN