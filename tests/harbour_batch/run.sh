#!/usr/bin/env bash
# Actual pinned Harbour RDD error-chain test. Test-only call seams do not enter kit.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
HB="$(cd "$1" && pwd)"
BUILD="$(cd "$2" && pwd)"
OUT="$ROOT/output/harbour-batch"
mkdir -p "$OUT"
HBINC="$HB/include"
RDD="$HB/contrib/rddads"
HBBIN="$(find "$HB/bin" -type f \( -name hbmk2 -o -name hbmk2.exe \) | head -1)"
test -n "$HBBIN"
# Linux ACE aliases missing from the compatibility header. Never redefine
# Windows SDK type names (DOUBLE/VOID) before windows.h.
RDD_TYPES=()
if [[ "${OS:-}" != Windows_NT ]];then RDD_TYPES=(-DDOUBLE=double -DWCHAR=UNSIGNED16 -DVOID=void);fi
# No change to real DLL functions: only RDD test objects use these named seams.
for unit in ads1 adsfunc adsmgmnt adsx;do
 source="$RDD/$unit.c"
 [ "$unit" != ads1 ] || source="$ROOT/tests/harbour_batch/ads1-test.c"
 gcc -c "$source" "${RDD_TYPES[@]}" \
  -DAdsAppendRecord=test_AdsAppendRecord -DAdsGotoRecord=test_AdsGotoRecord \
  -DAdsExecuteSQLDirect=test_AdsExecuteSQLDirect -DAdsExecuteSQL=test_AdsExecuteSQL \
  -I"$HBINC" -I"$RDD" -I"$ROOT/include/openads" -o "$OUT/$unit.o"
done
ar rcs "$OUT/librddads.a" "$OUT/ads1.o" "$OUT/adsfunc.o" "$OUT/adsmgmnt.o" "$OUT/adsx.o"
# Internal C++ layouts depend on core options (notably TLS). Reuse exact
# definitions/includes from its real compile command, never infer features.
python - "$BUILD" "$OUT/core-flags.txt" <<'PYFLAGS'
import pathlib,json,shlex,sys
build=pathlib.Path(sys.argv[1]);flags=[]
p=build/'compile_commands.json'
if p.exists():
 rows=json.loads(p.read_text());row=next(r for r in rows if r['file'].replace('\\','/').endswith('/network/server.cpp'))
 args=row.get('arguments') or shlex.split(row['command'])
 i=0
 while i<len(args):
  a=args[i]
  if a in ('-D','-I','-isystem'):
   flags.extend((a,args[i+1]));i+=2;continue
  if a.startswith(('-D','-I')):flags.append(a)
  i+=1
else:
 p=build/'src/CMakeFiles/openads_core.dir/flags.make'
 assert p.exists(),'Missing core compile flags; configure with CMAKE_EXPORT_COMPILE_COMMANDS=ON'
 for line in p.read_text().splitlines():
  if line.startswith(('CXX_DEFINES =','CXX_INCLUDES =')):flags+=shlex.split(line.split('=',1)[1])
assert flags,'No core flags recovered'
pathlib.Path(sys.argv[2]).write_text('\n'.join(flags)+'\n')
PYFLAGS
# Windows Python writes CRLF; Bash mapfile strips LF only.
mapfile -t CORE_FLAGS < <(tr -d '\r' < "$OUT/core-flags.txt")
EXTRA=()
if [ -f "$ROOT/tests/legacy-native-temp.h" ];then EXTRA=(-include "$ROOT/tests/legacy-native-temp.h");fi
g++ -std=c++17 "${CORE_FLAGS[@]}" "${EXTRA[@]}" -c "$ROOT/tests/harbour_batch/integration.cpp" \
 -I"$HBINC" -I"$RDD" -I"$ROOT/src" -I"$ROOT/include" -I"$ROOT/include/openads" -o "$OUT/integration.o"
mapfile -t LIBS < <(find "$BUILD" -name '*.a' ! -name '*dll.a' ! -path '*/CMakeFiles/*' | sort)
LINKS=(-ldflag=-Wl,--start-group)
for lib in "${LIBS[@]}";do LINKS+=("-ldflag=$lib");done
LINKS+=(-ldflag=-Wl,--end-group -lstdc++ -lpthread)
if [[ "${OS:-}" == Windows_NT ]];then LINKS+=(-lws2_32 -lbcrypt -lpsapi);else LINKS+=(-ldl);fi
"$HBBIN" "$ROOT/tests/harbour_batch/integration.prg" "$OUT/integration.o" -L"$OUT" -lrddads "${LINKS[@]}" -mt -gtstd -o"$OUT/harbour-batch"
set +e
"$OUT/harbour-batch" > "$OUT/runtime.log" 2>&1
rc=$?
set -e
cat "$OUT/runtime.log"
test "$rc" = 0
cat "$OUT/runtime.log"
grep -q HARBOUR-BATCH-ERROR-PASS "$OUT/runtime.log"
grep -q 'DEFERRED-BEFORE-APPEND-PASS command' "$OUT/runtime.log"
grep -q 'SQL-CLOSE-FAILURE-RETAINED-PASS sql-prepared' "$OUT/runtime.log"
