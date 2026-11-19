#include "openads/ace.h"
#include <stdio.h>
int main(void) {
 if(AdsFlushPendingFields(0)==0)return 1;
 puts("PENDING-FIELD-IMPORT-PASS");
 return 0;
}
