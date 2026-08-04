# Date display format — SAP's rules, and where OpenADS deliberately deviates

Every rule here was **probed against SAP ace64.dll** (scratch ADT + DBF
tables, `tools`-style dynamic loading), not inferred from documentation.
Re-probe before changing behaviour; the help files do not spell most of
this out.

## The split that matters: GetField formats, GetString does not

| Read | Date column | Blank date | Timestamp column |
|---|---|---|---|
| `AdsGetField` | **formatted** per the date format — `01/15/2024` (len 10) | `  /  /    ` (format with digits blanked) | `01/15/2024 01:45:59 PM` (len 22, 12-hour, 2-digit hour) |
| `AdsGetString` | **raw** `20240115` (len 8) — format-independent | `        ` (8 spaces, len 8) | SAP: error 5066 · **OpenADS: raw `YYYYMMDDhhmmss`** (deviation, see below) |
| `AdsGetDate` | formatted (delegates through GetField) | formatted blank | — |
| `AdsGetJulian` | numeric JDN | 0 | — |

This split is SAP's own behaviour, not an OpenADS invention. **Do not
"unify" the two entry points** — clients depend on each side:

- The S4 parity harness (`dd_meta_dump`) reads via `AdsGetField`, so SQL
  result dates compare formatted.
- `php_ads` (DA-Web, OpenERP) reads **only** via `AdsGetString` and parses
  the raw `YYYYMMDD` / `YYYYMMDDhhmmss` shapes itself. Formatting
  `AdsGetString` would break every PHP date read.
- Engine internals (index keys, WHERE compares, MIN/MAX text accumulation,
  materialised temp cells) use the raw decode — `YYYYMMDD` orders
  lexicographically as it does chronologically, which those paths rely on.

`AdsGetString` on remote/backend handles delegates through `AdsGetField`;
a thread-local (`g_field_read_raw`) keeps that delegated read raw.

## The format string

- Process-wide; default **`MM/DD/CCYY`** (SAP's default, probed).
- `AdsSetDateFormat` normalises: uppercased, and `YYYY` is stored as
  `CCYY` (`DD.MM.YYYY` reads back `DD.MM.CCYY`).
- Recognised tokens: `CCYY`/`YYYY`, `YY`, `MM`, `DD`; everything else is a
  literal separator. (`format_ace_date` / `blank_ace_date` /
  `parse_date_by_format` in `src/abi/ace_exports.cpp`.)

## Writes

| Write | SAP | OpenADS |
|---|---|---|
| `AdsSetDate("03/07/2025")` (current format) | parses per format | same (`parse_date_by_format`) |
| `AdsSetDate("20250307")` raw | **rejects** (5080) | accepts (superset — kept for existing callers) |
| `AdsSetDate` ISO `2025-03-07` | — | accepts (legacy OpenADS shape) |
| `AdsSetString` into a Date/Timestamp | **rejects** (5066) | accepts raw text (the remote twin routes every SetField through AdsSetString; php writes raw) |
| `AdsSetTimeStamp("01/15/2024 13:45:59")` | parses (24-hour input) | same; also accepts ISO and compact 14-digit |
| `AdsSetEmpty` on an ADT Date | blank (JDN 0) | same — **fixed 2026-08-04**: the empty string used to fall into the space-pad branch of `encode_field_string`, storing 0x20202020 = JDN 538976288 = the year 1470954 |
| 2-digit-year formats (`MM/DD/YY`) | epoch-resolved | same (`openads::engine::epoch()`) |

## Deliberate deviations (do not "fix" toward SAP without checking php)

1. **`AdsGetString` on a Timestamp** returns the raw `YYYYMMDDhhmmss`.
   SAP raises 5066 there. `php_ads` reads timestamps exactly this way —
   adopting SAP's error would break every PHP timestamp read.
2. **String writes into date fields keep working** (raw `YYYYMMDD`,
   ISO, or current-format text). SAP 5066s / 5080s these. The remote
   session's SetField path and php both write through strings.
3. **`ModTime` (ADT type 22)** is not display-formatted — php reads it
   raw via GetString; SAP behaviour for GetField on ModTime is unprobed.

## Regression tests

- `tests/unit/abi_date_format_test.cpp` — the probe table above as
  assertions, verified to fail with the formatting reverted.

If a gate case or client suddenly sees `20240115` where it expected
`01/15/2024` (or vice versa), check which entry point it reads through
before touching the engine — the difference is the spec, not a bug.
