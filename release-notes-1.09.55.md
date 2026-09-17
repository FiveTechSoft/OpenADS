# OpenADS v1.09.55

## Fix: Linux link used the wrong GT driver (v1.09.54 postmortem)

v1.09.54 got further — Configure passed, the pinned Harbour SDK
built — but the Linux link failed: we linked `gtstd` while
Harbour's own `rtl/gtsys.c` selects `HB_GT_REQUEST(TRM)` on Unix,
so `HB_FUN_HB_GT_TRM` stayed undefined. The GT selection is now
platform-conditional (gtwin on Windows, gttrm on Unix, plus
ncurses) instead of guessed.

No product-code changes versus v1.09.54 (server-side HRB UDF
loading ships here unchanged).

### Test Results

- Local: HRB proof build and suites as in v1.09.52 notes.
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.55-windows-x64.zip`
- `openads-1.09.55-windows-x86.zip`
- `openads-1.09.55-linux-x64.tar.gz` (HRB-enabled serverd)
- `openads-1.09.55-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
