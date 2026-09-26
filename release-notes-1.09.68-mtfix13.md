Built for Pritpal Bedi - bedipritpal/OpenADS, forked from FiveTechSoft/OpenADS.

## v1.09.68-mtfix13 - correct index-bag existence checks

An optimization added in mtfix10 answered that `brwcon01.z01` existed if `brwcon01.dbf` was open, because it compared filenames after removing their extensions. With the bag absent, this could tell the application to skip creating its index. This build restricts the table-name shortcut to the exact table path. Checking for an unopened bag now goes through the normal file-existence path; already-open index bags retain their earlier shortcut. A regression test checks that a live DBF cannot make a missing bag of the same stem appear present, and that a repeated miss uses the negative cache. The prior index-create diagnostic remains available when `OPENADS_CREATE_INDEX_DIAG_FILE` is set; it is off by default.

This is a targeted test build, not yet proof that it fixes Vouch's early exit. Test by starting a fresh org with BRWCON01.dbf and BRWCON01.z01 absent, then check that BRWCON01.z01 is created and the app continues. Save a backup of your old ace32.dll, replace only the client DLL in your application folder from the Windows x86 archive, and restore it after the test if needed. No server update is needed for this client-side change.
