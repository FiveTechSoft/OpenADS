# OpenADS v1.09.71

## ADT: append no longer slows down as the table grows (exclusive open)

Appending to a `.adt` table opened exclusively slowed from about
11,000 to about 300 records/second as the table grew. Every new record
was auto-locked and all locks were kept, so each append walked an
ever-growing lock list. Exclusive-open ADT appends now skip that lock
accumulation. Shared opens keep their existing lock behavior.

Reported by Tim (via Antonio) while converting an application's data to `.adt`.

### Changes

* `src/engine/table.cpp` - exclusive-open ADT appends skip auto-lock accumulation.
* `tests/unit/abi_adt_append_exclusive_test.cpp` - 10,000-record append
  regression test.
* Release notes in en/es/pt.

## Also since v1.09.70

* `src/engine/table.cpp` restored to the last good version after a bad
  web-UI commit duplicated part of the file.
* Commits to `main` now go through the validating apply-patch workflow.

### Packages

* `openads-1.09.71-windows-x64.zip`
* `openads-1.09.71-windows-x86.zip`
* `openads-1.09.71-linux-x64.tar.gz`
* `openads-1.09.71-macos-universal.tar.gz` - built, but its unit tests
  stall in CI and did not complete, so this package is UNTESTED /
  paquete macOS sin pruebas completas (under investigation).
  `release.yml` treats the macOS leg as optional until that is fixed.
