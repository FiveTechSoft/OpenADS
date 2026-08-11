# [](#adsmggetopentables2)AdsMgGetOpenTables2

Returns the open tables on the server (extended variant).

## [](#syntax)Syntax

```
UNSIGNED32 AdsMgGetOpenTables2(ADSHANDLE hMg, UNSIGNED8* pucUser,
                               UNSIGNED16 usConnNumber, void* pInfo,
                               UNSIGNED16* pusCount, UNSIGNED16* pusSize);
```

## [](#parameters)Parameters

Parameter

Type

Description

`hMg`

`ADSHANDLE`

Management connection handle.

`pucUser`

`UNSIGNED8*`

User name (or NULL for all).

`usConnNumber`

`UNSIGNED16`

Connection number (0 for all).

`pInfo`

`void*`

Output array of `ADS_MGMT_TABLE_INFO` structures.

`pusCount`

`UNSIGNED16*`

Number of tables returned.

`pusSize`

`UNSIGNED16*`

Total size of returned data in bytes.

## [](#return-value)Return Value

`AE_SUCCESS` (0) on success. Error code on failure.

## [](#description)Description

`AdsMgGetOpenTables2` is an extended variant of `AdsMgGetOpenTables` that returns additional information about open tables. The behavior is essentially the same, but version 2 may include extra fields in the output structure.

## [](#example)Example

```
ADS_MGMT_TABLE_INFO astTables[100];
memset(astTables, 0, sizeof(astTables));
UNSIGNED16 usCount = 100;
UNSIGNED16 usSize = sizeof(astTables);
AdsMgGetOpenTables2(hMgmt, NULL, 0, astTables, &usCount, &usSize);
```

## [](#see-also)See Also

-   [AdsMgGetOpenTables](/OpenADS/en/functions/ads-mg-get-open-tables/)
-   [AdsMgGetOpenIndexes](/OpenADS/en/functions/ads-mg-get-open-indexes/)
-   [AdsGetNumOpenTables](/OpenADS/en/functions/ads-get-num-open-tables/)

---

[AdsMgGetServerType →](/OpenADS/en/functions/ads-mg-get-server-type/)

---
