# oads_hb — Harbour HB_FUNC wrappers for `oads_*()` C API

Provides `OADS_FCREATE()`, `OADS_FOPEN()`, `OADS_FCLOSE()`,
`OADS_FWRITE()`, `OADS_FREAD()`, and `OADS_FSEEK()` as callable
Harbour functions.

These are thin wrappers over the `oads_*()` exports from the OpenADS
DLL. The `oads_*()` functions are themselves ABI aliases for the
canonical `AdsF*()` functions — same engine, different name.

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

## Build

```cmd
set OPENADS_LIB=C:\OpenADS\dist\import-libs\x64\mingw
hbmk2 myproject.hbp oads_hb.c -L%OPENADS_LIB% -lace64
```

Or add `oads_hb.c` to your `.hbp` recipe alongside `../oads_hb.c`.

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
