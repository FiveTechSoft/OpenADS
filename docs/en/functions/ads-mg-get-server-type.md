# [](#adsmggetservertype)AdsMgGetServerType

Returns the Advantage server type.

## [](#syntax)Syntax

```
UNSIGNED32 AdsMgGetServerType(ADSHANDLE hMg, UNSIGNED16* pusT);
```

## [](#parameters)Parameters

Parameter

Type

Description

`hMg`

`ADSHANDLE`

Management connection handle.

`pusT`

`UNSIGNED16*`

Pointer to the variable that receives the server type.

## [](#return-value)Return Value

`AE_SUCCESS` (0) on success. Error code on failure.

## [](#description)Description

`AdsMgGetServerType` returns the type of Advantage server to which the management connection is connected. The returned values are:

-   `ADS_AIS_SERVER` (1) — Advantage Internet Server
-   `ADS_LINUX` (2) — Advantage server on Linux

## [](#example)Example

```
UNSIGNED16 usType;
AdsMgGetServerType(hMgmt, &usType);
if (usType == ADS_AIS_SERVER) {
    // AIS server (Windows)
}
```

## [](#see-also)See Also

-   [AdsMgConnect](/OpenADS/en/functions/ads-mg-connect/)
-   [AdsGetConnectionType](/OpenADS/en/functions/ads-get-connection-type/)
-   [AdsGetServerName](/OpenADS/en/functions/ads-get-server-name/)

---

[AdsMgGetUserNames →](/OpenADS/en/functions/ads-mg-get-user-names/)

---
