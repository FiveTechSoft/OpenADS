# [](#adsmggetopenindexes)AdsMgGetOpenIndexes

Returns the open indexes on a table.

## [](#syntax)Syntax

```
UNSIGNED32 AdsMgGetOpenIndexes(ADSHANDLE hMg, UNSIGNED8* pucTable,
                               UNSIGNED8* pucUser, UNSIGNED16 usConnNumber,
                               void* pInfo, UNSIGNED16* pusCount,
                               UNSIGNED16* pusSize);
```

## [](#parameters)Parameters

Parameter

Type

Description

`hMg`

`ADSHANDLE`

Management connection handle.

`pucTable`

`UNSIGNED8*`

Table name (or NULL for all).

`pucUser`

`UNSIGNED8*`

User name (or NULL for all).

`usConnNumber`

`UNSIGNED16`

Connection number (0 for all).

`pInfo`

`void*`

Output array of `ADS_MGMT_INDEX_INFO` structures.

`pusCount`

`UNSIGNED16*`

Number of indexes returned.

`pusSize`

`UNSIGNED16*`

Total size of returned data in bytes.

## [](#return-value)Return Value

`AE_SUCCESS` (0) on success. Error code on failure.

## [](#description)Description

`AdsMgGetOpenIndexes` returns information about open indexes on a table, filtered by table, user, and/or connection number. Each entry includes the index file name, tag, and expression.

## [](#example)Example

```
ADS_MGMT_INDEX_INFO astIdx[50];
memset(astIdx, 0, sizeof(astIdx));
UNSIGNED16 usCount = 50;
UNSIGNED16 usSize = sizeof(astIdx);
AdsMgGetOpenIndexes(hMgmt, "Orders", NULL, 0, astIdx, &usCount, &usSize);
```

## [](#see-also)See Also

-   [AdsMgGetOpenTables](/OpenADS/en/functions/ads-mg-get-open-tables/)
-   [AdsGetNumIndexes](/OpenADS/en/functions/ads-get-num-indexes/)
-   [AdsGetIndexName](/OpenADS/en/functions/ads-get-index-name/)

---

[AdsMgGetOpenTables →](/OpenADS/en/functions/ads-mg-get-open-tables/)

---
