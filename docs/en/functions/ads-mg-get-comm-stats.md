# [](#adsmggetcommstats)AdsMgGetCommStats

Returns server communication statistics.

## [](#syntax)Syntax

```
UNSIGNED32 AdsMgGetCommStats(ADSHANDLE hMg, void* pInfo,
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

Pointer to the output `ADS_MGMT_COMM_STATS` structure.

`pusSize`

`UNSIGNED16*`

Structure size (input) and bytes written (output).

## [](#return-value)Return Value

`AE_SUCCESS` (0) on success. Error code on failure.

## [](#description)Description

`AdsMgGetCommStats` returns server communication statistics, including checksum percentage, total packets, out-of-sequence packets, checksum failures, and send/receive errors.

## [](#example)Example

```
ADS_MGMT_COMM_STATS stStats;
memset(&stStats, 0, sizeof(stStats));
UNSIGNED16 usSize = sizeof(stStats);
AdsMgGetCommStats(hMgmt, &stStats, &usSize);
printf("Total packets: %lu\n", stStats.ulTotalPackets);
```

## [](#see-also)See Also

-   [AdsMgGetActivityInfo](/OpenADS/en/functions/ads-mg-get-activity-info/)
-   [AdsMgResetCommStats](/OpenADS/en/functions/ads-mg-reset-comm-stats/)
-   [AdsMgGetConfigInfo](/OpenADS/en/functions/ads-mg-get-config-info/)

---

[AdsMgGetConfigInfo →](/OpenADS/en/functions/ads-mg-get-config-info/)

---
