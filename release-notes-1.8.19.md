# OpenADS 1.8.19

## Changes

### Fixed — remote xBrowse ghost rows (scope + SET DELETED ON)

Reported with DBF/FPT/CDX, index scope, and one deleted key inside the
range: LOCAL showed one row; REMOTE xBrowse painted a blank/skipped row,
repeated data, and could refuse to close.

Two related holes:

1. **Scoped key count included deleted keys** while `Skip` did not.
2. **rddads `OrdKeyCount`** calls `AdsGetRecordCount(hOrdCurrent)`. On
   remote indexes that returned the parent table’s **physical**
   `RecCount`, not the live scoped key count.

Both paths now return the navigable live key count (scope + SET DELETED).

Verified (ACE path = what xBrowse uses): Tim mini (2 keys / 1 deleted →
walk=1, OrdKeyCount=1) and `delscope` (C005–C015 → 9 live) against an
iMac `openads_serverd`. Not a full FiveWin GUI run.

**Update both** client `ace32`/`ace64` **and** `openads_serverd`.

### Also includes (1.8.18)

Server filesystem API (`oads_*` / `Ads*`) with `EnableFileFunc`.

## Binaries

| Archive | Contents |
|---------|----------|
| `openads-1.8.19-windows-x64.zip` | `ace64.dll`, `openace64.dll`, `openads_serverd.exe`, import libs |
| `openads-1.8.19-windows-x86.zip` | `ace32.dll`, `openace32.dll` (stdcall), `openads_serverd.exe`, import libs |
