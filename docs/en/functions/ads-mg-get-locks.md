# [](#adsmggetlocks)AdsMgGetLocks

Returns locks held by a user or on a table.

## [](#syntax)Syntax

```
UNSIGNED32 AdsMgGetLocks(ADSHANDLE hMg, UNSIGNED8* pucTable,
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

Output array of `ADS_MGMT_LOCK_INFO` structures.

`pusCount`

`UNSIGNED16*`

Number of locks returned.

`pusSize`

`UNSIGNED16*`

Total size of returned data in bytes.

## [](#return-value)Return Value

`AE_SUCCESS` (0) on success. Error code on failure.

## [](#description)Description

`AdsMgGetLocks` returns information about all record locks held matching the specified filters (table, user, connection). Can be used to obtain all locks on the server by passing NULL for the filters.

## [](#example)Example

```
ADS_MGMT_LOCK_INFO astLocks[100];
memset(astLocks, 0, sizeof(astLocks));
UNSIGNED16 usCount = 100;
UNSIGNED16 usSize = sizeof(astLocks);
AdsMgGetLocks(hMgmt, NULL, NULL, 0, astLocks, &usCount, &usSize);
```

## [](#see-also)See Also

-   [AdsMgGetLockOwner](/OpenADS/en/functions/ads-mg-get-lock-owner/)
-   [AdsGetAllLocks](/OpenADS/en/functions/ads-get-all-locks/)
-   [AdsIsRecordLocked](/OpenADS/en/functions/ads-is-record-locked/)

---

[AdsMgGetOpenIndexes →](/OpenADS/en/functions/ads-mg-get-open-indexes/)

---
