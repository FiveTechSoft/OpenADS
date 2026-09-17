# OpenADS v1.09.54

## Fix: Linux leg detection + transient x86 (v1.09.53 postmortem)

v1.09.53 published nothing (gate held): the Linux Configure step
failed on the new Harbour-SDK detection, and the x86 Configure
failed with no repo changes since its green .52 run (transient
runner issue — nothing to fix, rerun clears it).

### Changes

- **Exact-path SDK provisioning**: the Linux leg now resolves the
  Harbour headers, MT VM archive and compiler with `find` and
  passes them as explicit `-D` paths (plus a loud layout dump on
  mismatch) instead of guessing install-layout subdirectories in
  CMake `HINTS`.
- No product-code changes versus v1.09.53 (server-side HRB UDF
  loading ships here unchanged).

### Test Results

- Local: clean configures verified flag OFF and flag ON.
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.54-windows-x64.zip`
- `openads-1.09.54-windows-x86.zip`
- `openads-1.09.54-linux-x64.tar.gz` (HRB-enabled serverd)
- `openads-1.09.54-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
