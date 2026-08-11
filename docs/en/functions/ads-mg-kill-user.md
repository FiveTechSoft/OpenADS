# [](#adsmgkilluser)AdsMgKillUser

Disconnects a user from the server.

## [](#syntax)Syntax

```
UNSIGNED32 AdsMgKillUser(ADSHANDLE hMg, UNSIGNED8* pucUser,
                         UNSIGNED16 usOption);
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

User name to disconnect.

`usOption`

`UNSIGNED16`

Options (0 = default).

## [](#return-value)Return Value

`AE_SUCCESS` (0) on success. Error code on failure.

## [](#description)Description

`AdsMgKillUser` forces the disconnection of a user from the Advantage server. The user is disconnected immediately and all their connections and locks are released.

## [](#example)Example

```
AdsMgKillUser(hMgmt, "problem_user", 0);
```

## [](#see-also)See Also

-   [AdsMgGetUserNames](/OpenADS/en/functions/ads-mg-get-user-names/)
-   [AdsMgDisconnect](/OpenADS/en/functions/ads-mg-disconnect/)
-   [AdsMgGetActivityInfo](/OpenADS/en/functions/ads-mg-get-activity-info/)

---

[AdsMgResetCommStats →](/OpenADS/en/functions/ads-mg-reset-comm-stats/)

---
