---
title: AdsGetKeyLength
layout: default
parent: Referencia API
nav_order: 1
permalink: /es/funciones/ads-get-key-length/
---

# AdsGetKeyLength

Devuelve la longitud en bytes de la clave de índice para el orden activo.

## Sintaxis

```c
UNSIGNED32 AdsGetKeyLength(ADSHANDLE hIndex, UNSIGNED16 *pusKeyLen);
```

## Parámetros

| Parámetro | Tipo | Descripción |
|-----------|------|-------------|
| `hIndex` | `ADSHANDLE` | Handle del orden de índice. |
| `pusKeyLen` | `UNSIGNED16*` | Salida — longitud de la clave en bytes. |

## Valor de Retorno

`AE_SUCCESS` (0) en caso de éxito. Código de error distinto de cero si el handle
no se resuelve en un índice activo.

## Descripción

`AdsGetKeyLength` devuelve el ancho de una entrada de clave individual en el
índice B+tree activo. La longitud de la clave se determina en el momento de la
creación del índice a partir de la expresión y los tipos de campo. Para claves
de caracteres, esto es típicamente la suma de los anchos de los campos; para
claves numéricas/fechas, son 8 bytes (codificación FoxNumeric).

## Ejemplo

```c
ADSHANDLE hIndex;
UNSIGNED16 keyLen = 0;
AdsGetIndexHandle(hTable, "lastname", &hIndex);
AdsGetKeyLength(hIndex, &keyLen);
printf("Longitud de clave: %u bytes\n", keyLen);
```

## Ver También

- [AdsGetKeyType]({{ site.baseurl }}/es/funciones/ads-get-key-type/)
- [AdsGetIndexExpr]({{ site.baseurl }}/es/funciones/ads-get-index-expr/)
- [AdsExtractKey]({{ site.baseurl }}/es/funciones/ads-extract-key/)

---

[← AdsGetKeyNum]({{ site.baseurl }}/es/funciones/ads-get-key-num/)
[AdsGetKeyType →]({{ site.baseurl }}/es/funciones/ads-get-key-type/)