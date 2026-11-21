/* Uses the caller ADS RDD plus the packaged OpenADS implementation archive. */
#include "ads.ch"
REQUEST ADS
PROCEDURE Main()
   LOCAL hConn
   OAds_SetLogging( .F. )
   AdsSetServerType( ADS_LOCAL_SERVER )
   hConn := AdsConnect( "." )
   IF Empty( hConn )
      ErrorLevel( 1 )
      RETURN
   ENDIF
   AdsDisconnect()
   ? "Harbour static ACE connect/disconnect OK"
RETURN
