#!/usr/bin/env bash
# Run within the matching MSYS2 MinGW environment, from repository root.
set -euo pipefail
archive="$1"
bits="$2"
case "$bits" in 32) comp=mingw;; 64) comp=mingw64;; *) exit 2;; esac
archive="$(realpath "$archive")"
root="$(pwd)"
HB="$RUNNER_TEMP/harbour-static-$bits"
ref=4ec2e154ed92757418bc30af571ab90240ab3209
if [ ! -d "$HB/src/.git" ]; then
  mkdir -p "$HB"
  git clone --depth 1 https://github.com/harbour/core.git "$HB/src"
fi
git -C "$HB/src" fetch --depth 1 origin "$ref"
git -C "$HB/src" checkout --detach "$ref"
make -C "$HB/src" -j"$(nproc)" install HB_PLATFORM=win HB_COMPILER="$comp" \
  HB_INSTALL_PREFIX="$HB/install" HB_BUILD_CONTRIBS=no HB_BUILD_3RDEXT=no
hbmk="$(find "$HB/install" -iname hbmk2.exe -type f | head -1)"
test -n "$hbmk"
work="$RUNNER_TEMP/harbour-static-smoke-$bits"
mkdir -p "$work"
# Explicit stock RDD objects avoid rddads.hbc's automatic DLL import libs.
for unit in ads1 adsfunc adsmgmnt adsx; do
  gcc -c "$HB/src/contrib/rddads/$unit.c" -I"$HB/src/include" \
    -I"$HB/src/contrib/rddads" -I"$root/include/openads" -o "$work/$unit.o"
done
# Package runtime and Harbour wrappers once, using this exact compiler.
# Keep the raw CMake archive for native C++ smoke tests; publish the kit archive.
runtime_root="$(dirname "$(dirname "$(command -v gcc)")")"
kit="$root/build/static-kit-$bits"
mkdir -p "$kit"
python tools/scripts/package_static_client.py --profile matching \
  --engine "$(cygpath -m "$archive")" \
  --runtime-root "$(cygpath -m "$runtime_root")" \
  --link-root "$(cygpath -m "$runtime_root")" \
  --harbour-include "$(cygpath -m "$HB/src/include")" \
  --ace-include "$(cygpath -m "$root/include/openads")" \
  --glue "$(cygpath -m "$root/contrib/oads_hb/oads_hb.c")" \
  --output "$(cygpath -m "$kit/libopenace$bits.a")"
"$hbmk" "$root/tests/smoke/harbour_static_ace.prg" \
  "$work/ads1.o" "$work/adsfunc.o" "$work/adsmgmnt.o" "$work/adsx.o" \
  "$kit/libopenace$bits.a" \
  -comp="$comp" -I"$root/include/openads" -I"$HB/src/contrib/rddads" \
  -lws2_32 -lpsapi -ldflag=-static -o"$work/harbour-static-smoke.exe"
objdump -p "$work/harbour-static-smoke.exe" > "$work/imports.txt"
if grep -Ei 'DLL Name:.*(openace[0-9]*|ace32|ace64|libstdc\+\+.*|libgcc.*|libwinpthread.*)\.dll' "$work/imports.txt"; then
  echo 'Harbour static smoke unexpectedly imports an ACE DLL' >&2
  exit 1
fi
cd "$work"
./harbour-static-smoke.exe
