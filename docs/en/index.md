---
title: Home (EN)
layout: default
nav_order: 2
permalink: /en/
has_children: true
---

# OpenADS — Documentation (English)

OpenADS is a free, clean-room implementation of an
ADS-compatible database engine. It is a **drop-in replacement**
for the Advantage Client Engine (`ace32.dll` / `ace64.dll` /
`libace.so`) — Harbour / Clipper applications that link against
`contrib/rddads` keep working without recompilation.

Current release: **v1.7.0**.

## What's in here

- **[What's New](whatsnew/)** — summary of changes since
  v1.0.0-rc29 → v1.7.0 (remote scope, xBrowse fixes, CDX perf, and more).
- **[Project History](history/)** — how OpenADS grew from a
  minimal skeleton into a full ADS-compatible engine (868 commits).
- **[Migrating from ADS](migrating-from-ads/)** — map an Advantage
  install to OpenADS; rollback caveats (CDX error 7017), port 6262, Studio.
- **[Getting started](getting-started/)** — install, first build,
  smoke test.
- **[Architecture](architecture/)** — five-layer architecture
  (ABI / Session / SQL / Engine / Platform).
- **[Wire protocol](wire-protocol/)** — formal spec of the
  OpenADS-native TCP / TLS wire (frame layout, every opcode,
  payload format, error semantics, versioning).
- **[Main flows](flows/)** — interactive single-page sequence
  walk-through of the eight canonical call paths (local DBF,
  remote TCP, SQL pipeline, Studio HTTP, AOF/Rushmore, memo I/O,
  transactions, wire-protocol opcodes), backed by
  [`assets/flows.json`](../assets/flows.json).
- **[Data Dictionary](data-dictionary/)** — clean-room `.add`
  text format + the `engine::DataDict` API + REST surface.
- **[Studio (web console)](studio-guide/)** — administer an
  OpenADS database from any browser through the embedded HTTP
  console (Remote Server *or* LocalServer mode).
- **[Benchmarks](benchmarks/)** — cross-platform SQL workload
  numbers + AOF (Rushmore) + wire-protocol xbrowse repaint.
- **[rddads / X# RDD compat](rddads-compat/)** — Harbour
  `contrib/rddads` and X# `AXDBFCDX` compatibility surface
  (rc19 M12.22 / M12.23).
- **[SQLite backend](sqlite-backend/)** — open and drive a SQLite
  database through ACE / rddads via a `sqlite://` connection URI.
- **[Stored procedures](stored-procedures/)** — custom AEP
  procedures (external DLL/SO) and the built-in `sp_*` Data
  Dictionary procedures.
- **[Cookbook](https://github.com/FiveTechSoft/OpenADS/tree/main/cookbook)**
  — runnable, commented Harbour examples (console + ORM tracks),
  simple → advanced.
- **[Service deployment](service-deployment/)** — run
  `openads_serverd` as a Windows Service / systemd unit / macOS
  launchd plist (rc14).
- **[TLS deployment](tls-deployment/)** — terminate HTTPS in
  front of Studio with Caddy / nginx / stunnel / SSH tunnel.
- **[Ordinal compatibility](ordinal-compat/)** — fix the
  Windows "ordinal NNN not found" loader error when an app's
  import table references SAP-style ordinals.
- **[Known issues](../known-issues/)** — current open items.
- **[API Reference](api-reference/)** — complete reference
  for all 357 `Ads*` exported functions, organized by category
  with implementation status.

## Other languages

[Español](/es/) · [Português](/pt/)
