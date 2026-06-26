---
title: Início (PT)
layout: default
nav_order: 4
permalink: /pt/
has_children: true
---

# OpenADS — Documentação (Português)

OpenADS é uma implementação livre e *clean-room* de um motor
de banco de dados compatível com ADS. Funciona como
**substituto direto** do Advantage Client Engine (`ace32.dll` /
`ace64.dll` / `libace.so`) — aplicações Harbour / Clipper que
fazem link com `contrib/rddads` continuam funcionando sem
recompilar.

Release atual: **v1.1.0**.

## Conteúdo

- **[Novidades](novidades/)** — resumo das mudanças desde
  v1.0.0-rc29 (driver SQLite, triggers, DA-Web, escrita ADI, e
  mais).
- **[História do projeto](historia/)** — como o OpenADS cresceu
  de um esqueleto mínimo até um motor completo compatível com
  ADS (868 commits).
- **[Como contribuir](contribuindo/)** — build, PRs, política de
  protocolo paralelo e checklist.
- **[Primeiros passos](primeiros-passos/)** — instalação,
  primeiro build, smoke test.
- **[Arquitetura](arquitetura/)** — arquitetura de cinco
  camadas (ABI / Sessão / SQL / Motor / Plataforma).
- **[Protocolo wire](/OpenADS/en/wire-protocol/)** — spec do
  wire TCP / TLS nativo OpenADS (frame, opcodes, payload, erros,
  versionamento). *Disponível em inglês.*
- **[Dicionário de dados](dicionario-dados/)** — formato `.add`
  clean-room + API `engine::DataDict` + superfície REST.
- **[Studio (console web)](guia-studio/)** — administração do
  motor a partir de qualquer navegador (modo Remote Server *ou*
  LocalServer).
- **[Benchmarks](benchmarks/)** — SQL local + AOF (Rushmore) +
  repaint xbrowse sobre o wire.
- **[Compat rddads / X# RDD](rddads-compat/)** — superfície de
  compatibilidade Harbour `contrib/rddads` e X# `AXDBFCDX`
  (rc19 M12.22 / M12.23).
- **[Backend SQLite](sqlite-backend/)** — abrir e operar um banco
  de dados SQLite via ACE / rddads com uma URI `sqlite://`.
- **[Stored procedures](stored-procedures/)** — procedimentos AEP
  personalizados (DLL/SO externa) e os procedimentos `sp_*`
  integrados do Data Dictionary.
- **[Cookbook](https://github.com/FiveTechSoft/OpenADS/tree/main/cookbook)**
  — exemplos Harbour executáveis e comentados (trilhas console +
  ORM), de simples a avançado.
- **[Implantação como serviço](servico-implantacao/)** — rodar
  `openads_serverd` como serviço Windows / unit systemd /
  launchd plist (rc14).
- **[Implantação TLS](tls-implantacao/)** — terminar HTTPS
  na frente do Studio com Caddy / nginx / stunnel / túnel SSH.
- **[Compatibilidade de ordinais](ordinal-compat/)** — resolver
  o erro Win "ordinal NNN não encontrado" quando a import
  table da app referencia ordinais SAP.
- **[Referência da API](api-reference/)** — 357 funções `Ads*`
  exportadas por `ace64.dll` / `libace.so` (conexões, tabelas,
  índices, SQL, Data Dictionary, cifra, telemetria).
- **[Issues conhecidos](/OpenADS/known-issues/)** — items em
  aberto. *Disponível em inglês.*

## Outros idiomas

[English](/en/) · [Español](/es/)
