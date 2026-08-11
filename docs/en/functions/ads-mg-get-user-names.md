# [](#adsmggetusernames)AdsMgGetUserNames

Returns the names of users connected to the server.

## [](#syntax)Syntax

```
UNSIGNED32 AdsMgGetUserNames(ADSHANDLE hMg, UNSIGNED8* pucFile,
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

`pucFile`

`UNSIGNED8*`

Table name to filter by (or NULL for all).

`pInfo`

`void*`

Output array of `ADS_MGMT_USER_INFO` structures.

`pusCount`

`UNSIGNED16*`

Number of users returned.

`pusSize`

`UNSIGNED16*`

Total size of returned data in bytes.

## [](#return-value)Return Value

`AE_SUCCESS` (0) on success. Error code on failure.

## [](#description)Description

`AdsMgGetUserNames` returns information about users connected to the server. If a table name is specified, only users with that table open are returned.

## [](#example)Example

```
ADS_MGMT_USER_INFO astUsers[50];
memset(astUsers, 0, sizeof(astUsers));
UNSIGNED16 usCount = 50;
UNSIGNED16 usSize = sizeof(astUsers);
AdsMgGetUserNames(hMgmt, NULL, astUsers, &usCount, &usSize);
```

## [](#see-also)See Also

-   [AdsMgGetActivityInfo](/OpenADS/en/functions/ads-mg-get-activity-info/)
-   [AdsMgKillUser](/OpenADS/en/functions/ads-mg-kill-user/)
-   [AdsMgGetLocks](/OpenADS/en/functions/ads-mg-get-locks/)

---

[AdsMgGetWorkerThreadActivity →](/OpenADS/en/functions/ads-mg-get-worker-thread-activity/)

---
