---
title: AdsGetNumOpenTables
layout: default
parent: Referencia API
nav_order: 1
permalink: /es/funciones/ads-get-num-open-tables/
---

# AdsGetNumOpenTables

Devuelve el número de tablas abiertas actualmente en el proceso.

## Sintaxis

```c
UNSIGNED32 AdsGetNumOpenTables(UNSIGNED16 *pusNumTables);
```

## Parámetros

| Parámetro | Tipo | Descripción |
|-----------|------|-------------|
| `pusNumTables` | `UNSIGNED16*` | Salida — contador de tablas abiertas. |

## Valor de Retorno

`AE_SUCCESS` (0) en caso de éxito.

## Descripción

`AdsGetNumOpenTables` itera el registro de handles y cuenta
todos los handles de tabla registrados en todos los backends: tablas locales,
tablas remotas, SQLite, PostgreSQL, MariaDB, MSSQL, ODBC y
tablas Firebird.

Esto reemplaza el stub anterior que siempre devolvía 0.

## Ejemplo

```c
UNSIGNED16 numTables = 0;
AdsOpenTable(&hTable1, "customers.dbf", NULL, NULL,
             ADS_ANSI, ADS_SHARED, NULL, NULL);
AdsOpenTable(&hTable2, "orders.dbf", NULL, NULL,
             ADS_ANSI, ADS_SHARED, NULL, NULL);
AdsGetNumOpenTables(&numTables);
printf("Tablas abiertas: %u\n", numTables);  // 2
```

## Ver También

- [AdsOpenTable]({{ site.baseurl }}/es/funciones/ads-open-table/)
- [AdsCloseAllTables]({{ site.baseurl }}/es/funciones/ads-close-all-tables/)
- [AdsGetNumOpenIndexes]({{ site.baseurl }}/es/funciones/ads-get-num-indexes/)

---

[← AdsGetNumLocks]({{ site.baseurl }}/es/funciones/ads-get-num-locks/)
[AdsGetRecord →]({{ site.baseurl }}/es/funciones/ads-get-record/)