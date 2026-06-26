---
title: AdsGetTableConType
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-table-con-type/
---

# AdsGetTableConType

Retorna o tipo de tabela (CDX, NTX, ADT, etc.).

## Sintaxe

```c
UNSIGNED32 AdsGetTableConType(ADSHANDLE hTable, UNSIGNED16 *pusType);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `hTable` | `ADSHANDLE` | Handle da tabela. |
| `pusType` | `UNSIGNED16*` | Saída — constante do tipo de tabela. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso.

## Constantes de Tipo de Tabela

| Constante | Valor | Descrição |
|-----------|-------|-----------|
| `ADS_CDX` | 1 | Índice composto CDX (FoxPro/Harbour). |
| `ADS_NTX` | 2 | Índice NTX (Clipper). |
| `ADS_ADT` | 5 | Tabela ADT (Advantage nativa). |

## Descrição

`AdsGetTableConType` delega para `AdsGetTableType` que
deriva o tipo de tabela a partir da extensão do arquivo (`.dbf` → CDX,
`.adt` → ADT). Isso substitui o stub anterior que sempre
retornava `ADS_CDX`.

## Exemplo

```c
ADSHANDLE hTable;
UNSIGNED16 tableType = 0;
AdsOpenTable(&hTable, "data.adt", NULL, NULL,
             ADS_ANSI, ADS_EXCLUSIVE, NULL, NULL);
AdsGetTableConType(hTable, &tableType);
if (tableType == ADS_ADT)
    printf("ADT table\n");
else
    printf("DBF/CDX table\n");
AdsCloseTable(hTable);
```

## Ver Também

- [AdsGetTableType]({{ site.baseurl }}/pt/funcoes/ads-get-table-type/)
- [AdsOpenTable]({{ site.baseurl }}/pt/funcoes/ads-open-table/)
- [AdsCreateTable]({{ site.baseurl }}/pt/funcoes/ads-create-table/)

---

[← AdsGetTableCharType]({{ site.baseurl }}/pt/funcoes/ads-get-table-char-type/)
[AdsGetTableConnection →]({{ site.baseurl }}/pt/funcoes/ads-get-table-connection/)
