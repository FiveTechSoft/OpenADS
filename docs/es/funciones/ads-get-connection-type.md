---
title: AdsGetConnectionType
layout: default
parent: Referencia API
nav_order: 1
permalink: /es/funciones/ads-get-connection-type/
---

# AdsGetConnectionType

Devuelve si una conexión es local o remota.

## Sintaxis

```c
UNSIGNED32 AdsGetConnectionType(ADSHANDLE hConnect, UNSIGNED16 *pusType);
```

## Parámetros

| Parámetro | Tipo | Descripción |
|-----------|------|-------------|
| `hConnect` | `ADSHANDLE` | Handle de la conexión o cualquier objeto derivado de ella (tabla, índice). |
| `pusType` | `UNSIGNED16*` | Salida — constante del tipo de conexión. |

## Valor de Retorno

`AE_SUCCESS` (0) en caso de éxito.

## Constantes de Tipo de Conexión

| Constante | Valor | Descripción |
|-----------|-------|-------------|
| `ADS_LOCAL_SERVER` | 0 | Conexión local (dentro del proceso). |
| `ADS_REMOTE_SERVER` | 1 | Conexión remota TCP/TLS. |

## Descripción

`AdsGetConnectionType` determina si el handle dado se resuelve
en una conexión de motor local o una conexión TCP remota.
Primero verifica si es un handle de tabla remota; si lo encuentra,
reporta `ADS_REMOTE_SERVER`. De lo contrario, recurre al registro
de conexiones locales y reporta `ADS_LOCAL_SERVER`.

Puede pasar un handle de tabla, handle de índice o handle de conexión —
la función resuelve a través del registro de handles para determinar
el tipo de conexión.

## Ejemplo

```c
ADSHANDLE hConn;
UNSIGNED16 connType = 0;
AdsConnect60("tcp://server:6247", NULL, NULL, NULL, 0, &hConn);
AdsGetConnectionType(hConn, &connType);
if (connType == ADS_REMOTE_SERVER)
    printf("Conectado a servidor remoto\n");
else
    printf("Conexión local\n");
AdsDisconnect(hConn);
```

## Ver También

- [AdsConnect60]({{ site.baseurl }}/es/funciones/ads-connect60/)
- [AdsIsConnectionAlive]({{ site.baseurl }}/es/funciones/ads-is-connection-alive/)
- [AdsGetHandleType]({{ site.baseurl }}/es/funciones/ads-get-handle-type/)

---

[← AdsGetDateFormat]({{ site.baseurl }}/es/funciones/ads-get-date-format/)
[AdsGetDefault →]({{ site.baseurl }}/es/funciones/ads-get-default/)