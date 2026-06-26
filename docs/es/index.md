---
title: Inicio (ES)
layout: default
nav_order: 3
permalink: /es/
has_children: true
---

# OpenADS — Documentación (Español)

OpenADS es una implementación libre y *clean-room* de un motor
de base de datos compatible con ADS. Funciona como **reemplazo
directo** del Advantage Client Engine (`ace32.dll` /
`ace64.dll` / `libace.so`) — las aplicaciones Harbour / Clipper
que enlazan contra `contrib/rddads` siguen funcionando sin
recompilar.

Release actual: **v1.1.0**.

## Contenido

- **[Novedades](novedades/)** — resumen de cambios desde
  v1.0.0-rc29 (driver SQLite, triggers, DA-Web, escritura ADI,
  y más).
- **[Historia del proyecto](historia/)** — cómo OpenADS creció
  desde un esqueleto mínimo hasta un motor completo compatible
  con ADS (868 commits).
- **[Primeros pasos](primeros-pasos/)** — instalación, primer
  build, smoke test.
- **[Arquitectura](arquitectura/)** — arquitectura de cinco
  capas (ABI / Sesión / SQL / Motor / Plataforma).
- **[Protocolo wire](/OpenADS/en/wire-protocol/)** — spec del
  wire TCP / TLS nativo OpenADS (frame, opcodes, payload, errores,
  versionado). *Disponible en inglés.*
- **[Diccionario de datos](diccionario-datos/)** — formato `.add`
  clean-room + API `engine::DataDict` + superficie REST.
- **[Studio (consola web)](guia-studio/)** — administración del
  motor desde cualquier navegador (modo Remote Server *o*
  LocalServer).
- **[Benchmarks](benchmarks/)** — SQL local + AOF (Rushmore) +
  repaint xbrowse sobre el wire.
- **[Compat rddads / X# RDD](rddads-compat/)** — superficie de
  compatibilidad Harbour `contrib/rddads` y X# `AXDBFCDX`
  (rc19 M12.22 / M12.23).
- **[Backend SQLite](sqlite-backend/)** — abrir y operar una base
  de datos SQLite vía ACE / rddads con una URI `sqlite://`.
- **[Stored procedures](stored-procedures/)** — procedimientos AEP
  personalizados (DLL/SO externa) y los procedimientos `sp_*`
  integrados del Data Dictionary.
- **[Cookbook](https://github.com/FiveTechSoft/OpenADS/tree/main/cookbook)**
  — ejemplos Harbour ejecutables y comentados (pistas console +
  ORM), de simple a avanzado.
- **[Despliegue como servicio](servicio-despliegue/)** — correr
  `openads_serverd` como servicio Windows / unit systemd /
  launchd plist (rc14).
- **[Despliegue TLS](tls-despliegue/)** — terminar HTTPS
  delante de Studio con Caddy / nginx / stunnel / SSH tunnel.
- **[Compatibilidad de ordinales](ordinal-compat/)** — solucionar
  el error Win "no se encontró ordinal NNN" cuando la import
  table de la app referencia ordinales SAP.
- **[Referencia API](api-reference/)** — 357 funciones `Ads*`
  exportadas por `ace64.dll` / `libace.so` (conexiones, tablas,
  índices, SQL, Data Dictionary, cifra, telemetría).
- **[Issues conocidos](/OpenADS/known-issues/)** — items
  abiertos. *Disponible en inglés.*

## Otros idiomas

[English](/en/) · [Português](/pt/)
