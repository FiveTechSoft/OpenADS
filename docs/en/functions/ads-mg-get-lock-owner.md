# [](#adsmggetlockowner)AdsMgGetLockOwner

Returns the owner of a specific lock.

## [](#syntax)Syntax

```
UNSIGNED32 AdsMgGetLockOwner(ADSHANDLE hMg, UNSIGNED8* pucTable,
                             UNSIGNED32 ulRecord, void* pInfo,
                             UNSIGNED16* pusSize, UNSIGNED16* pusLockType);
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

Table name.

`ulRecord`

`UNSIGNED32`

Record number that is locked.

`pInfo`

`void*`

Pointer to the output `ADS_MGMT_LOCK_INFO` structure.

`pusSize`

`UNSIGNED16*`

Structure size (input) and bytes written (output).

`pusLockType`

`UNSIGNED16*`

Returned lock type.

## [](#return-value)Return Value

`AE_SUCCESS` (0) on success. Error code on failure.

## [](#description)Description

`AdsMgGetLockOwner` returns information about the user who holds the lock on a specific record in a table. Useful for diagnosing lock conflicts.

## [](#example)Example

```
ADS_MGMT_LOCK_INFO stInfo;
memset(&stInfo, 0, sizeof(stInfo));
UNSIGNED16 usSize = sizeof(stInfo);
UNSIGNED16 usLockType;
AdsMgGetLockOwner(hMgmt, "Orders", 42, &stInfo, &usSize, &usLockType);
printf("Locked by: %s\n", stInfo.aucUserName);
```

## [](#see-also)See Also

-   [AdsMgGetLocks](/OpenADS/en/functions/ads-mg-get-locks/)
-   [AdsIsRecordLocked](/OpenADS/en/functions/ads-is-record-locked/)
-   [AdsLockRecord](/OpenADS/en/functions/ads-lock-record/)

---

[AdsMgGetLocks →](/OpenADS/en/functions/ads-mg-get-locks/)

---
