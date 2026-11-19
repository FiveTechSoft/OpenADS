#include "ads1.c"
HB_FUNC( TESTCHECKRESULT )
{
   adsCheckedWriteResult( ( ADSAREAP ) hb_rddGetCurrentWorkAreaPointer(),
                          ( UNSIGNED32 ) hb_parni( 1 ), ( HB_ERRCODE ) hb_parni( 2 ) );
}
