# Contributing to OpenADS

Thank you for helping improve OpenADS. This guide covers build,
test, pull-request workflow, and project policies every contributor
should follow.

## Quick start

### Prerequisites

- **CMake** 3.20+ and a supported toolchain (MSVC x64 on Windows,
  Clang/GCC on Linux, AppleClang on macOS).
- **Ninja** (recommended on Linux/macOS).
- **Git**.

### Build and test

```bash
# Configure (pick a preset for your platform)
cmake --preset msvc-x64          # Windows
cmake --preset ninja-clang       # Linux
cmake --preset default           # macOS

# Build
cmake --build build/<preset> --config Release

# Run unit tests
ctest --test-dir build/<preset> --output-on-failure -C Release
```

Presets are defined in [`CMakePresets.json`](CMakePresets.json).
Windows users can also run [`build_windows.bat`](build_windows.bat).

### DA-Web (optional)

See [`DA-Web/README.md`](DA-Web/README.md). Requires PHP 8.x with
the `php_openads` Zend extension (`bindings/php_ext/`) and
`openace64.dll` (or `ace64.dll` for Harbour drop-in builds).

### PHP bindings

| Path | Use when |
|------|----------|
| [`bindings/php`](bindings/php) | Pure PHP 8.1+ via `ext-ffi`; Composer package |
| [`bindings/php_ext`](bindings/php_ext) | Native Zend extension; powers DA-Web |

Details: [`bindings/php_ext/README.md`](bindings/php_ext/README.md).

### Harbour example

[`examples/harbour-hbmk2/`](examples/harbour-hbmk2/) — turnkey
`.hbp` project linking `contrib/rddads` against OpenADS `ace64.dll`.

---

## Pull-request workflow

1. **Fork** the repository on GitHub.
2. **Clone** your fork and add the upstream remote:

   ```bash
   git remote add upstream https://github.com/FiveTechSoft/OpenADS.git
   git fetch upstream
   ```

3. **Branch** from the latest `upstream/main`:

   ```bash
   git checkout -b your-topic upstream/main
   ```

4. **Change** one logical slice per PR (docs, fix, test — not mixed
   refactors).
5. **Test** on at least one platform before opening the PR.
6. **Push** to your fork and open a PR against `FiveTechSoft/OpenADS:main`.
7. Use honest commit prefixes: `docs:`, `fix:`, `test:`, `feat:`,
   or `wip:` when not yet validated.

**Maintainers merge; contributors do not.** Never push directly to
`main` on the upstream repo. Every change lands through an open PR
that the project owner reviews and merges.

### Releasing (maintainers)

Push a `v*` tag and let `.github/workflows/release.yml` create the
GitHub Release. Windows assets **must** be named
`openads-<ver>-windows-x64.zip` / `openads-<ver>-windows-x86.zip`.

Do **not** also upload `openads-<ver>-win-x64.zip` /
`openads-<ver>-win-x86.zip`. Those short names were a hand-rolled
slim kit that shipped beside the CI zip (v1.8.84–v1.8.86) with a
different `ace64.dll` and a different layout. See
[`packaging/windows/README.md`](packaging/windows/README.md).

### Every PR must be documented

The PR description should include:

| Section | Content |
|---------|---------|
| **Summary** | One paragraph: what changes and why |
| **Changes** | Bullet list of files / behaviour touched |
| **Motivation** | User-visible problem or parity gap |
| **Testing** | Exact test files and cases run (see below) |

For behaviour changes, update the relevant doc under `docs/` or
`DA-Web/README.md` when the feature is user-facing.

### Every code PR must include tests

At minimum:

- **Unit test** in `tests/unit/` (doctest, picked up by `ctest`), or
- **Smoke test** in `tests/unit/*_smoke_test.cpp` or
  `tests/smoke/` for end-to-end ABI / RDD paths.

Docs-only PRs are exempt, but must still describe what was verified
(spelling, links, build of doc site if applicable).

### Test fixtures: generic in git, legacy local only

**Committed tests (`tests/unit/`, `ctest`, CI) must use generic,
self-contained fixtures** — built in the test itself or under
`tests/fixtures/` as minimal synthetic files (text `.add` v1, tiny
`.dbf`, temp directories). No hardcoded client paths, no dependency
on proprietary binary `.add`/`.adt` blobs from production or
third-party dumps.

| OK in git / PR | Not in git / PR |
|----------------|-----------------|
| `fs::temp_directory_path()` + synthetic DBF/DD | `pmsys.add`, `landlords.adt`, `F:\…` paths |
| `write_dd()` with `# OpenADS Data Dictionary v1` | Real legacy binary dictionaries |
| Skip-if-absent optional probes (existing upstream) | **New** tests that require legacy files |

