# OpenADS v1.09.51

## Fix: Linux/macOS release legs (clang `-Werror` break)

v1.09.50 shipped without `openads-1.09.50-linux-x64.tar.gz`:
the `ninja-clang` build (Linux) and the macOS build both failed
on the new login-gate stress test, while MSVC and MinGW-GCC
stayed green — so the publish step went ahead with 3 of 4 assets.

### Changes

- **One-line fix** (`tests/unit/network_login_gate_stress_test.cpp`):
  the 8-way racer lambda captured the loop index it never uses
  (`[&, t]` → `[&]`). Clang's `-Wunused-lambda-capture` (promoted
  by project-wide `-Werror`) rejects it; GCC/MSVC don't warn.
  No behavior change — all racers do identical work.
- Verified the v1.09.50 C++ delta (5 files) is now fully
  clang-clean, so no second round-trip is expected.

### Test Results

- MinGW-x64 (GCC 16.2): the 4 login-gate/lock-exclusion cases
  pass (648 assertions); REVERSE/index suites still green.
- Full matrix (Windows MSVC x64/x86, Linux, macOS) runs in CI on the
  tag push.

### Packages

- `openads-1.09.51-windows-x64.zip`
- `openads-1.09.51-windows-x86.zip`
- `openads-1.09.51-linux-x64.tar.gz`
- `openads-1.09.51-macos-universal.tar.gz`

*Field triage, release engineering and validation: Pritpal Bedi.*
