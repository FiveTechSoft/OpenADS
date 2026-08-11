# [](#adsmggetconfiginfo)AdsMgGetConfigInfo

Returns server configuration information.

## [](#syntax)Syntax

```
UNSIGNED32 AdsMgGetConfigInfo(ADSHANDLE hMg, void* pVals,
                              UNSIGNED16* pusValsSize, void* pMem,
                              UNSIGNED16* pusMemSize);
```

## [](#parameters)Parameters

Parameter

Type

Description

`hMg`

`ADSHANDLE`

Management connection handle.

`pVals`

`void*`

Pointer to the output `ADS_MGMT_CONFIG_PARAMS` structure.

`pusValsSize`

`UNSIGNED16*`

Parameter structure size (input) and bytes written (output).

`pMem`

`void*`

Pointer to the output `ADS_MGMT_CONFIG_MEMORY` structure.

`pusMemSize`

`UNSIGNED16*`

Memory structure size (input) and bytes written (output).

## [](#return-value)Return Value

`AE_SUCCESS` (0) on success. Error code on failure.

## [](#description)Description

`AdsMgGetConfigInfo` returns Advantage server configuration information, including resource limits (connections, workspaces, tables, etc.) and memory usage.

## [](#example)Example

```
ADS_MGMT_CONFIG_PARAMS stVals;
ADS_MGMT_CONFIG_MEMORY stMem;
memset(&stVals, 0, sizeof(stVals));
memset(&stMem, 0, sizeof(stMem));
UNSIGNED16 usValsSize = sizeof(stVals);
UNSIGNED16 usMemSize = sizeof(stMem);
AdsMgGetConfigInfo(hMgmt, &stVals, &usValsSize, &stMem, &usMemSize);
```

## [](#see-also)See Also

-   [AdsMgGetActivityInfo](/OpenADS/en/functions/ads-mg-get-activity-info/)
-   [AdsMgGetInstallInfo](/OpenADS/en/functions/ads-mg-get-install-info/)
-   [AdsMgGetCommStats](/OpenADS/en/functions/ads-mg-get-comm-stats/)

---

[AdsMgGetInstallInfo →](/OpenADS/en/functions/ads-mg-get-install-info/)

---
