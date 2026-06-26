---
title: AdsGetConnectionType
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-connection-type/
---

# AdsGetConnectionType

Retorna se uma conexão é local ou remota.

## Sintaxe

```c
UNSIGNED32 AdsGetConnectionType(ADSHANDLE hConnect, UNSIGNED16 *pusType);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hConnect` | `ADSHANDLE` | Handle da conexão ou de qualquer objeto derivado dela (tabela, índice). |
| `pusType` | `UNSIGNED16*` | Saída — constante do tipo de conexão. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso.

## Constantes de Tipo de Conexão

| Constante | Valor | Descrição |
|-----------|-------|-----------|
| `ADS_LOCAL_SERVER` | 0 | Conexão local (em processo). |
| `ADS_REMOTE_SERVER` | 1 | Conexão remota TCP/TLS. |

## Descrição

`AdsGetConnectionType` determina se o handle dado resolve
para uma conexão de motor local ou uma conexão TCP remota.
Primeiro verifica se é um handle de tabela remota; se encontrado,
relata `ADS_REMOTE_SERVER`. Caso contrário, recorre ao registro
de conexões locais e relata `ADS_LOCAL_SERVER`.

Você pode passar um handle de tabela, handle de índice ou handle de conexão —
a função resolve através do registro de handles para determinar
o tipo de conexão.

## Exemplo

```c
ADSHANDLE hConn;
UNSIGNED16 connType = 0;
AdsConnect60("tcp://server:6247", NULL, NULL, NULL, 0, &hConn);
AdsGetConnectionType(hConn, &connType);
if (connType == ADS_REMOTE_SERVER)
    printf("Connected to remote server\n");
else
    printf("Local connection\n");
AdsDisconnect(hConn);
```

## Ver Também

- [AdsConnect60]({{ site.baseurl }}/pt/funcoes/ads-connect60/)
- [AdsIsConnectionAlive]({{ site.baseurl }}/pt/funcoes/ads-is-connection-alive/)
- [AdsGetHandleType]({{ site.baseurl }}/pt/funcoes/ads-get-handle-type/)

---

[← AdsGetDateFormat]({{ site.baseurl }}/pt/funcoes/ads-get-date-format/)
[AdsGetDefault →]({{ site.baseurl }}/pt/funcoes/ads-get-default/)
