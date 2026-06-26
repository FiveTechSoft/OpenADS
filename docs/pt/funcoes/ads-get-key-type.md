---
title: AdsGetKeyType
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-key-type/
---

# AdsGetKeyType

Retorna o tipo de codificação da chave de índice para a ordem ativa.

## Sintaxe

```c
UNSIGNED32 AdsGetKeyType(ADSHANDLE hIndex, UNSIGNED16 *pusKeyType);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hIndex` | `ADSHANDLE` | Handle da ordem de índice. |
| `pusKeyType` | `UNSIGNED16*` | Saída — constante do tipo de chave. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso.

## Constantes de Tipo de Chave

| Constante | Valor | Descrição |
|-----------|-------|-----------|
| `ADS_RAWKEY` | 0 | Bytes de chave binária bruta. |
| `ADS_STRINGKEY` | 1 | Chave de string de caractere/preenchida com espaços. |
| `ADS_DOUBLEKEY` | 2 | Chave numérica ou de data (codificação FoxNumeric/NtxNumeric de 8 bytes). |

## Descrição

`AdsGetKeyType` inspeciona o `KeyEncoding` do índice ativo
e o mapeia para as constantes de tipo de chave do ACE. Índices
de expressão de caractere retornam `ADS_STRINGKEY`; índices de
expressão numérica e de data retornam `ADS_DOUBLEKEY`.

## Exemplo

```c
ADSHANDLE hIndex;
UNSIGNED16 keyType = 0;
AdsGetIndexHandle(hTable, "amount", &hIndex);
AdsGetKeyType(hIndex, &keyType);
if (keyType == ADS_DOUBLEKEY)
    printf("Numeric index key\n");
else
    printf("Character index key\n");
```

## Ver Também

- [AdsGetKeyLength]({{ site.baseurl }}/pt/funcoes/ads-get-key-length/)
- [AdsGetIndexExpr]({{ site.baseurl }}/pt/funcoes/ads-get-index-expr/)
- [AdsExtractKey]({{ site.baseurl }}/pt/funcoes/ads-extract-key/)

---

[← AdsGetKeyLength]({{ site.baseurl }}/pt/funcoes/ads-get-key-length/)
[AdsGetLastTableUpdate →]({{ site.baseurl }}/pt/funcoes/ads-get-last-table-update/)
