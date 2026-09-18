# Vendored minizip 1.1 (classic ZipCrypto-compatible API)

Source: zlib 1.3.1 `contrib/minizip`
(`https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz`):
`zip.h`, `unzip.h`, `ioapi.h`, `crypt.h`, `zip.c`, `unzip.c`, `ioapi.c`
(zlib/libpng license, see zlib README).

Why vendored instead of FetchContent: exactly ONE deliberate change
versus upstream, in `unzip.c` — upstream force-disables read-side
decryption:

```c
#ifndef NOUNCRYPT
        #define NOUNCRYPT
#endif
```

That block is deleted here. Without it, `unzOpenCurrentFilePassword`
rejects every password (`UNZ_PARAMERROR`) while the write side
happily encrypts — password-protected server backups would be
write-only. The decrypt code itself is untouched upstream code.

The zlib *library* is still fetched/built by CMake (see root
CMakeLists, `zlib_src`); only these minizip sources are vendored.
