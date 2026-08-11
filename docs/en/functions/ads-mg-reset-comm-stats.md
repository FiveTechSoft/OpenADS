# [](#adsmgresetcommstats)AdsMgResetCommStats

Resets server communication statistics.

## [](#syntax)Syntax

```
UNSIGNED32 AdsMgResetCommStats(ADSHANDLE hMg);
```

## [](#parameters)Parameters

Parameter

Type

Description

`hMg`

`ADSHANDLE`

Management connection handle.

## [](#return-value)Return Value

`AE_SUCCESS` (0) on success. Error code on failure.

## [](#description)Description

`AdsMgResetCommStats` resets all server communication statistics to zero. This is useful for measuring network traffic over a specific period.

## [](#example)Example

```
AdsMgResetCommStats(hMgmt);
// Statistics collection starts from zero
```

## [](#see-also)See Also

-   [AdsMgGetCommStats](/OpenADS/en/functions/ads-mg-get-comm-stats/)
-   [AdsMgGetActivityInfo](/OpenADS/en/functions/ads-mg-get-activity-info/)
-   [AdsMgGetConfigInfo](/OpenADS/en/functions/ads-mg-get-config-info/)

---

[AdsOpenTable90 →](/OpenADS/en/functions/ads-open-table-90/)

---
