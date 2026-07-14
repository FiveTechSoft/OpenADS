---
title: AdsCacheRecords
layout: default
parent: Referencia API
nav_order: 1
permalink: /es/funciones/ads-cache-records/
---

# AdsCacheRecords

Sugiere cuántos registros leer por adelantado para una tabla.

## Sintaxis

```c
UNSIGNED32 AdsCacheRecords(ADSHANDLE hTable, UNSIGNED16 usNumRecords);
```

## Parámetros

| Parámetro | Tipo | Descripción |
|-----------|------|-------------|
| `hTable` | `ADSHANDLE` | Handle de la tabla. |
| `usNumRecords` | `UNSIGNED16` | Número sugerido de registros a leer por adelantado. |

## Valor de Retorno

`AE_SUCCESS` (0) en caso de éxito. `AE_INTERNAL_ERROR` (5000) si el handle es desconocido.

## Descripción

Define cuántos registros lee por anticipado un `AdsSkip` hacia adelante en una conexión remota.

**Normalmente no necesita llamar a esta función.** Por omisión, OpenADS elige la profundidad por sí mismo: un `AdsSkip` hacia adelante devuelve un bloque de filas anticipadas junto con la actual, y el cliente sirve los siguientes skips desde ese bloque sin ningún viaje a la red. El servidor aumenta la profundidad al detectar un recorrido secuencial y la reduce ante cualquier reposicionamiento, escritura o cambio de orden: así un browse obtiene un bloque profundo, mientras que un «buscar un registro y leerlo» no paga por filas que nunca se van a mirar. (SAP usa un valor fijo de diez registros; OpenADS se adapta.)

Llame a `AdsCacheRecords` cuando sepa algo sobre su patrón de acceso que el servidor no pueda deducir:

| `usNumRecords` | Efecto |
|-----------|--------|
| `0` o `1` | **Desactiva** la lectura anticipada para esta tabla. |
| `N` | Lee exactamente `N` registros por skip, ignorando el ajuste automático. |

Desactivarla es lo correcto en un bucle por lotes que edita casi todos los registros que visita: cada escritura descarta el bloque de todos modos, así que leer por anticipado es puro desperdicio. Subirla (el valor «agresivo» de SAP es 100) conviene a un recorrido unidireccional que lee gran parte de una tabla sin editarla. SAP indica que valores por encima de 100 no suelen aportar nada; OpenADS limita cada bloque a 512 registros en cualquier caso, para que una petición no se convierta en un recorrido ilimitado en el servidor.

La profundidad es un techo, no una promesa: el bloque también está limitado por un presupuesto de bytes, de modo que una tabla con registros anchos devuelve menos filas de las pedidas en lugar de una trama de red desmesurada.

La lectura anticipada nunca sirve datos obsoletos causados por usted: el bloque se descarta ante cualquier escritura, búsqueda, cambio de orden o `AdsRefreshRecord`. Igual que en SAP, las filas ya presentes en el bloque **no** reflejan cambios hechos simultáneamente por *otros* usuarios; llame a [AdsRefreshRecord]({{ site.baseurl }}/es/funciones/ads-refresh-record/) para forzar una relectura.

## Ejemplo

```c
AdsCacheRecords(hTable, 50);
```

## Ver También

- [AdsCacheOpenTables]({{ site.baseurl }}/es/funciones/ads-cache-open-tables/)
- [AdsCacheOpenCursors]({{ site.baseurl }}/es/funciones/ads-cache-open-cursors/)

---

[AdsCacheOpenTables →]({{ site.baseurl }}/es/funciones/ads-cache-open-tables/)
