#include "hbapi.h"
#include "ace.h"
HB_FUNC(VF87CONNECT){ADSHANDLE h=0;UNSIGNED32 rc=AdsConnect60((UNSIGNED8*)hb_parc(1),ADS_REMOTE_SERVER,0,0,0,&h);hb_retnint(rc?-(HB_MAXINT)rc:h);}
HB_FUNC(VF87DISCONNECT){hb_retnint(AdsDisconnect((ADSHANDLE)hb_parnint(1)));}
HB_FUNC(VF87ERROR){UNSIGNED32 code=0;UNSIGNED16 n=1000;UNSIGNED8 buf[1001]={0};AdsGetLastError(&code,buf,&n);hb_retc((const char*)buf);}
