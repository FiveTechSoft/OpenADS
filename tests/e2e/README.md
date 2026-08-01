# End-to-end regression (b_big_e2e)

`b_big_e2e.prg` is the comprehensive end-to-end regression program for the
OpenADS ACE engine, driven through Harbour's real **rddads** RDD — the same
client stack FiveWin applications use. It exists so a release can be
validated end-to-end on **both bitnesses** before it ships: fixed bugs
used to reappear because only the x64 build was ever exercised.

## What it covers (21 sections)

- `AdsVersion()` reports the real build version programmatically
- `INDEX ON FIELD->name` stores the key expression without the `FIELD->`
  qualifier (Harbour rejects it otherwise)
- production-bag auto-open on `USE` leaves no active order
- `dbSetIndex()` activates the first order when none is set
- `dbSetOrder(n)` / `dbSetOrder(0)` → index order / **natural order**
- scope + SET DELETED: walk, KeyCount, KeyNo, EOF, skip-back, GoBottom
  (the Tim Stone phantom-rows scenario)
- index bags with a custom extension (`.Z01`) need an explicit
  `dbSetIndex()` after `USE`
- `dbReindex()` holds every row
- 9 × (append + RLock + replace + commit + unlock) must finish < 5 s
  (the "~10 s to commit 9 records" stall)
- a second shared connection sees the committed rows
- 3 reader threads on a shared connection complete clean

## Build

Requires MSVC 2022, Harbour at `C:\harbour` (with the patched rddads
sources), and OpenADS built at `build\default` (x64) and `build-x86` (x86):

```bat
tests\e2e\build_e2e.bat
```

produces `b_big_e2e64.exe` and `b_big_e2e32.exe` and copies the matching
engine DLL next to them.

## Run

```bat
rem local engine
b_big_e2e64.exe local  C:\OpenADS\tests\e2e\_locdata
b_big_e2e32.exe local  C:\OpenADS\tests\e2e\_locdata

rem remote: start the server first (file functions needed by the PRG)
build\default\tools\serverd\Release\openads_serverd.exe ^
    --port 16299 --data C:\OpenADS\tests\e2e\_srvdata --enable-file-func
b_big_e2e64.exe remote tcp://127.0.0.1:16299/C:/OpenADS/tests/e2e/_srvdata
b_big_e2e32.exe remote tcp://127.0.0.1:16299/C:/OpenADS/tests/e2e/_srvdata
```

Exit code 0 and `E2E RESULT: 21 passed, 0 failed` on all four
combinations is the release gate. Note: the PRG logs via `OutStd`, not
`?` — some 32-bit Harbour console builds swallow QOut output entirely.
