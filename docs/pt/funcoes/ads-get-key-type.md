---
title: AdsGetKeyType
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-key-type/
---

# AdsGetKeyType

Retorna o tipo de dados da expressão de chave do índice.

## Sintaxe

```c
UNSIGNED32 AdsGetKeyType(ADSHANDLE hIndex, UNSIGNED16* p);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hIndex` | `ADSHANDLE` | Handle do índice. |
| `p` | `UNSIGNED16*` | Ponteiro para receber o tipo da chave. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso. `AE_INTERNAL_ERROR` (5000) se o handle for desconhecido.

## Descrição

`AdsGetKeyType` informa o tipo do resultado da expressão de chave do
índice, usando as constantes de *tipo de campo*:

| Constante | Valor | Descrição |
|-----------|-------|-----------|
| `ADS_LOGICAL` | 1 | Expressão de chave lógica. |
| `ADS_NUMERIC` | 2 | Expressão de chave numérica. |
| `ADS_DATE` | 3 | Expressão de chave de data. |
| `ADS_STRING` | 4 | Expressão de chave de caracteres. |
| `ADS_RAW` | 16 | Expressão de chave raw (concatenação `;` de ADT). |

Não são as constantes de codificação de buffer (`ADS_STRINGKEY` /
`ADS_DOUBLEKEY` / `ADS_RAWKEY`) usadas por `AdsSeek` e `AdsSetScope`.
Uma chave de campo simples responde pelo esquema da tabela; expressões
calculadas respondem pelo tipo do resultado (`UPPER(name)` →
`ADS_STRING`, `Val(code)` → `ADS_NUMERIC`). O rddads do Harbour usa
este valor para escolher a codificação da chave de `OrdScope()`.

## Exemplo

```c
UNSIGNED16 usKeyType;
AdsGetKeyType(hIndex, &usKeyType);
// usKeyType é ADS_STRING, ADS_NUMERIC, ADS_DATE ou ADS_LOGICAL
```

## Ver Também

- [AdsGetKeyLength]({{ site.baseurl }}/pt/funcoes/ads-get-key-length/)
- [AdsGetKeyCount]({{ site.baseurl }}/pt/funcoes/ads-get-key-count/)
- [AdsExtractKey]({{ site.baseurl }}/pt/funcoes/ads-extract-key/)

---

[AdsGetIndexHandle →]({{ site.baseurl }}/pt/funcoes/ads-get-index-handle/)
