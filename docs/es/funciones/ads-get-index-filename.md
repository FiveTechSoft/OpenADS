---
title: AdsGetIndexFilename
layout: default
parent: Referencia API
nav_order: 1
permalink: /es/funciones/ads-get-index-filename/
---

# AdsGetIndexFilename

Devuelve la ruta del archivo de índice para un orden dado.

## Sintaxis

```c
UNSIGNED32 AdsGetIndexFilename(ADSHANDLE hIndex, UNSIGNED16 usOrder,
                                UNSIGNED8 *pucBuf, UNSIGNED16 *pusBufLen);
```

## Parámetros

| Parámetro | Tipo | Descripción |
|-----------|------|-------------|
| `hIndex` | `ADSHANDLE` | Handle del orden de índice. |
| `usOrder` | `UNSIGNED16` | Reservado, pasar 0. |
| `pucBuf` | `UNSIGNED8*` | Buffer de salida para la ruta del archivo. |
| `pusBufLen` | `UNSIGNED16*` | Entrada/salida — capacidad del buffer en la entrada, longitud real en la salida. |

## Valor de Retorno

`AE_SUCCESS` (0) en caso de éxito.

## Descripción

`AdsGetIndexFilename` devuelve la ruta resuelta del sistema de archivos del
archivo `.cdx` / `.ntx` / `.adi` que contiene el orden de índice dado. La ruta
se almacena en el `IndexBinding` en el momento del registro del handle y siempre
es precisa tanto para índices activos como estacionados.

## Ejemplo

```c
ADSHANDLE hIndex;
char path[260];
UNSIGNED16 len = sizeof(path);
AdsGetIndexHandle(hTable, "lastname", &hIndex);
AdsGetIndexFilename(hIndex, 0, (UNSIGNED8*)path, &len);
printf("Archivo de índice: %s\n", path);
```

## Ver También

- [AdsGetIndexExpr]({{ site.baseurl }}/es/funciones/ads-get-index-expr/)
- [AdsGetIndexCondition]({{ site.baseurl }}/es/funciones/ads-get-index-condition/)
- [AdsOpenIndex]({{ site.baseurl }}/es/funciones/ads-open-index/)

---

[← AdsGetIndexCondition]({{ site.baseurl }}/es/funciones/ads-get-index-condition/)
[AdsGetIndexOrderByHandle →]({{ site.baseurl }}/es/funciones/ads-get-index-order-by-handle/)