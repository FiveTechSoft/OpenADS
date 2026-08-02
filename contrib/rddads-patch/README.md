# rddads patch: dbSetOrder(0) must notify the engine

`dbsetorder0-natural-order.patch` — apply to Harbour `contrib/rddads/ads1.c`.

## Why

Stock Harbour rddads implements `OrdSetFocus(0)` / `dbSetOrder(0)` by
zeroing its local `hOrdCurrent` and **never calling any ACE API**
(`adsOrderListFocus` in `ads1.c`). From then on it navigates through the
*table* handle (`AdsGotoTop(hTable)` / `AdsSkip(hTable, n)`).

SAP ACE has no per-table "active order", so that works there. OpenADS
keeps one (activated when an index handle is used), so without a
notification the table keeps walking the last index order and
`dbSetOrder(0)` still shows index order instead of natural (record
number) order.

The patch makes both "clear order" branches (empty tag name and numeric
order 0) call `AdsSetIndexOrderByHandle( hTable, 0 )` — the OpenADS
extension the engine parks the active order on.

## Build notes

- Requires the OpenADS `ace.h` (declares `AdsSetIndexOrderByHandle`) on
  the include path when compiling rddads, e.g. copy
  `include/openads/ace.h` into `contrib/rddads/` and set
  `HB_WITH_ADS` to that directory, then rebuild `rddads` with hbmk2
  (`hbmk2 rddads.hbp`) for every toolchain you use (msvc64, msvc,
  mingw, mingw64, ...).
- Verified with `tests/e2e/b_big_e2e.prg`: stock rddads fails the
  `dbSetOrder(0) -> natural order` section (22/23); patched rddads
  passes 23/23.

## History

The original patch was written for OpenADS v1.8.46 (see
`_release_1.8.46_notes.md`) but lived only in a local Harbour working
tree; it was lost when that tree was reset, and the bug "reappeared"
on every rddads rebuilt from stock sources (reported again by
Pritpal Bedi, 32-bit mingw build, Aug 2026). This reconstruction is
byte-equivalent in behaviour to the one shipped in the Aug-1
`rddads.lib` (same two call sites, same referenced symbol).
Keep it in the repo so it is never lost again.
