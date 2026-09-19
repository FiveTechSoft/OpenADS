# OpenADS v1.09.66

## Zip destination follows the table-path doctrine (field fix)

`cZipDir` is now mapped exactly like `.dbf` table paths
(`resolve_table_file` doctrine), so Vouch keeps sending
fully-qualified local spellings with zero changes:

- Default mode folds drives/root slashes onto the owning data
  root (`c:\creative.bkp\` → `<root>/creative.bkp`), same rule as
  source dirs; the filename stays application-dependent with no
  date or extension added.
- Under `--legacy-paths`, a spelling that repeats the root's own
  path strips it (`C:\data\bkp` under `--data C:\data` → `bkp`)
  instead of doubling it; anything else folds as above.
- The jail verdict still goes through canonical resolution
  (`..`/symlink escapes fail loud); only the build/report spelling
  stays lexical so `archive_rel` prefixes keep matching.

## MinGW shared DLL links again

Covered in v1.09.65's notes; shipped here.

### Test Results

- New legacy-remount case (`BACKUP/nightly` from a root-prefixed
  absolute spelling) plus the drive-folding case; full zip file
  12/12 green on MinGW x86/x64 (154 assertions).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.66-windows-x64.zip`
- `openads-1.09.66-windows-x86.zip`
- `openads-1.09.66-linux-x64.tar.gz`
- `openads-1.09.66-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
