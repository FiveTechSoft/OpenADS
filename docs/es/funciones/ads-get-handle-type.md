---
title: AdsGetHandleType
layout: default
parent: Referencia API
nav_order: 1
permalink: /es/funciones/ads-get-handle-type/
---

# AdsGetHandleType

Devuelve el tipo de un handle ADS (tabla, conexión, sentencia, etc.).

## Sintaxis

```c
UNSIGNED32 AdsGetHandleType(ADSHANDLE h, UNSIGNED16 *pusType);
```

## Parámetros

| Parámetro | Tipo | Descripción |
|-----------|------|-------------|
| `h` | `ADSHANDLE` | Cualquier handle ADS válido. |
| `pusType` | `UNSIGNED16*` | Salida — constante del tipo de handle. |

## Valor de Retorno

`AE_SUCCESS` (0) en caso de éxito.

## Constantes de Tipo de Handle

| Constante | Valor | Descripción |
|-----------|-------|-------------|
| `ADS_NONE` | 0 | Handle desconocido / inválido. |
| `ADS_TABLE` | 1 | Handle de tabla (local, remota o backend). |
| `ADS_STATEMENT` | 2 | Handle de sentencia SQL. |
| `ADS_CURSOR` | 4 | Handle de cursor SQL. |
| `ADS_DATABASE_CONNECTION` | 6 | Handle de conexión a base de datos. |
| `ADS_SYS_ADMIN_CONNECTION` | 7 | Conexión de administrador del sistema. |

## Descripción

`AdsGetHandleType` consulta el método `kind_of()` del registro
de handles para determinar el tipo de cualquier handle ADS.
Distingue correctamente entre tablas, conexiones, sentencias e
índices en todos los backends (local, remoto, SQLite, PostgreSQL,
MariaDB, MSSQL, ODBC, Firebird).

Esto reemplaza el stub anterior que siempre devolvía `ADS_TABLE`.

## Ejemplo

```c
ADSHANDLE h;
UNSIGNED16 hType = 0;
AdsConnect60("tcp://server:6247", NULL, NULL, NULL, 0, &h);
AdsGetHandleType(h, &hType);
if (hType == ADS_DATABASE_CONNECTION)
    printf("El handle es una conexión\n");
AdsDisconnect(h);
```

## Ver También

- [AdsGetConnectionType]({{ site.baseurl }}/es/funciones/ads-get-connection-type/)
- [AdsGetTableType]({{ site.baseurl }}/es/funciones/ads-get-table-type/)
- [AdsConnect60]({{ site.baseurl }}/es/funciones/ads-connect60/)

---

[← AdsGetFilter]({{ site.baseurl }}/es/funciones/ads-get-filter/)
[AdsGetIndexCondition →]({{ site.baseurl }}/es/funciones/ads-get-index-condition/)