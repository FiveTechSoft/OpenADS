---
title: Getting started
layout: default
parent: Home (EN)
nav_order: 1
permalink: /en/getting-started/
---

# Getting started

OpenADS is a CMake project written in C++17. It builds on
Windows (MSVC), Linux (clang or gcc), and macOS (AppleClang).

## Build

```sh
git clone https://github.com/FiveTechSoft/OpenADS
cd OpenADS
cmake --preset default
cmake --build build/default --config Release
ctest --test-dir build/default --output-on-failure -C Release
```

Output binaries:

- `ace64.dll` (Windows) / `libace.so` (Linux) / `libace.dylib`
  (macOS) under `build/default/src/Release/` — the drop-in ACE
  replacement.
- `tools/serverd/openads_serverd` — standalone TCP server CLI.
- `tools/bench/openads_bench` — cross-platform SQL workload timer.

## Build options

- `OPENADS_WITH_HTTP=ON` (**default ON since v1.0.0-rc20**) — builds
  the **Studio** web console into `openads_serverd` *and* into
  `ace64.dll` / `ace32.dll` (LocalServer mode). Vendors `cpp-httplib`
  and `nlohmann/json`. Pass `-DOPENADS_WITH_HTTP=OFF` to opt out.
- `cmake -DOPENADS_WITH_TLS=ON …` — enables `tls://` client URIs in
  `AdsConnect60`. Vendors `mbedtls 3.6 LTS` (Apache-2.0) and
  **statically links** it since v1.0.0-rc8 — zero runtime `libssl`
  / `libcrypto` / `mbedtls` DLL dependency.

The Windows release ZIP bundles **both** `ace64.dll` (x64) and
`ace32.dll` (x86) with matching `openads_serverd_{x64,x86}.exe`
since v1.0.0-rc8, so X#, Harbour-x86, and legacy Clipper apps all
pick the right bitness from one download.

## Smoke test (drop-in)

Place `ace64.dll` (or `libace.so`) on a Harbour application's
`PATH` ahead of any SAP-shipped copy. Existing `contrib/rddads`
calls now hit OpenADS.

## Build your own Harbour app against OpenADS (`hbmk2` / `.hbp`)

The repo ships a turnkey `hbmk2` template at
[`examples/harbour-hbmk2/`](https://github.com/FiveTechSoft/OpenADS/tree/main/examples/harbour-hbmk2)
— drop your `.prg` next to `openads_demo.hbp`, point
`OPENADS_LIB` at OpenADS' build output, run `hbmk2`. The
produced `.exe` drives DBF / CDX through Harbour's stock
`contrib/rddads` RDD but every `Ads*` call lands on OpenADS'
`ace64.dll` instead of any SAP-shipped one.

```cmd
:: From a Visual Studio x64 Developer Command Prompt:
cd examples\harbour-hbmk2
set OPENADS_LIB=C:\OpenADS\build\default\src\Release
set PATH=C:\harbour\bin\win\msvc64;%OPENADS_LIB%;%PATH%
hbmk2 openads_demo.hbp
copy /y "%OPENADS_LIB%\ace64.dll" .
openads_demo.exe
```

The `.hbp` is intentionally minimal — only the two link entries
that change for OpenADS:

```hbmk
openads_demo.prg
-comp=msvc64
-lrddads                    # Harbour's ADS RDD (contrib/rddads)
-L${OPENADS_LIB}
-lace64                     # OpenADS' import lib (instead of SAP's)
-lrddcdx
-lrddntx
-lrddfpt
```

A 32-bit variant (`openads_demo_x86.hbp` → `-lace32`) sits
alongside, plus a POSIX `build.sh`. For FiveWin (FWH) GUI apps
`hbmk2` is not enough — see
[`examples/fivewin/`](https://github.com/FiveTechSoft/OpenADS/tree/main/examples/fivewin)
for `build_msvc64.cmd` that mirrors FWH's stock build script
with `rddads.lib` + OpenADS' `ace64.lib` added in.

Typical "my hbmk2 attempt failed" symptoms:

| Symptom | Likely cause |
|---|---|
| `unresolved external symbol AdsConnect60` (or any `Ads*`) | `OPENADS_LIB` not set, or wrong toolchain — `ace64.lib` is MSVC; for bcc64 / MinGW use the matching import lib. |
| `lib 'rddads' not found` | `contrib/rddads` not built for the `-comp=…` you chose. Rebuild Harbour's contrib for that toolchain. |
| Runtime `ace64.dll not found` | DLL not next to the exe and not on `PATH`. |
| Strings show truncated in a TBrowse / xBrowse | Fixed in v1.0.0-rc27 — `AdsGetField` now pads CHAR to declared width. |
| `AdsVersion()` looks like `12.0` / `11.10` | You loaded SAP's `ace64.dll`. `where ace64.dll` and reorder `PATH`. |

## Smoke test (TCP server + Studio)

```sh
# 1. Build (HTTP is on by default since rc20)
cmake --preset default
cmake --build build/default --target openads_serverd --config Release

# 2. Launch the daemon
./build/default/tools/serverd/openads_serverd \
    --port 6262 \
    --http-port 6263 \
    --data /path/to/your/data \
    --error-log-path /path/to/logs

# 3. Open the Studio in any browser
xdg-open http://localhost:6263/         # Linux
open       http://localhost:6263/       # macOS
start      http://localhost:6263/       # Windows
```

Server operational log (`ads_err.dbf`) and the distinction from the
transaction journal (`openads.txlog`): see
[Error log and journal paths](error-log/).

## LocalServer Studio (in-process)

Since v1.0.0-rc9 the same Studio console is embedded in
`ace64.dll` / `ace32.dll`. A Harbour / X# / Clipper app that loads
the OpenADS DLL gets the SPA in its own process — no daemon
needed. Set `OPENADS_STUDIO_PORT=8080` before launching the host
app to auto-start it, or call `AdsStudioStart(port, data_dir)`
from the host code. See [Studio guide](studio-guide/) for the
full surface (mode badge, AOF demo, REST API).

## Run `openads_serverd` as a system service

Since v1.0.0-rc14:

- **Windows**: `openads_serverd --install-service` (auto-start
  via SCM); `--uninstall-service` removes it.
- **Linux**: `scripts/openads-serverd.service` is a hardened
  systemd unit (`User=openads`, `ProtectSystem=strict`,
  `NoNewPrivileges`).
- **macOS**: `scripts/com.openads.serverd.plist` is a launchd
  plist with KeepAlive on crash.

See [Service deployment](service-deployment/) for the full
recipes.
