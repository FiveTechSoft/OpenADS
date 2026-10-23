# OpenADS 1.09.75 - pre-release candidate

This candidate carries the MLS 2026 patch series by Tim Stone on top of 1.09.74 (f41738d3), plus follow-up fixes ported from Pritpal Bedi's fork: six commits, 14 files, +506/-15, merged as f9084018397eb077892f01bef5e54056879495d4. It is published as a pre-release: the full source CI is green, but the complete build has not yet been validated against the real MLS 2026 application. Treat it as a testing candidate, not a stable release.

Thanks to Tim Stone (MasterLink Software) for the patch series, the MSVC x86/x64 builds of its base level and the build notes, and to Pritpal Bedi for the follow-up fixes on his fork that address further items from those notes.

## Database and index behavior

- SetAOF/ClearAOF now mirror onto the ABI twin handle, including a twin created after the AOF was set. Filtered, indexed browses under REMOTE no longer show wrong or partial rows.
- A non-optimisable AOF is refused with AE_INVALID_EXPRESSION, as local AdsSetAOF does, so rddads falls back to client-side filtering instead of silently returning unfiltered rows.
- goto_record re-anchors the sequence index, so DbGoto(n) + DbSkip(-1) under an AOF no longer lands on BOF. This affected LOCAL too.
- AdsSkip anchors the server cursor after a pair-certified GoTop/GoBottom, so the next indexed Skip walks from the correct server row.
- ADT numeric fields with decimals are created as ADT numeric (type 2), keeping the declared width and scale, instead of DOUBLE with 0 decimals. Values such as 7.75 no longer display rounded.
- Text writes to DBF N/F fields are stored right-aligned at the declared scale (standard dBase/ADS format) instead of verbatim left-aligned; values rounding to zero print as 0, not -0.00.
- DbCreate() in ADT mode without an extension now names the .adt file, and a SQL CREATE TABLE whose name ends in .adt defaults to ADT unless the caller set an explicit statement type.

## Platform

- The server data jail now compares path prefixes case-insensitively on Windows, and resolve_under_root('.') keeps the root's own spelling. A client path accepted by the MSVC server is no longer refused for case reasons.

## Reported open items (from Tim Stone's build notes, not independently verified here)

- His notes report that IndexKey(n) / OrdName(n) past the last tag return tag 1 instead of "", and warn that code walking tags with `while ! Empty( IndexKey( j ) )` can loop until it runs out of memory; he recommends bounding such loops with OrdCount().
- His notes report one stale value observed on a read immediately after DbCommit() + DbUnlock() under REMOTE (client row cache suspected), not reproduced since.
- His notes report that Winlibs GCC 10.2 builds of the patch line misbehaved when their DLL was loaded into an MSVC-built Harbour application: case-sensitive path-jail refusals (error 7079) and numeric fields formatting as stars, which corrupted DBF numerics on save in his environment. The Windows case-insensitive jail fix in this release addresses the path comparison; the stars report stands as his observation of those GCC kits, not of this exact build, which he has not yet tested as a whole. Until this candidate passes his application testing, use the MSVC-built Windows binaries from this package with MSVC-built applications and do not mix in a MinGW/GCC-built DLL.

## Packages and installation

The release workflow builds four packages from the tag:

- openads-1.09.75-windows-x86.zip
- openads-1.09.75-windows-x64.zip
- openads-1.09.75-linux-x64.tar.gz
- openads-1.09.75-macos-universal.tar.gz

The publish gate refuses to create this release unless every build leg is green and every archive passes integrity, content and version-stamp checks. The macOS universal package returns in this candidate (CI-built and CI-tested only; no application testing on macOS). The older-Linux glibc2.31 compatibility package is not part of this candidate; the standard Linux package targets Ubuntu 24.04-class systems.

Windows ZIPs are expected to carry both ACE DLL names, MSVC imports, refreshed MinGW DLL imports and MinGW static ACE libraries, plus matching headers, OAds glue and stats display source, license, notices and sample server INI; the gate verifies this list before the release appears. Match the client DLL, import/static libraries and OAds glue to this release; compile application code with the matching glue. Preserve your existing server INI and data when changing binaries.

## Validation and build provenance

All 12 source CI checks passed on the merged tree, including Windows MSVC x86 and x64, Linux with and without TLS, macOS, SQL live gates, PHP binding and Harbour smoke:
https://github.com/FiveTechSoft/OpenADS/actions/runs/39914842062

A local Linux build also passed the full CTest suite (7/7 suites, 1721 cases, 622184 assertions). The release-workflow run URL for this tag and the SHA-256 checksums of the packages will be added to this body once the build completes, before this pre-release is published. What remains pending is validation of the complete build against the real MLS 2026 application; this candidate exists so that test can happen.
