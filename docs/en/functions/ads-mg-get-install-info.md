# [](#adsmggetinstallinfo)AdsMgGetInstallInfo

Returns server installation information.

## [](#syntax)Syntax

```
UNSIGNED32 AdsMgGetInstallInfo(ADSHANDLE hMg, void* pInfo,
                               UNSIGNED16* pusSize);
```

## [](#parameters)Parameters

Parameter

Type

Description

`hMg`

`ADSHANDLE`

Management connection handle.

`pInfo`

`void*`

Pointer to the output `ADS_MGMT_INSTALL_INFO` structure.

`pusSize`

`UNSIGNED16*`

Structure size (input) and bytes written (output).

## [](#return-value)Return Value

`AE_SUCCESS` (0) on success. Error code on failure.

## [](#description)Description

`AdsMgGetInstallInfo` returns information about the Advantage server installation, including registered owner, version, installation date, character set, evaluation expiration date, and serial number.

## [](#example)Example

```
ADS_MGMT_INSTALL_INFO stInfo;
memset(&stInfo, 0, sizeof(stInfo));
UNSIGNED16 usSize = sizeof(stInfo);
AdsMgGetInstallInfo(hMgmt, &stInfo, &usSize);
printf("Version: %s\n", stInfo.aucVersionStr);
```

## [](#see-also)See Also

-   [AdsMgGetActivityInfo](/OpenADS/en/functions/ads-mg-get-activity-info/)
-   [AdsMgGetConfigInfo](/OpenADS/en/functions/ads-mg-get-config-info/)
-   [AdsGetVersion](/OpenADS/en/functions/ads-get-version/)

---

[AdsMgGetLockOwner →](/OpenADS/en/functions/ads-mg-get-lock-owner/)

---
