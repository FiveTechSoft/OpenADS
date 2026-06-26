---
title: AdsGetConnectionType
layout: default
parent: API Reference
nav_order: 1
permalink: /en/functions/ads-get-connection-type/
---

# AdsGetConnectionType

Returns whether a connection is local or remote.

## Syntax

```c
UNSIGNED32 AdsGetConnectionType(ADSHANDLE hConnect, UNSIGNED16 *pusType);
```

## Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `hConnect` | `ADSHANDLE` | Handle of the connection or any object derived from it (table, index). |
| `pusType` | `UNSIGNED16*` | Output — connection type constant. |

## Return Value

`AE_SUCCESS` (0) on success.

## Connection Type Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `ADS_LOCAL_SERVER` | 0 | Local (in-process) connection. |
| `ADS_REMOTE_SERVER` | 1 | Remote TCP/TLS connection. |

## Description

`AdsGetConnectionType` determines whether the given handle
resolves to a local engine connection or a remote TCP connection.
It checks for a remote-table handle first; if found, reports
`ADS_REMOTE_SERVER`. Otherwise it falls back to the local
connection registry and reports `ADS_LOCAL_SERVER`.

You can pass a table handle, index handle, or connection handle —
the function resolves through the handle registry to determine
the connection type.

## Example

```c
ADSHANDLE hConn;
UNSIGNED16 connType = 0;
AdsConnect60("tcp://server:6247", NULL, NULL, NULL, 0, &hConn);
AdsGetConnectionType(hConn, &connType);
if (connType == ADS_REMOTE_SERVER)
    printf("Connected to remote server\n");
else
    printf("Local connection\n");
AdsDisconnect(hConn);
```

## See Also

- [AdsConnect60]({{ site.baseurl }}/en/functions/ads-connect60/)
- [AdsIsConnectionAlive]({{ site.baseurl }}/en/functions/ads-is-connection-alive/)
- [AdsGetHandleType]({{ site.baseurl }}/en/functions/ads-get-handle-type/)

---

[← AdsGetDateFormat]({{ site.baseurl }}/en/functions/ads-get-date-format/)
[AdsGetDefault →]({{ site.baseurl }}/en/functions/ads-get-default/)
