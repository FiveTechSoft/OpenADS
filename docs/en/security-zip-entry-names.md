# Complete ZIP entry names

Archive target enumeration and extraction read the complete central-directory
entry name before choosing a path. Names longer than the old 1,024-byte working
buffer no longer silently select a truncated output name. Embedded NUL names,
empty names and lengths outside the ZIP field range are rejected.

The original exact-name and extraction-root checks still apply. This fix does
not close archive resource limits, concurrent filesystem races, failed-overwrite
recovery or traditional ZipCrypto limitations. The security audit remains open.
