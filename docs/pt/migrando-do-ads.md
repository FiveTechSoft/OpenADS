---
title: Migrando do ADS
layout: default
parent: Início (PT)
nav_order: 1
permalink: /pt/migrando-do-ads/
---

# Migrando do ADS

O OpenADS é um motor open-source compatível com o ADS, para aplicações feitas
para o Advantage Database Server (ADS) e a API do Advantage Client Engine (ACE).
Se seus dados são DBF/ADT com índices CDX/NTX/ADI e seu app usa `rddads`, a API C
do ACE, o `AXDBFCDX` do X# ou a extensão PHP `advantage`, você migra sem
reescrever. Este guia mapeia cada peça de uma instalação Advantage para o
equivalente no OpenADS.

## Mapa de conceitos

| Advantage | OpenADS | Observações |
|-----------|---------|-------------|
| Advantage Database Server (serviço) | `openads_serverd` | Servidor TCP; instala como Serviço do Windows / systemd no Linux / launchd no macOS. |
| Advantage Local Server | a própria DLL do motor | `ace64.dll` roda o motor em processo; sem DLL `adsloc` separada. |
| `ace32.dll` / `ace64.dll` (cliente) | `ace64.dll` / `ace32.dll` (drop-in) | Mesmo nome, mesma ABI C. Também vem como `openace64.dll` para instalações lado a lado. |
| Advantage Data Architect (ARC) | console web **Studio** | Interface no navegador: tabelas, SQL, estrutura, dicionário de dados. Abra com `openads-studio.bat`/`.sh`. |
| `ads.cfg` / configurações | `openads.ini` | Gerado por `openads_serverd --setup`, lido com `--config`. |
| Dicionário `.add` / `.ai` | mesmos `.add` / `.ai` | Abertos e geridos nativamente. |
| Caminho `\\srv\vol\dados` | `tcp://srv:6262/dados` | URI remota; local continua caminho de arquivo. |
| Porta TCP padrão **6262** | **6262** | Idêntica — mantenha, a menos que o ADS ainda rode no mesmo host (veja abaixo). |

## Lado servidor — instale como o setup antigo

O instalador antigo do ADS perguntava porta, pasta de dados, code page e se
iniciava como serviço. O OpenADS faz o mesmo por um wizard de console:

```
openads_serverd --setup
```

Ele grava um `openads.ini` e, se você pedir, registra o serviço de auto-início
da sua plataforma. Depois rode direto do arquivo:

```
openads_serverd --config openads.ini
```

> **OpenADS e ADS na mesma máquina?** Ambos usam a porta TCP 6262. Pare o
> serviço Advantage primeiro, ou dê outra porta ao OpenADS (`port = 6263` no ini,
> ou `--port 6263`). O servidor avisa claramente se houver conflito de bind.

> **Code page.** O OpenADS serve UTF-8 / CP437 hoje; a seleção de code page do
> servidor (CP850, Windows-1252) está no roadmap. Se seus DBFs forem CP850,
> teste uma cópia antes de entrar em produção.

> **Rollback para SAP ACE não é automático.** O OpenADS grava CDX no formato
> compatível com DBFCDX do Harbour (magic `RCHB` no offset 0x14 do header da
> structure tag). O SAP ACE não reconhece esse layout e retorna **erro 7017**
> (*Corrupt .ADI, .CDX, or .IDX index*) em qualquer tabela cujo CDX foi tocado
> pelo OpenADS. O OpenADS **lê** CDX construídos pelo SAP sem modificação,
> desde que você não grave pelo OpenADS.
>
> Se você pode precisar do SAP ACE novamente (rollback, ambiente misto ou
> benchmark lado a lado), faça backup dos `.cdx` originais **antes** de apontar
> o app para o OpenADS, ou, após voltar ao SAP, apague os `.cdx` escritos pelo
> OpenADS e reconstrua todas as tags no SAP. Índices NTX e ADI seguem a mesma
> regra unidirecional após escrita pelo OpenADS.

## Lado cliente — aponte seu app para o OpenADS

No caso comum você **não** relinka:

1. Coloque **`ace64.dll`** (app 32 bits → `ace32.dll`) ao lado do seu `.exe`, ou
   no `PATH` antes de qualquer cópia existente.
2. Rode o app. Binários `rddads` / FiveWin / xBase++ que carregam o Advantage
   pelo nome canônico passam a usar o OpenADS sem alteração.

Para um build novo, use a import lib do seu compilador em `lib/` (MSVC, MinGW,
Borland). Binários legados ligados por ordinal: veja
[`../en/ordinal-compat.md`](../en/ordinal-compat.md). Detalhes de RDD:
[`rddads-compat.md`](rddads-compat.md).

Conexões remotas usam URI no lugar do caminho UNC:

```c
AdsConnect60("tcp://servidor:6262/apps/vendas/vendas.add",
             usuario, senha, ADS_REMOTE_SERVER, &hConn);
```

Outros clientes: a extensão PHP espelha a API antiga `php_advantage`
(`bindings/php_ext/`), e há um binding FFI portátil em `bindings/php/`.

## Administrar dados — use o Studio no lugar do ARC

Rode `openads-studio.bat` (Windows) ou `./openads-studio.sh` (Linux/macOS) na
pasta do release, ou, num servidor em execução, suba com `--http-port 6263` e
abra `http://SERVIDOR:6263/`. O Studio cobre o que você fazia no ARC: navegar e
editar registros, rodar SQL, ver/reconstruir estrutura (reindex, pack, zap) e
criar/editar objetos do dicionário (tabelas, usuários, índices, regras de RI).
Proteja com `--http-user usuario:senha`; para exposição pública, ponha um proxy
TLS na frente (veja [`servico-implantacao.md`](servico-implantacao.md)).

## Checklist

- [ ] Instalar o servidor: `openads_serverd --setup` → `openads.ini`.
- [ ] Resolver a porta 6262 se o ADS ainda roda no host.
- [ ] Largar `ace64.dll`/`ace32.dll` ao lado do app (ou relinkar com `lib/`).
- [ ] Backup dos `.cdx` do SAP se um rollback para o SAP ACE for possível.
- [ ] Testar leituras/gravações/seeks de índice numa **cópia** dos dados.
- [ ] Verificar acentuação (ressalva de code page acima).
- [ ] Usar o Studio (`openads-studio`) no dia a dia.
- [ ] Configurar auto-início (wizard, ou [`servico-implantacao.md`](servico-implantacao.md)).

---

*Advantage Database Server, Advantage Client Engine e Advantage Data Architect
são nomes de seus respectivos donos, citados aqui apenas para descrever
compatibilidade. O OpenADS é um projeto independente, sem afiliação ou
endosso.*
