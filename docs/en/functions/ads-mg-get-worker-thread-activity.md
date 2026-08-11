# [](#adsmggetworkerthreadactivity)AdsMgGetWorkerThreadActivity

Returns server worker thread activity.

## [](#syntax)Syntax

```
UNSIGNED32 AdsMgGetWorkerThreadActivity(ADSHANDLE hMg, void* pInfo,
                                        UNSIGNED16* pusCount,
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

Output array of `ADS_MGMT_THREAD_ACTIVITY` structures.

`pusCount`

`UNSIGNED16*`

Number of threads returned.

`pusSize`

`UNSIGNED16*`

Total size of returned data in bytes.

## [](#return-value)Return Value

`AE_SUCCESS` (0) on success. Error code on failure.

## [](#description)Description

`AdsMgGetWorkerThreadActivity` returns information about the activity of the server's worker threads. Each entry includes the thread number, operation code, user name, and connection number.

## [](#example)Example

```
ADS_MGMT_THREAD_ACTIVITY astThreads[20];
memset(astThreads, 0, sizeof(astThreads));
UNSIGNED16 usCount = 20;
UNSIGNED16 usSize = sizeof(astThreads);
AdsMgGetWorkerThreadActivity(hMgmt, astThreads, &usCount, &usSize);
```

## [](#see-also)See Also

-   [AdsMgGetActivityInfo](/OpenADS/en/functions/ads-mg-get-activity-info/)
-   [AdsMgGetConfigInfo](/OpenADS/en/functions/ads-mg-get-config-info/)
-   [AdsMgGetCommStats](/OpenADS/en/functions/ads-mg-get-comm-stats/)

---

[AdsMgKillUser →](/OpenADS/en/functions/ads-mg-kill-user/)

---
