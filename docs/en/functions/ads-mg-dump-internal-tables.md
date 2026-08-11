# [](#adsmgdumpinternaltables)AdsMgDumpInternalTables

Dumps server internal tables for diagnostics.

## [](#syntax)Syntax

```
UNSIGNED32 AdsMgDumpInternalTables(ADSHANDLE hMgmtHandle);
```

## [](#parameters)Parameters

Parameter

Type

Description

`hMgmtHandle`

`ADSHANDLE`

Management connection handle.

## [](#return-value)Return Value

`AE_SUCCESS` (0) on success. Error code on failure.

## [](#description)Description

`AdsMgDumpInternalTables` dumps information about the server's internal tables. This function is used for server diagnostics and troubleshooting.

## [](#example)Example

```
AdsMgDumpInternalTables(hMgmt);
```

## [](#see-also)See Also

-   [AdsMgConnect](/OpenADS/en/functions/ads-mg-connect/)
-   [AdsMgGetConfigInfo](/OpenADS/en/functions/ads-mg-get-config-info/)
-   [AdsMgGetActivityInfo](/OpenADS/en/functions/ads-mg-get-activity-info/)

---

[AdsMgGetActivityInfo →](/OpenADS/en/functions/ads-mg-get-activity-info/)

---
