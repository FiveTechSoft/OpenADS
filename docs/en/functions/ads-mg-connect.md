# [](#adsmgconnect)AdsMgConnect

Establishes a management connection to an Advantage server.

## [](#syntax)Syntax

```
UNSIGNED32 AdsMgConnect(UNSIGNED8* pucServer, UNSIGNED8* pucUser,
                        UNSIGNED8* pucPwd, ADSHANDLE* phMg);
```

## [](#parameters)Parameters

Parameter

Type

Description

`pucServer`

`UNSIGNED8*`

Server name or address.

`pucUser`

`UNSIGNED8*`

User name.

`pucPwd`

`UNSIGNED8*`

User password.

`phMg`

`ADSHANDLE*`

Pointer to the output management connection handle.

## [](#return-value)Return Value

`AE_SUCCESS` (0) on success. Error code on failure.

## [](#description)Description

`AdsMgConnect` establishes a dedicated management connection to an Advantage server. This connection is used for server monitoring and administration functions.

## [](#example)Example

```
ADSHANDLE hMgmt;
AdsMgConnect("myserver", "admin", "password", &hMgmt);
```

## [](#see-also)See Also

-   [AdsMgDisconnect](/OpenADS/en/functions/ads-mg-disconnect/)
-   [AdsMgGetActivityInfo](/OpenADS/en/functions/ads-mg-get-activity-info/)
-   [AdsMgGetServerType](/OpenADS/en/functions/ads-mg-get-server-type/)

---

[AdsMgDisconnect →](/OpenADS/en/functions/ads-mg-disconnect/)

---
