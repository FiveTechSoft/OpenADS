---
title: AdsGetKeyType
layout: default
parent: Referencia API
nav_order: 1
permalink: /es/funciones/ads-get-key-type/
---

# AdsGetKeyType

Devuelve el tipo de datos de la expresión de clave del índice.

## Sintaxis

```c
UNSIGNED32 AdsGetKeyType(ADSHANDLE hIndex, UNSIGNED16 *pusKeyType);
```

## Parámetros

| Parámetro | Tipo | Descripción |
|-----------|------|-------------|
| `hIndex` | `ADSHANDLE` | Handle del orden de índice. |
| `pusKeyType` | `UNSIGNED16*` | Salida — constante del tipo de clave. |

## Valor de Retorno

`AE_SUCCESS` (0) en caso de éxito.

## Constantes de Tipo de Clave

| Constante | Valor | Descripción |
|-----------|-------|-------------|
| `ADS_LOGICAL` | 1 | Expresión de clave lógica. |
| `ADS_NUMERIC` | 2 | Expresión de clave numérica. |
| `ADS_DATE` | 3 | Expresión de clave de fecha. |
| `ADS_STRING` | 4 | Expresión de clave de caracteres. |
| `ADS_RAW` | 16 | Expresión de clave raw (concatenación `;` de ADT). |

Nótese que son las constantes de *tipo de campo*, no las constantes
de codificación de buffer (`ADS_STRINGKEY` / `ADS_DOUBLEKEY` /
`ADS_RAWKEY`) que usan `AdsSeek` y `AdsSetScope`.

## Descripción

`AdsGetKeyType` informa el tipo del resultado de la expresión de
clave del índice. Una clave de campo simple responde según el esquema
de la tabla (un campo `C` devuelve `ADS_STRING`, un campo
`N`/`F`/`I`/`B`/`Y` devuelve `ADS_NUMERIC`, un campo `D` devuelve
`ADS_DATE`, un campo `L` devuelve `ADS_LOGICAL`). Las expresiones
calculadas responden según su tipo de resultado: las expresiones de
cadena como `UPPER(name)` devuelven `ADS_STRING`; las numéricas como
`Val(code)` devuelven `ADS_NUMERIC`.

Los bindings de lenguaje dependen de este valor para codificar claves
de scope y seek — el rddads de Harbour, por ejemplo, elige la
codificación de clave de `OrdScope()` a partir de él.

## Ejemplo

```c
ADSHANDLE hIndex;
UNSIGNED16 keyType = 0;
AdsGetIndexHandle(hTable, "amount", &hIndex);
AdsGetKeyType(hIndex, &keyType);
if (keyType == ADS_NUMERIC)
    printf("Clave de índice numérica\n");
else if (keyType == ADS_STRING)
    printf("Clave de índice de caracteres\n");
```

## Ver También

- [AdsGetKeyLength]({{ site.baseurl }}/es/funciones/ads-get-key-length/)
- [AdsGetIndexExpr]({{ site.baseurl }}/es/funciones/ads-get-index-expr/)
- [AdsExtractKey]({{ site.baseurl }}/es/funciones/ads-extract-key/)

---

[← AdsGetKeyLength]({{ site.baseurl }}/es/funciones/ads-get-key-length/)
[AdsGetLastTableUpdate →]({{ site.baseurl }}/es/funciones/ads-get-last-table-update/)