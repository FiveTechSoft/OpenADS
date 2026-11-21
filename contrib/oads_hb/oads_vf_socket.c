/* The optional socket-backed vf front end lives in its own archive member.
 * Like Harbour's hb_vfFromSocket(), this function requires hbtcpio.
 * Its native socket is consumed by Harbour, not sent to an OpenADS server.
 */
#include "hbapi.h"

HB_FUNC_TRANSLATE( OADS_VFFROMSOCKET, HB_VFFROMSOCKET )
