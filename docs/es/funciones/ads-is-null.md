---
title: AdsIsNull
layout: default
parent: Referencia API
nav_order: 1
permalink: /es/funciones/ads-is-null/
---

# AdsIsNull

Prueba si un campo en el registro actual es NULL.

## Sintaxis

```c
UNSIGNED32 AdsIsNull(ADSHANDLE hTable, UNSIGNED8 *pucField,
                      UNSIGNED16 *pbNull);
```

## Parámetros

| Parámetro | Tipo | Descripción |
|-----------|------|-------------|
| `hTable` | `ADSHANDLE` | Handle de la tabla (local, remota o backend). |
| `pucField` | `UNSIGNED8*` | Nombre del campo (cadena terminada en NUL) u ordinal basado en 1 mediante `ADSFIELD(n)`. |
| `pbNull` | `UNSIGNED16*` | Salida — `1` si el campo es NULL, `0` en caso contrario. |

## Valor de Retorno

`AE_SUCCESS` (0) en caso de éxito. Código de error distinto de cero en caso de fallo
(handle inválido, campo no encontrado).

## Descripción

`AdsIsNull` inspecciona el bitmap NULL del registro actual para
determinar si el campo especificado es NULL. El bitmap NULL solo
está presente en tablas ADT; para tablas CDX/NTX siempre está
ausente, por lo que la función siempre reporta "no nulo" (0).

Para tablas remotas y backend, la función reporta de forma
conservadora "no nulo" ya que la capacidad de NULL aún no se
expone a través del protocolo de red.

## Ejemplo

```c
ADSHANDLE hTable;
UNSIGNED16 isNull = 0;
AdsOpenTable(&hTable, "customers.adt", NULL, NULL,
             ADS_ANSI, ADS_EXCLUSIVE, NULL, NULL);
AdsGotoTop(hTable);
AdsIsNull(hTable, (UNSIGNED8*)"email", &isNull);
if (isNull)
    printf("email es NULL\n");
else
    printf("email tiene un valor\n");
AdsCloseTable(hTable);
```

## Ver También

- [AdsGetField]({{ site.baseurl }}/es/funciones/ads-get-field/)
- [AdsSetNull]({{ site.baseurl }}/es/funciones/ads-set-null/)
- [AdsSetEmpty]({{ site.baseurl }}/es/funciones/ads-set-empty/)

---

[← AdsIsIndexUnique]({{ site.baseurl }}/es/funciones/ads-is-index-unique/)
[AdsIsRecordInAOF →]({{ site.baseurl }}/es/funciones/ads-is-record-in-aof/)