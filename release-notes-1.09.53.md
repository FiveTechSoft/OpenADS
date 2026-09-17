# OpenADS v1.09.53

## Fix: release-blocking CMake parse error (v1.09.52 never shipped)

v1.09.52 published nothing: every leg failed at Configure in ~1 s
and the all-or-nothing gate correctly blocked the release. Cause
was a leftover duplicate (`gtwin` append + stray `endif()`) in the
new Harbour-detection block — a plain CMake syntax error, caught
locally by reproducing a clean configure with the flag both OFF
and ON before tagging this time.

No product-code changes versus v1.09.52 (server-side HRB UDF
loading ships here unchanged).

### Test Results

- Local: clean configures verified flag OFF and flag ON; HRB proof
  build and suites as in v1.09.52 notes.
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.53-windows-x64.zip`
- `openads-1.09.53-windows-x86.zip`
- `openads-1.09.53-linux-x64.tar.gz` (HRB-enabled serverd)
- `openads-1.09.53-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
