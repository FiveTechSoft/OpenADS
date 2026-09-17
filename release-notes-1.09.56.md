# OpenADS v1.09.56

## Fix: clang-18 rejects version-gated pragma (v1.09.55 postmortem)

v1.09.55 got to the Linux build and failed compiling
`hrb_harbour.cpp`: our Harbour-header suppression named
`-Wc2y-extensions`, which the CI clang (18.1.3) does not know —
and under `-Werror` the unknown pragma itself is fatal. The shield
`-Wunknown-warning-option` now comes first so version-specific
groups degrade to silence on older Clangs (verified structure
locally; Windows/macOS legs were already green).

No product-code changes versus v1.09.55 (server-side HRB UDF
loading ships here unchanged).

Note: the x86 leg in .55 failed in an unrelated timing-sensitive
teardown test (`network_teardown_batch_test`, SIGSEGV) with no
connection to this work (HRB code is not compiled on that leg) —
same hopping-legs shape as the .50 x64 Test failure. Verdict:
pre-existing flake, watching it, not quarantining yet.

### Test Results

- Local: HRB suites green; pragma structure accepted by clang.
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.56-windows-x64.zip`
- `openads-1.09.56-windows-x86.zip`
- `openads-1.09.56-linux-x64.tar.gz` (HRB-enabled serverd)
- `openads-1.09.56-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
