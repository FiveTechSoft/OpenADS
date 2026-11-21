#!/usr/bin/env bash
set -euo pipefail
# hbmk2, Harbour source root, client DLL directory, client import name, output,
# optional existing kit-glue directory. Builds the fs_args88 argument-shape
# conformance probe. Mirrors build_vf87.sh.
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
 "$HBMK" -hblib contrib/oads_hb/oads_hb.c -iinclude/openads -o"$OUT/oadshb"
 GLUE=$OUT
 GLUENAME=oadshb
fi
"$HBMK" tests/smoke/harbour/fs_args88.prg tests/smoke/harbour/vf_transport87.c -L"$GLUE" -l"$GLUENAME" -L"$CLIENT" -l"$IMPORT" -iinclude/openads -mt -gtstd -o"$OUT/fs_args88"
