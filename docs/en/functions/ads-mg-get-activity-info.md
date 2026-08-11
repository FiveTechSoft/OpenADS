# [](#adsmggetactivityinfo)AdsMgGetActivityInfo

Returns server activity information.

## [](#syntax)Syntax

```
UNSIGNED32 AdsMgGetActivityInfo(ADSHANDLE hMg, void* pInfo,
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

Pointer to the output `ADS_MGMT_ACTIVITY_INFO` structure.

`pusSize`

`UNSIGNED16*`

Structure size (input) and bytes written (output).

## [](#return-value)Return Value

`AE_SUCCESS` (0) on success. Error code on failure.

## [](#description)Description

`AdsMgGetActivityInfo` returns information about current server activity, including number of operations, logged errors, uptime, and resource usage (users, connections, tables, etc.).

## [](#example)Example

```
ADS_MGMT_ACTIVITY_INFO stInfo;
memset(&stInfo, 0, sizeof(stInfo));
UNSIGNED16 usSize = sizeof(stInfo);
AdsMgGetActivityInfo(hMgmt, &stInfo, &usSize);
printf("Operations: %lu\n", stInfo.ulOperations);
```

## [](#see-also)See Also

-   [AdsMgConnect](/OpenADS/en/functions/ads-mg-connect/)
-   [AdsMgGetCommStats](/OpenADS/en/functions/ads-mg-get-comm-stats/)
-   [AdsMgGetConfigInfo](/OpenADS/en/functions/ads-mg-get-config-info/)

---

[AdsMgGetCommStats →](/OpenADS/en/functions/ads-mg-get-comm-stats/)

---
