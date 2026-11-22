/* Test-only numeric construction: avoids Harbour source-literal parser rounding. */
#include "hbapi.h"
#include <math.h>
HB_FUNC( MARKERTESTNUMBER )
{
    switch(hb_parni(1)) {
    case 1: hb_retnd(9007199254740991.0); break;
    case 2: hb_retnd(9007199254740992.0); break;
    case 3: hb_retnd(HUGE_VAL); break;
    case 4: hb_retnd(-HUGE_VAL); break;
    case 5: hb_retnd(NAN); break;
    default: hb_retnd(1.5); break;
    }
}
