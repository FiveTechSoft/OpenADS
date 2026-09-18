# OpenADS v1.09.58

## Fix: the `[slow]`/`[flaky]` exclusions never worked (v1.09.57 postmortem)

v1.09.57 still ran the quarantined tests — and win-x64 timed out
at 30 minutes. Root cause, verified against the doctest 2.4.11
source in-tree: the short flag for `--test-case-exclude` is
`-tce`, not `-e` (bare `-e` is silently ignored), and repeated
`-tce` occurrences do NOT accumulate (`parseOption` takes one).
So the repo's long-standing `-e=*[slow]*` excluded nothing: every
tier always ran everything, including the 240 s storm cases —
which also explains past 30-minute Test timeouts on loaded
runners. Both tiers now use a single comma-separated `-tce`
(verified: default tier runs 1571, slow tier 4, quarantined cases
still pass explicitly on demand).

No product-code changes versus v1.09.57 (server-side HRB UDF
loading ships here unchanged).

### Test Results

- Local: tiers behave as designed; only failure anywhere is the
  documented pre-existing MinGW-only CDX alloc-tail case (fails
  identically on the clean tree; passes on MSVC/clang).
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.58-windows-x64.zip`
- `openads-1.09.58-windows-x86.zip`
- `openads-1.09.58-linux-x64.tar.gz` (HRB-enabled serverd)
- `openads-1.09.58-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
