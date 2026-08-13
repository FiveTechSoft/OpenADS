---
title: AdsGetIndexCondition
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-index-condition/
---

# AdsGetIndexCondition

Retorna a expressão de condição FOR de uma ordem de índice.

## Sintaxe

```c
UNSIGNED32 AdsGetIndexCondition(ADSHANDLE hIndex, UNSIGNED8 *pucBuf,
                                UNSIGNED16 *pusBufLen);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hIndex` | `ADSHANDLE` | Handle da ordem de índice. |
| `pucBuf` | `UNSIGNED8*` | Buffer de saída para a string de condição. |
| `pusBufLen` | `UNSIGNED16*` | Entrada/saída — capacidade do buffer na entrada, comprimento real na retorno. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso. Retorna uma string vazia se o
índice não tiver condição FOR.

## Descrição

`AdsGetIndexCondition` recupera a expressão condicional (FOR)
associada a uma tag de índice. Nem todos os índices possuem uma
condição FOR; quando ausente, a função retorna uma string vazia com
`*pusBufLen = 0`.

Esta função segue o mesmo padrão de resolução de handle que
[AdsGetIndexExpr]({{ site.baseurl }}/pt/funcoes/ads-get-index-expr/):
primeiro verifica o binding pendente, depois o IIndex da ordem ativa.

## Exemplo

```c
ADSHANDLE hIndex;
char cond[256];
UNSIGNED16 len = sizeof(cond);
AdsGetIndexHandle(hTable, "active_cust", &hIndex);
AdsGetIndexCondition(hIndex, (UNSIGNED8*)cond, &len);
if (len > 0)
    printf("FOR condition: %s\n", cond);
else
    printf("No FOR condition\n");
```

## Ver Também

- [AdsGetIndexExpr]({{ site.baseurl }}/pt/funcoes/ads-get-index-expr/)
- [AdsGetIndexFilename]({{ site.baseurl }}/pt/funcoes/ads-get-index-filename/)
- [AdsOpenIndex]({{ site.baseurl }}/pt/funcoes/ads-open-index/)

---

[← AdsGetHandleType]({{ site.baseurl }}/pt/funcoes/ads-get-handle-type/)
[AdsGetIndexFilename →]({{ site.baseurl }}/pt/funcoes/ads-get-index-filename/)
