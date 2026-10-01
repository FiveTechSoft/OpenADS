Built for Pritpal Bedi - bedipritpal/OpenADS, forked from FiveTechSoft/OpenADS.

# v1.09.71-mtfix23 - upstream sync test build

Test build only. Use matching client and server versions and back up binaries and data before testing. This is not a production recommendation. No fork-main merge is included; Tim Stone's exact ECLMST application test remains the gate for that merge.

## Upstream boundary and retained fixes

Merged upstream v1.09.71 at 82ac63c438c155ef8c267f3f2992ca530f9e2908 once into perf/safe-local-answers, preserving both parents in merge 37ceaf51eec6fac69be4395f37c0897b0c05e1ef. Newer upstream main and its macOS compile experiments are excluded.

The merge leaves src/, include/ and the fork's workflows byte-identical to mtfix22. Upstream documentation and release notes were merged. Its ADT exclusive-append regression was added as a separate test rather than replacing the fork's broader distinct-values/shared-guard test.

Retained fork behavior includes case-insensitive LOCAL tag lookup, stock Harbour order-0/empty natural focus, retained scopes and tag direction, REMOTE natural-Skip reanchoring, engine append retry/probes, alias lane pinning, canonical archive/header checks and the Linux glibc 2.31 kit. Vouch application code is untouched.

## Validation

Local Linux Debug -O0/-g0, warnings-as-errors disabled because of inherited warnings. Byte-identical source/include reused mtfix22 build objects after fresh-build resource limits; the new upstream test compiled fresh and core/server/ACE/unit binaries were relinked.

All 1,606 enabled cases were run across four ranges, with 16 disabled cases: 1,604 passed and 2 failed; 628,676 assertions, 7 failed. Both failure classes independently reproduced on the untouched mtfix22 baseline: Exclusive-held OpenIndex returned 6106 rather than 7040, and the REMOTE create/index storm produced a count mismatch. These are known unresolved failures, not a clean full-suite pass.

The normal suite under its existing slow/flaky exclusions passed 1,595 cases and 612,903 assertions in 93 seconds. The initial 90-second CTest wrapper timeout did not indicate an assertion failure. Five other CTest entries passed: slow cases, 30-repeat pool stress, SQL URI, SQLite filter and SQLite seek. Both ADT append regressions passed together: 2 cases, 50,055 assertions.

macOS unit validation remains incomplete from mtfix22: Configure/Build passed but unit-test runs timed out. No macOS unit pass, Tim Windows x64 application pass, or ECLMST-file pass is claimed. Platform packaging results must be checked separately before this draft is published.

## Release integrity

mtfix22's tag-push packaging run silently replaced its reviewed archives and notes. The exact run34 archives and reviewed notes were restored and all five public downloads were verified byte-for-byte. This build adds release guards: mtfix tag pushes do not package or upload; draft staging requires a manual dispatch; a manual dispatch refuses to alter an existing public mtfix release and fails closed if release state cannot be read. Production-tag behavior is unchanged.

## Application check

Repeat Tim's ECLMST report using CDX-format ECLMST.adi (adt_cdx_index=1): select eclcom by name and number, check OrdName/IndexOrd, set order 0 and empty focus and verify physical record navigation. Repeat LOCAL and REMOTE, switch back to a tag, check scopes and append/write maintenance. Record exact handles, recnos and return codes for mismatches. Pritpal does not personally use ADT tables.

Use the full Windows x64/x86, Linux x64/glibc231 or macOS universal client/server kit. Do not mix ACE libraries from different builds.
