---
title: AdsGetNumOpenTables
layout: default
parent: Referência da API
nav_order: 1
permalink: /pt/funcoes/ads-get-num-open-tables/
---

# AdsGetNumOpenTables

Retorna o número de tabelas atualmente abertas no processo.

## Sintaxe

```c
UNSIGNED32 AdsGetNumOpenTables(UNSIGNED16 *pusNumTables);
```

## Parâmetros

| Parâmetro | Tipo | Descrição |
|-----------|------|-----------|
| `pusNumTables` | `UNSIGNED16*` | Saída — contagem de tabelas abertas. |

## Valor de Retorno

`AE_SUCCESS` (0) em caso de sucesso.

## Descrição

`AdsGetNumOpenTables` itera o registro de handles e conta
todos os handles de tabela registrados em todos os backends: tabelas
locais, tabelas remotas, SQLite, PostgreSQL, MariaDB, MSSQL, ODBC e
tabelas Firebird.

Isso substitui o stub anterior que sempre retornava 0.

## Exemplo

```c
UNSIGNED16 numTables = 0;
AdsOpenTable(&hTable1, "customers.dbf", NULL, NULL,
             ADS_ANSI, ADS_SHARED, NULL, NULL);
AdsOpenTable(&hTable2, "orders.dbf", NULL, NULL,
             ADS_ANSI, ADS_SHARED, NULL, NULL);
AdsGetNumOpenTables(&numTables);
printf("Open tables: %u\n", numTables);  // 2
```

## Ver Também

- [AdsOpenTable]({{ site.baseurl }}/pt/funcoes/ads-open-table/)
- [AdsCloseAllTables]({{ site.baseurl }}/pt/funcoes/ads-close-all-tables/)
- [AdsGetNumOpenIndexes]({{ site.baseurl }}/pt/funcoes/ads-get-num-indexes/)

---

[← AdsGetNumLocks]({{ site.baseurl }}/pt/funcoes/ads-get-num-locks/)
[AdsGetRecord →]({{ site.baseurl }}/pt/funcoes/ads-get-record/)
