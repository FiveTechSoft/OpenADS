---
title: AdsGetIndexCondition
layout: default
parent: Referencia API
nav_order: 1
permalink: /es/funciones/ads-get-index-condition/
---

# AdsGetIndexCondition

Devuelve la expresión de condición FOR de un orden de índice.

## Sintaxis

```c
UNSIGNED32 AdsGetIndexCondition(ADSHANDLE hIndex, UNSIGNED8 *pucBuf,
                                UNSIGNED16 *pusBufLen);
```

## Parámetros

| Parámetro | Tipo | Descripción |
|-----------|------|-------------|
| `hIndex` | `ADSHANDLE` | Handle del orden de índice. |
| `pucBuf` | `UNSIGNED8*` | Buffer de salida para la cadena de condición. |
| `pusBufLen` | `UNSIGNED16*` | Entrada/salida — capacidad del buffer en la entrada, longitud real en la salida. |

## Valor de Retorno

`AE_SUCCESS` (0) en caso de éxito. Devuelve una cadena vacía si el
índice no tiene condición FOR.

## Descripción

`AdsGetIndexCondition` recupera la expresión condicional (FOR)
asociada con una etiqueta de índice. No todos los índices tienen una
condición FOR; cuando está ausente, la función devuelve una cadena vacía con
`*pusBufLen = 0`.

Esta función sigue el mismo patrón de resolución de handle que
[AdsGetIndexExpr]({{ site.baseurl }}/es/funciones/ads-get-index-expr/):
primero verifica el binding estacionado, luego el IIndex del orden activo.

## Ejemplo

```c
ADSHANDLE hIndex;
char cond[256];
UNSIGNED16 len = sizeof(cond);
AdsGetIndexHandle(hTable, "active_cust", &hIndex);
AdsGetIndexCondition(hIndex, (UNSIGNED8*)cond, &len);
if (len > 0)
    printf("Condición FOR: %s\n", cond);
else
    printf("Sin condición FOR\n");
```

## Ver También

- [AdsGetIndexExpr]({{ site.baseurl }}/es/funciones/ads-get-index-expr/)
- [AdsGetIndexFilename]({{ site.baseurl }}/es/funciones/ads-get-index-filename/)
- [AdsOpenIndex]({{ site.baseurl }}/es/funciones/ads-open-index/)

---

[← AdsGetHandleType]({{ site.baseurl }}/es/funciones/ads-get-handle-type/)
[AdsGetIndexFilename →]({{ site.baseurl }}/es/funciones/ads-get-index-filename/)