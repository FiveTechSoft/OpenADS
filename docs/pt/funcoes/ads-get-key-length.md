---
title: AdsGetKeyLength
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-key-length/
---

# AdsGetKeyLength

Retorna o comprimento em bytes da chave de índice para a ordem ativa.

## Sintaxe

```c
UNSIGNED32 AdsGetKeyLength(ADSHANDLE hIndex, UNSIGNED16 *pusKeyLen);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hIndex` | `ADSHANDLE` | Handle da ordem de índice. |
| `pusKeyLen` | `UNSIGNED16*` | Saída — comprimento da chave em bytes. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso. Código de erro diferente de zero se o handle
não resolver para um índice ativo.

## Descrição

`AdsGetKeyLength` retorna a largura de uma única entrada de chave no
índice B+tree ativo. O comprimento da chave é determinado no momento da
criação do índice a partir da expressão e dos tipos de campo. Para chaves
de caractere, isso é tipicamente a soma das larguras dos campos; para chaves
numéricas/data, são 8 bytes (codificação FoxNumeric).

## Exemplo

```c
ADSHANDLE hIndex;
UNSIGNED16 keyLen = 0;
AdsGetIndexHandle(hTable, "lastname", &hIndex);
AdsGetKeyLength(hIndex, &keyLen);
printf("Key length: %u bytes\n", keyLen);
```

## Ver Também

- [AdsGetKeyType]({{ site.baseurl }}/pt/funcoes/ads-get-key-type/)
- [AdsGetIndexExpr]({{ site.baseurl }}/pt/funcoes/ads-get-index-expr/)
- [AdsExtractKey]({{ site.baseurl }}/pt/funcoes/ads-extract-key/)

---

[← AdsGetKeyNum]({{ site.baseurl }}/pt/funcoes/ads-get-key-num/)
[AdsGetKeyType →]({{ site.baseurl }}/pt/funcoes/ads-get-key-type/)
