---
title: AdsGetIndexFilename
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-index-filename/
---

# AdsGetIndexFilename

Retorna o caminho do arquivo de índice para uma determinada ordem.

## Sintaxe

```c
UNSIGNED32 AdsGetIndexFilename(ADSHANDLE hIndex, UNSIGNED16 usOrder,
                                UNSIGNED8 *pucBuf, UNSIGNED16 *pusBufLen);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hIndex` | `ADSHANDLE` | Handle da ordem de índice. |
| `usOrder` | `UNSIGNED16` | Reservado, passe 0. |
| `pucBuf` | `UNSIGNED8*` | Buffer de saída para o caminho do arquivo. |
| `pusBufLen` | `UNSIGNED16*` | Entrada/saída — capacidade do buffer na entrada, comprimento real na retorno. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso.

## Descrição

`AdsGetIndexFilename` retorna o caminho resolvido do sistema de arquivos do
arquivo `.cdx` / `.ntx` / `.adi` que contém a determinada ordem de índice.
O caminho é armazenado no `IndexBinding` no momento do registro do handle
e é sempre preciso tanto para índices ativos quanto pendentes.

## Exemplo

```c
ADSHANDLE hIndex;
char path[260];
UNSIGNED16 len = sizeof(path);
AdsGetIndexHandle(hTable, "lastname", &hIndex);
AdsGetIndexFilename(hIndex, 0, (UNSIGNED8*)path, &len);
printf("Index file: %s\n", path);
```

## Ver Também

- [AdsGetIndexExpr]({{ site.baseurl }}/pt/funcoes/ads-get-index-expr/)
- [AdsGetIndexCondition]({{ site.baseurl }}/pt/funcoes/ads-get-index-condition/)
- [AdsOpenIndex]({{ site.baseurl }}/pt/funcoes/ads-open-index/)

---

[← AdsGetIndexCondition]({{ site.baseurl }}/pt/funcoes/ads-get-index-condition/)
[AdsGetIndexOrderByHandle →]({{ site.baseurl }}/pt/funcoes/ads-get-index-order-by-handle/)
