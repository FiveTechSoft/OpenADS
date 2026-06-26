---
title: AdsGetTableConType
layout: default
parent: Referencia API
nav_order: 1
permalink: /es/funciones/ads-get-table-con-type/
---

# AdsGetTableConType

Devuelve el tipo de tabla (CDX, NTX, ADT, etc.).

## Sintaxis

```c
UNSIGNED32 AdsGetTableConType(ADSHANDLE hTable, UNSIGNED16 *pusType);
```

## Parámetros

| Parámetro | Tipo | Descripción |
|-----------|------|-------------|
| `hTable` | `ADSHANDLE` | Handle de la tabla. |
| `pusType` | `UNSIGNED16*` | Salida — constante del tipo de tabla. |

## Valor de Retorno

`AE_SUCCESS` (0) en caso de éxito.

## Constantes de Tipo de Tabla

| Constante | Valor | Descripción |
|-----------|-------|-------------|
| `ADS_CDX` | 1 | Índice compuesto CDX (FoxPro/Harbour). |
| `ADS_NTX` | 2 | Índice NTX (Clipper). |
| `ADS_ADT` | 5 | Tabla ADT (nativa de Advantage). |

## Descripción

`AdsGetTableConType` delega a `AdsGetTableType` que
deriva el tipo de tabla a partir de la extensión del archivo (`.dbf` → CDX,
`.adt` → ADT). Esto reemplaza el stub anterior que siempre
devolvía `ADS_CDX`.

## Ejemplo

```c
ADSHANDLE hTable;
UNSIGNED16 tableType = 0;
AdsOpenTable(&hTable, "data.adt", NULL, NULL,
             ADS_ANSI, ADS_EXCLUSIVE, NULL, NULL);
AdsGetTableConType(hTable, &tableType);
if (tableType == ADS_ADT)
    printf("Tabla ADT\n");
else
    printf("Tabla DBF/CDX\n");
AdsCloseTable(hTable);
```

## Ver También

- [AdsGetTableType]({{ site.baseurl }}/es/funciones/ads-get-table-type/)
- [AdsOpenTable]({{ site.baseurl }}/es/funciones/ads-open-table/)
- [AdsCreateTable]({{ site.baseurl }}/es/funciones/ads-create-table/)

---

[← AdsGetTableCharType]({{ site.baseurl }}/es/funciones/ads-get-table-char-type/)
[AdsGetTableConnection →]({{ site.baseurl }}/es/funciones/ads-get-table-connection/)