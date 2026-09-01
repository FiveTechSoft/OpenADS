# OpenADS — Quick Start

This release archive is self-contained: the engine, the server and the admin
console are all here. Pick the path that matches how you used Advantage.

## What's in the box

| File | What it is |
|------|------------|
| `ace64.dll` / `ace32.dll` | The engine — a **drop-in** for the ACE `ace64.dll` / `ace32.dll`. Contains the whole engine (local **and** remote), so there is no second `adsloc` DLL to ship. |
| `openace64.dll` / `openace32.dll` | The same engine under its own name (use it when an existing `ace64.dll` must stay alongside, e.g. a PHP box). |
| `lib/` | Import libraries for MSVC, MinGW and Borland (link your app against these). |
| `openads_serverd(.exe)` | The network server (the "Advantage Database Server" equivalent). |
| `openads-studio.bat` / `.sh` | One-click **web admin console** — the ARC replacement. |
| `openads.ini.sample` | Documented server config template. |

---

## 1. Local / embedded app — "just grab the DLLs"

If your application opened tables directly from a folder (ADS Local Server),
you only need the DLL:

1. Copy **`ace64.dll`** (or `ace32.dll` for a 32-bit app) into the folder next
   to your `.exe`, or anywhere on the `PATH` **ahead of** any existing copy.
2. Run your app. A Harbour `rddads` / FiveWin / xBase++ app that loaded
   Advantage by the canonical name now talks to OpenADS — no recompile.

> Linking a fresh build? Use the matching import lib in `lib/` (MSVC, MinGW or
> Borland). See `docs/en/rddads-compat.md` and `docs/en/ordinal-compat.md`.

## 2. Browse / edit your data — the ARC replacement

Double-click **`openads-studio.bat`** (Windows) or run **`./openads-studio.sh`**
(Linux/macOS) from this folder. Your browser opens a full admin console:
table browser, SQL editor, structure/reindex/pack, and a data-dictionary
manager — the jobs you used Advantage Data Architect for.

Pass a data folder as the first argument to open it directly:

```
openads-studio.bat C:\mydata
./openads-studio.sh /var/lib/mydata
```

## 3. Network server — the "Advantage Database Server" equivalent

Let the guided wizard ask the questions the old installer did (port, data
folder, auto-start) and write a config file:

```
openads_serverd --setup
```

> Serving tables that live under more than one drive or share? Answer the
> "Data directory" prompt with a `;`-separated list, e.g.
> `C:\app\data;D:\more-data` — a client only has to connect to a path under
> one of them.

Then run it from that file:

```
openads_serverd --config openads.ini
```

Clients connect with a remote URI:

```
AdsConnect60("tcp://SERVERHOST:6262/data/mydb.add", user, pass, ADS_REMOTE_SERVER, ...)
```

### How many clients can connect?

The server caps concurrent connections at **500** by default. Each client
process/connection counts; beyond the cap new connections are refused
silently (the client sees a failed `AdsConnect60`). To raise it, edit the
`openads.ini` the wizard wrote and add:

```ini
max_sessions = 1000   ; 0 = unlimited
backlog      = 256    ; TCP accept queue for connect bursts
```

then restart the daemon. Check the effective value with
`openads_serverd --help` (the flag is `--max_sessions N`; the matching
env var is `OPENADS_SERVER_MAX_SESSIONS`). See `openads.ini.sample` for
every key with defaults and explanations.

To run it unattended (Windows Service / Linux systemd / macOS launchd), answer
"yes" to the wizard's auto-start question, or see `docs/en/service-deployment.md`.

---

Migrating an existing Advantage deployment? Read
**`docs/en/migrating-from-ads.md`** for the concept-by-concept map.
