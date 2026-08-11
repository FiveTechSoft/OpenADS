# [](#adsmgdisconnect)AdsMgDisconnect

Closes a management connection.

## [](#syntax)Syntax

```
UNSIGNED32 AdsMgDisconnect(ADSHANDLE hMg);
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

`AdsMgDisconnect` closes a management connection previously opened with `AdsMgConnect`.

## [](#example)Example

```
AdsMgDisconnect(hMgmt);
```

## [](#see-also)See Also

-   [AdsMgConnect](/OpenADS/en/functions/ads-mg-connect/)
-   [AdsMgGetActivityInfo](/OpenADS/en/functions/ads-mg-get-activity-info/)
-   [AdsDisconnect](/OpenADS/en/functions/ads-disconnect/)

---

[AdsMgDumpInternalTables →](/OpenADS/en/functions/ads-mg-dump-internal-tables/)

---
