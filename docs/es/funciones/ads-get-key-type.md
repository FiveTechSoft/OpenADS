---
title: AdsGetKeyType
layout: default
parent: Referencia API
nav_order: 1
permalink: /es/funciones/ads-get-key-type/
---

# AdsGetKeyType

Devuelve el tipo de codificación de la clave de índice para el orden activo.

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
| `ADS_RAWKEY` | 0 | Bytes de clave binaria sin procesar. |
| `ADS_STRINGKEY` | 1 | Clave de cadena de caracteres / rellenada con espacios. |
| `ADS_DOUBLEKEY` | 2 | Clave numérica o de fecha (codificación de 8 bytes FoxNumeric / NtxNumeric). |

## Descripción

`AdsGetKeyType` inspecciona el `KeyEncoding` del índice activo
y lo mapea a las constantes de tipo de clave ACE. Los índices de
expresión de caracteres devuelven `ADS_STRINGKEY`; los índices de
expresión numérica y de fecha devuelven `ADS_DOUBLEKEY`.

## Ejemplo

```c
ADSHANDLE hIndex;
UNSIGNED16 keyType = 0;
AdsGetIndexHandle(hTable, "amount", &hIndex);
AdsGetKeyType(hIndex, &keyType);
if (keyType == ADS_DOUBLEKEY)
    printf("Clave de índice numérica\n");
else
    printf("Clave de índice de caracteres\n");
```

## Ver También

- [AdsGetKeyLength]({{ site.baseurl }}/es/funciones/ads-get-key-length/)
- [AdsGetIndexExpr]({{ site.baseurl }}/es/funciones/ads-get-index-expr/)
- [AdsExtractKey]({{ site.baseurl }}/es/funciones/ads-extract-key/)

---

[← AdsGetKeyLength]({{ site.baseurl }}/es/funciones/ads-get-key-length/)
[AdsGetLastTableUpdate →]({{ site.baseurl }}/es/funciones/ads-get-last-table-update/)