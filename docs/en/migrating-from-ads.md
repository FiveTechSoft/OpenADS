---
title: Migrating from ADS
layout: default
parent: Home (EN)
nav_order: 1
permalink: /en/migrating-from-ads/
---

# Migrating from ADS

OpenADS is an open-source, ADS-compatible engine for applications built against
the Advantage Database Server (ADS) and the Advantage Client Engine (ACE) API.
If your data is DBF/ADT with CDX/NTX/ADI indexes and your app uses `rddads`,
the ACE C API, X#'s `AXDBFCDX`, or the PHP `advantage` extension, you can move
without rewriting it. This guide maps each piece of an Advantage install to its
OpenADS counterpart.

## Concept map

| Advantage | OpenADS | Notes |
|-----------|---------|-------|
| Advantage Database Server (service) | `openads_serverd` | TCP server; installs as a Windows Service / Linux systemd / macOS launchd. |
| Advantage Local Server | the engine DLL itself | `ace64.dll` runs the engine in-process; no separate `adsloc` DLL. |
| `ace32.dll` / `ace64.dll` (client) | `ace64.dll` / `ace32.dll` (drop-in) | Same name, same C ABI. Also shipped as `openace64.dll` for side-by-side installs. |
| Advantage Data Architect (ARC) | **Studio** web console | Browser UI: tables, SQL, structure, data dictionary. Launch with `openads-studio.bat`/`.sh`. |
| `ads.cfg` / server settings | `openads.ini` | Written by `openads_serverd --setup`, read with `--config`. |
| Data dictionary `.add` / `.ai` | same `.add` / `.ai` | Opened and managed natively. |
| Connection path `\\srv\vol\data` | `tcp://srv:6262/data` | Remote URI; local stays a filesystem path. |
| Default TCP port **6262** | **6262** | Identical, so keep it — unless ADS still runs on the same host (see below). |

## Server side — install like the old setup did

The old ADS installer asked for a port, a data folder, a code page and whether
to start as a service. OpenADS does the same through a console wizard:

```
openads_serverd --setup
```

It writes an `openads.ini` and, if you ask it to, registers the auto-start
service for your platform. Run it later straight from that file:

```
openads_serverd --config openads.ini
```

> **Running OpenADS and ADS on the same machine?** Both default to TCP 6262.
> Either stop the Advantage service first, or give OpenADS another port
> (`port = 6263` in the ini, or `--port 6263`). The server prints a clear hint
> if the bind clashes.

> **Code page.** OpenADS serves UTF-8 / CP437 today; a selectable server code
> page (CP850, Windows-1252) is on the roadmap. If your DBFs are CP850, test a
> copy before going live.

> **Rollback to SAP ACE is not automatic.** OpenADS writes CDX files in
> Harbour's DBFCDX-compatible format (magic `RCHB` at offset 0x14 in the
> structure-tag header). SAP ACE does not recognize this layout and returns
> **error 7017** (*Corrupt .ADI, .CDX, or .IDX index*) on any table whose CDX
> has been touched by OpenADS. OpenADS **reads** SAP-built CDX files fine as
> long as you do not write through OpenADS.
>
> If you may need SAP ACE again later (rollback, mixed environment, or
> side-by-side benchmarking), either keep the original SAP-built `.cdx` files
> backed up **before** pointing the app at OpenADS, or, after rolling back to
> SAP, delete the OpenADS-written `.cdx` files and rebuild all tags under SAP.
> NTX and ADI indexes follow the same one-way rule once OpenADS has written
> them.

## Client side — point your app at OpenADS

You do **not** relink for the common case:

1. Put **`ace64.dll`** (32-bit app → `ace32.dll`) next to your `.exe`, or on the
   `PATH` ahead of any existing copy.
2. Run the app. `rddads` / FiveWin / xBase++ binaries that load Advantage by the
   canonical name now use OpenADS unchanged.

If you link a fresh build, use the import lib for your compiler under `lib/`
(MSVC, MinGW, Borland). Ordinal-linked legacy binaries: see
[`ordinal-compat.md`](ordinal-compat.md). RDD specifics:
[`rddads-compat.md`](rddads-compat.md).

Remote connections use a URI in place of a UNC path:

```c
AdsConnect60("tcp://dbhost:6262/apps/sales/sales.add",
             user, pass, ADS_REMOTE_SERVER, &hConn);
```

Other clients: the PHP extension mirrors the old `php_advantage` API
(`bindings/php_ext/`), and a portable FFI binding is in `bindings/php/`.

## Administering data — use Studio instead of ARC

Run `openads-studio.bat` (Windows) or `./openads-studio.sh` (Linux/macOS) from
the release folder, or, on a running server, start it with
`--http-port 6263` and open `http://SERVER:6263/`. Studio covers what you did in
ARC: browse and edit rows, run SQL, view/rebuild structure (reindex, pack, zap),
and create/edit data-dictionary objects (tables, users, indexes, RI rules).
Secure it with `--http-user user:password`; for public exposure put a TLS proxy
in front (see [`tls-deployment.md`](tls-deployment.md)).

## Checklist

- [ ] Install the server: `openads_serverd --setup` → `openads.ini`.
- [ ] Resolve the 6262 port if ADS still runs on the host.
- [ ] Drop `ace64.dll`/`ace32.dll` next to your app (or relink with `lib/`).
- [ ] Back up SAP-built `.cdx` files if you may need to roll back to SAP ACE.
- [ ] Smoke-test reads/writes/index seeks against a **copy** of the data.
- [ ] Verify accented text (code-page caveat above).
- [ ] Use Studio (`openads-studio`) for day-to-day admin.
- [ ] Set up auto-start (wizard, or [`service-deployment.md`](service-deployment.md)).

---

*Advantage Database Server, the Advantage Client Engine and Advantage Data
Architect are names of their respective owners, used here only to describe
compatibility. OpenADS is an independent project and is not affiliated with or
endorsed by them.*
