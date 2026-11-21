#!/usr/bin/env bash
set -euo pipefail
# hbmk2, Harbour source root, client DLL directory, client import name, output,
# optional existing kit-glue directory. Builds all probes with the kit runtime.
HBMK=$1
HBROOT=$2
CLIENT=$3
IMPORT=$4
OUT=$5
mkdir -p "$OUT"
if [ $# -ge 6 ]; then
 GLUE=$6
 GLUENAME=rddads
else
 "$HBMK" -hblib contrib/oads_hb/oads_vf.c contrib/oads_hb/oads_vf_driver.c contrib/oads_hb/oads_vf_socket.c -iinclude/openads -o"$OUT/oadsvf"
 GLUE=$OUT
 GLUENAME=oadsvf
fi
for name in vf_frontend87 vf_local87 vf_compare87 vf_locks87 vf_extra87 vf_bulk87 vf_flags87 vf_pairs87 vf_boundary87; do
 "$HBMK" "tests/smoke/harbour/$name.prg" tests/smoke/harbour/vf_transport87.c -L"$GLUE" -l"$GLUENAME" -L"$CLIENT" -l"$IMPORT" -iinclude/openads -mt -gtstd -o"$OUT/$name"
done
# Socket code remains a separate archive member; normal users do not acquire
# hbtcpio merely by linking a historical or non-socket OAds entry point.
"$HBMK" -hblib "$HBROOT/contrib/hbtcpio/tcpio.c" -o"$OUT/hbtcpio"
"$HBMK" tests/smoke/harbour/vf_socket_member87.prg -L"$GLUE" -l"$GLUENAME" -L"$OUT" -lhbtcpio -L"$CLIENT" -l"$IMPORT" -mt -gtstd -o"$OUT/vf_socket_member87"
