# [](#adsmggetopentables)AdsMgGetOpenTables

Returns the open tables on the server.

## [](#syntax)Syntax

```
UNSIGNED32 AdsMgGetOpenTables(ADSHANDLE hMg, UNSIGNED8* pucUser,
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

`AdsMgGetOpenTables` returns information about all tables open on the server, filtered by user and/or connection number. Each entry includes the table name, owning user, connection number, open mode, and lock type.

## [](#example)Example

```
ADS_MGMT_TABLE_INFO astTables[100];
memset(astTables, 0, sizeof(astTables));
UNSIGNED16 usCount = 100;
UNSIGNED16 usSize = sizeof(astTables);
AdsMgGetOpenTables(hMgmt, NULL, 0, astTables, &usCount, &usSize);
```

## [](#see-also)See Also

-   [AdsMgGetOpenTables2](/OpenADS/en/functions/ads-mg-get-open-tables-2/)
-   [AdsMgGetOpenIndexes](/OpenADS/en/functions/ads-mg-get-open-indexes/)
-   [AdsGetNumOpenTables](/OpenADS/en/functions/ads-get-num-open-tables/)

---

[AdsMgGetOpenTables2 →](/OpenADS/en/functions/ads-mg-get-open-tables-2/)

---
