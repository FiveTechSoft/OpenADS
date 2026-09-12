# OpenADS v1.09.44

## Complete set: x86 package + contrib in x64

v1.09.43's engine (counts without frames) is unchanged in this
release. Its CI run lost the Windows x86 leg to a flaky MinGW
setup step, so the `windows-x86` package — the one 32-bit
Harbour apps load `ace32.dll` from — never published. This
release republishes the full set, plus a packaging fix.

### Changes (vs v1.09.43)

- **Full four-package set**, engine identical to v1.09.43.
- ** `contrib/oads_hb` ships in the x64 zip too** (was x86-only;
  the sources are arch-independent).

### Test Results

- MinGW-x64 (GCC 16.2) + strict clang: batching suites green;
  full unit suite 1566/1567 (sole failure pre-existing,
  MinGW-only CDX alloc-tail).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.44-windows-x64.zip`
- `openads-1.09.44-windows-x86.zip`
- `openads-1.09.44-linux-x64.tar.gz`
- `openads-1.09.44-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
