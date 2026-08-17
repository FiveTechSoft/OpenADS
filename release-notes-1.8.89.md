## v1.8.89

### Added — process-wide audit sequence (Pritpal Bedi)

Each `RESOLVED` line now has three id columns:

```
TGZZDM 00000001 00000007 2026-08-16 20:39:46.910 RESOLVED="C:/..."
^^^^^^ ^^^^^^^^ ^^^^^^^^
conn   per-conn process
```

| Column | Width | Meaning |
|--------|-------|---------|
| Connection serial | 6 `[0-9A-Z]` | Which `AdsConnect` |
| Entry serial | 8 decimal | 1, 2, 3… on **that** connection |
| Process sequence | 8 decimal | 1, 2, 3… for the **whole process** — never restarts |

Two connections interleaving (ADS + DBF login) no longer repeat a
number in the third column. Detail lines of the same resolve reuse
both ENTRY and SEQ, so `OPENADS_LOG_FILE` stays a gapless sequence.

### Files

**x64 (64-bit):**

- `openads_serverd.exe` — server daemon
- `ace64.dll` / `openace64.dll` — ACE shared library (identical copies)
- `ace64.lib` — MSVC import library
- `libace64.a` — MinGW import library
- `ace64_borland.lib` — Borland import library
- `openads_bench.exe` — bench CLI

**x86 (32-bit):**

- `openads_serverd.exe` — server daemon
- `ace32.dll` / `openace32.dll` — ACE shared library
- `ace32.lib` — MSVC import library
- `libace32.a` — MinGW import library
- `ace32_borland.lib` — Borland import library
- `contrib/oads_hb/oads_hb.c` — Harbour HB_FUNC wrappers

**Full Changelog**: https://github.com/FiveTechSoft/OpenADS/compare/v1.8.88...v1.8.89
