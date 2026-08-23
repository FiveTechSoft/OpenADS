# oads_hb � Harbour HB_FUNC wrappers for `oads_*()` C API

Provides `OADS_FCREATE()`, `OADS_FOPEN()`, `OADS_FCLOSE()`,
`OADS_FWRITE()`, `OADS_FREAD()`, and `OADS_FSEEK()` as callable
Harbour functions, plus the server-side distributed mutex functions
`OADS_MUTEXCREATE()`, `OADS_MUTEXLOCK()`, `OADS_MUTEXTRYLOCK()`,
`OADS_MUTEXUNLOCK()` and `OADS_MUTEXDESTROY()`.

These are thin wrappers over the `oads_*()` exports from the OpenADS
DLL. The `oads_*()` functions are themselves ABI aliases for the
canonical `AdsF*()` functions � same engine, different name.

## Why?

Legacy FiveWin / Harbour code that was written against an older DLL
which exported `oads_FOpen`, `oads_FCreate`, etc. can now call them
from PRG code without `#pragma BEGINDUMP` or raw C linkage.

## Usage from PRG

```harbour
#include "ads.ch"
REQUEST ADS, ADSCDX

PROCEDURE Main()
   LOCAL hConn, hFile, cRead, nPos

   AdsSetServerType( ADS_LOCAL_SERVER )
   hConn := AdsConnect( "C:\mydata" )

   // Create a file
   hFile := OADS_FCreate( hConn, "hello.txt", 0 )

   // Write data
   OADS_FWrite( hFile, "Hello, World!" )

   // Close
   OADS_FClose( hFile )

   // Reopen read-only
   hFile := OADS_FOpen( hConn, "hello.txt", 3 )  // 3 = ADS_READONLY

   // Read all
   cRead := OADS_FRead( hFile, 1024 )
   ? cRead   // "Hello, World!"

   // Seek
   nPos := OADS_FSeek( hFile, 7, 0 )  // SEEK_SET
   cRead := OADS_FRead( hFile, 100 )
   ? cRead   // "World!"

   OADS_FClose( hFile )
   AdsDisconnect()
RETURN
```

## Function signatures

| PRG function | Parameters | Returns |
|---|---|---|
| `OADS_FCreate( hConn, cFile, nAttr )` | hConn, filename, attribute (0) | hFile (0 on fail) |
| `OADS_FOpen( hConn, cFile, nMode )` | hConn, filename, mode (0=rw, 3=ro) | hFile (0 on fail) |
| `OADS_FClose( hFile )` | file handle | lOk (.T./.F.) |
| `OADS_FWrite( hFile, cData )` | file handle, buffer | nBytesWritten |
| `OADS_FWrite( hFile, cData, @nWritten )` | with @ out param | lOk (.T./.F.) |
| `OADS_FRead( hFile, nLen )` | file handle, length | cData |
| `OADS_FRead( hFile, @cBuf, nLen )` | with @ buffer | nBytesRead |
| `OADS_FSeek( hFile, nOffset, nOrigin )` | offset, origin (0=SET,1=CUR,2=END) | nPosition |

## Server-side mutexes (remote connections only)

Server-wide named mutexes, shared by all sessions of a server.
They require a remote connection (`tcp://` / `tls://`); on a local
connection they return `.F.`. `hConn` is optional: when omitted the
default connection set with `OADS_SetConnection()` is used.

| PRG function | Parameters | Returns |
|---|---|---|
| `OAds_MutexCreate( [ hConn ], cName )` | mutex name | lOk |
| `OAds_MutexLock( [ hConn ], cName, nTimeoutMs )` | 0 = wait forever | lOk |
| `OAds_MutexTryLock( [ hConn ], cName )` | non-blocking | lLocked |
| `OAds_MutexUnlock( [ hConn ], cName )` | owner only | lOk |
| `OAds_MutexDestroy( [ hConn ], cName )` | mutex name | lOk |

```harbour
hConn := AdsConnect( "tcp://192.168.18.184:6262//Users/anto/OpenADS/data" )
OAds_SetConnection( hConn )

OAds_MutexCreate( "invoice_seq" )
IF OAds_MutexLock( "invoice_seq", 5000 )   // wait up to 5 s
   // ... critical section, e.g. allocate next invoice number ...
   OAds_MutexUnlock( "invoice_seq" )
ENDIF
OAds_MutexDestroy( "invoice_seq" )
```

Mutexes are released automatically when the owning session
disconnects.

## Logging kill-switch (production)

`OAds_SetLogging( lOn )` enables/disables every log line the ace DLL
can emit from this process: the audit channel (`OPENADS_LOG_FILE` /
console RESOLVED lines) and the `ace_calls.log` bring-up traces.
Logging is ON by default (developer diagnostics); call
`OAds_SetLogging( .F. )` once at startup before shipping so paths,
aliases and record data never reach end-user machines.

```harbour
OAds_SetLogging( .F. )   // production: silent DLL
```

The switch is process-local — it also silences the embedded engine in
local-server mode (same process). A remote `openads_serverd` keeps its
own configuration on the server machine.

## Build

`oads_hb.c` is compiled into **your** Harbour project (not the OpenADS
DLL). It includes `"ace.h"` the same way `contrib/rddads` does — from
`HB_WITH_ADS`, the ACE SDK, or a copy of `include/openads/ace.h` on
the include path. Do not change this back to `"openads/ace.h"`: that
path only exists inside the OpenADS source tree.

```cmd
set OPENADS_LIB=C:\OpenADS\dist\import-libs\x64\mingw
hbmk2 myproject.hbp oads_hb.c -L%OPENADS_LIB% -lace64
```

Or add `oads_hb.c` to your `.hbp` recipe.

## Test

```cmd
cd test
set OPENADS_LIB=C:\OpenADS\dist\import-libs\x64\mingw
hbmk2 test_oads_file.hbp
test_oads_file.exe
```

Remote (iMac on LAN):
```cmd
openads_serverd --port 6262 --data /Users/anto/OpenADS/data
set OPENADS_TEST_REMOTE=tcp://192.168.18.184:6262//Users/anto/OpenADS/data
test_oads_file.exe
```