**Validating against legacy binaries locally is fine** — run ad-hoc
scripts on your machine to compare behaviour. Do **not** commit those
scripts, paths, captures, or fixtures as part of a PR. Keep local-only
probes outside the repo or in `.gitignore` on your fork.

---

## Protocol policy (required reading)

OpenADS provides **two separate surfaces**:

1. **Local ACE-compatible ABI** — drop-in `ace32.dll` / `ace64.dll` /
   `libace.so` for Harbour `contrib/rddads` and similar clients.
2. **OpenADS-native wire protocol** — a **parallel, independent**
   TCP/TLS format documented in [`docs/wire-protocol.md`](docs/wire-protocol.md).
   It is **not** byte-compatible with any legacy proprietary remote
   wire format.

### Contributions MUST

- Extend the documented OpenADS wire spec, `openads_serverd`, or
  the local ABI through clean-room implementation.
- Validate behaviour via unit tests, `ctest`, and public Harbour
  harnesses where applicable.
- Use neutral terms for legacy binary formats (`.add`, `.adt`,
  `.adi`, `.adm`) in **new** comments, commits, issues, and docs
  you author.

### Contributions MUST NOT

- Target **byte-level compatibility** with any legacy remote wire
  protocol.
- Add **disassembly**, decompilation, or **offline cipher-breaking**
  workflows, scripts, research notes, or captures.
- Commit tooling or documentation for the above into the repository.

**Forbidden approaches must not appear in git history or pull
requests** — not in branches, diffs, issue attachments, or PR
descriptions. If exploratory work happens locally, do not push it.

### Clean-room sources

Acceptable inputs:

- Public Harbour `contrib/rddads` source (the primary call site).
- Observable behaviour of the legacy ACE API (error codes, field
  widths, index semantics) verified by tests.
- The OpenADS wire specification and existing engine code.

Not acceptable in versioned contributions:

- Leaked internal manuals.
- Disassembly or decompilation output.
- Proprietary protocol captures reproduced verbatim in the repo.

---

## Neutral terminology (for new content)

When writing commits, issues, or docs, prefer:

| Concept | Suggested term |
|---------|----------------|
| Discontinued commercial engine | legacy ACE engine, reference engine |
| Goal of OpenADS | compatible emulation, drop-in replacement |
| Legacy binary DD/tables | legacy `.add` / `.adt` binary formats |
| Remote access | OpenADS wire protocol (`tcp://` / `tls://`) |
| Comparison work | behavioural parity, compatibility matrix |

Avoid vendor trademarks and phrases like "reverse engineering" or
"disassembly" in contribution metadata.

---

## Pre-push checklist

1. `git diff` — no disassembly dumps, cryptanalysis scripts, or
   legacy wire-compat prototypes.
2. New files are tests, docs, or engine fixes aligned with the
   protocol policy above.
3. `ctest` passes (or PR title uses `wip:` with explanation).
4. PR description has Summary / Changes / Motivation / Testing.
5. Code change includes unit or smoke test (unless docs-only).
6. Tests use generic/synthetic fixtures only — no new legacy binary
   dependencies in `tests/unit/` or CI.
7. No real customer/patient data in fixtures.
8. Commit messages and PR text use neutral terminology.

---

## Where to look

| Topic | Location |
|-------|----------|
| Architecture | [`docs/en/architecture.md`](docs/en/architecture.md) |
| Wire protocol | [`docs/wire-protocol.md`](docs/wire-protocol.md) |
| Data Dictionary | [`docs/en/data-dictionary.md`](docs/en/data-dictionary.md) |
| **Temp tables behind `ORDER BY` / `DISTINCT` / `TOP`** | [`docs/materialised-cursor-temps.md`](docs/materialised-cursor-temps.md) — **required reading before changing that temp's format.** Three constraints pin it (standalone table, full-length column names, numeric scale); two are invisible until a specific customer scenario breaks, and both have already been regressed once. |
| Open tasks | [`TODO.md`](TODO.md), [`roadmap.txt`](roadmap.txt) |
| Changelog | [`CHANGELOG.md`](CHANGELOG.md) |
| Licence | [`LICENSE`](LICENSE), [`NOTICE`](NOTICE) |

---

## Questions

Open a GitHub issue on [FiveTechSoft/OpenADS](https://github.com/FiveTechSoft/OpenADS/issues)
with a minimal repro and the platform/preset you built on.

Portuguese summary: [`docs/pt/contribuindo.md`](docs/pt/contribuindo.md).