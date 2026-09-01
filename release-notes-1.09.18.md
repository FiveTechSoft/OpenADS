# Release notes — v1.09.18 (2026-08-31)

## Notation — one canonical spelling: `_` everywhere (Pritpal Bedi)

Every phrase now has a single canonical form on the command line, in
`openads.ini`, and in env variables: **underscore**.

- `openads.ini`: a `-` inside a key folds to `_` (`error-log-max` ==
  `error_log_max`), so old dash-style files keep working — but the
  canonical form shown everywhere is underscore.
- Command line: the same fold after `--`, so `--max_sessions` ==
  `--max-sessions`, `--http_port` == `--http-port`,
  `--enable_file_func` == `--enable-file-func`, etc. `--help` now
  documents the underscore forms, matching the OPENADS_* env vars.
- `openads.ini.sample` ships in canonical underscore notation.

## Added — server capacity config surfaced: `max_sessions` + `backlog` via CLI & openads.ini

The server's session cap (default **500** concurrent connections, from
`OPENADS_SERVER_MAX_SESSIONS` since v1.4.0) had **no** ini key or CLI
flag — deployments needing more concurrent clients (e.g. the B_BIG
700-instance stress) had to set an environment variable by hand. Worse,
the `backlog` ini key and `--backlog` CLI flag were parsed and printed
on the startup banner but **never actually reached `listen()`** — only
the env/default applied. Both are now real:

- **`--max_sessions N`** CLI flag and **`max_sessions = N`** ini key
  (dash spelling accepted, `0` = unlimited). Default is unchanged:
  `OPENADS_SERVER_MAX_SESSIONS` env, else 500.
  Connections beyond the cap are refused and counted
  (`rejected_sessions` counter, visible in the Studio Sessions panel).
- **`Server::set_backlog(n)`** wired through: `--backlog` / ini
  `backlog` now sets the TCP accept queue for the primary **and** every
  extra `[port:NNNN]` listener. Default remains env
  `OPENADS_SERVER_BACKLOG`, else 256.
- **`openads.ini.sample` rewritten from scratch**: every server and
  client key with its default value and an explanation, the precedence
  rules (env > CLI > ini > built-in), multi-port sections, and the full
  server-side env-var surface (`OPENADS_SERVER_*`).

### Suggested values for very-many-instance workloads (B_BIG ≥ 500)

```ini
max_sessions = 1000   ; instances + headroom (0 = unlimited)
backlog      = 256    ; absorb the connect burst
```

## Verification

- Full suite: **1520/1520 test cases, 573,277 assertions, 0 failures**
  (Windows x64, Release).
- Remote create/index/append storm at **700 concurrent connections**
  (the B_BIG staging dance: create → 3 CDX tags → shared open → 10
  appends each, then physical DBF+CDX validation): **7/7 clean runs**.
- New `parse_ini` cases: canonical underscore keys, dash aliases fold
  to the same field, `max_sessions` with `0=unlimited`, rejection of
  garbage.

## Upgrade notes

- No file-format or wire-protocol changes; drop-in replacement for
  v1.09.x serverd and ace32/ace64.
- If you serve more than ~500 simultaneous clients, add
  `max_sessions = 1000` (or your number) to your `openads.ini` —
  otherwise the cap stays at 500 exactly as before.
- `--backlog` now has a real effect; if you previously set an odd
  `backlog` value in openads.ini believing it inert, re-check it.
- Dash-style CLI flags and ini keys still work, but prefer the
  underscore spellings — that is what the help text and samples show.

## What to test (Pritpal)

1. Copy the new `openads.ini.sample` next to `openads_serverd.exe` as
   `openads.ini`, set `max_sessions = 1000`, restart the daemon.
2. Run B_BIG with 700 instances: instances losing the create race get a
   clean **7040** and proceed; final record count should be exactly
   10 × instances (100 each — verified byte-perfect on our 700-instance
   table).
3. If anything still misbehaves, report the stage (connect / DbCreate /
   INDEX ON / append) and the instance numbers from the window titles.