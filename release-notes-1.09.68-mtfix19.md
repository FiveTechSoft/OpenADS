Built for Pritpal Bedi - bedipritpal/OpenADS, forked from FiveTechSoft/OpenADS.

## v1.09.68-mtfix19 - OAds connection handoff test build

This is a test build, not a production recommendation. Keep mtfix15 as the recommended working build until Vouch's fresh-organization flow validates this candidate. mtfix19 includes mtfix18's always-live directory and file existence checks, plus a change in the Harbour `contrib/oads_hb/oads_hb.c` glue. It does not change server-side table locking or file formats.

`OAds_SetConnection(hConn)` now stores a shared OAds default handle, and `OAds_GetConnection()` and all no-handle OAds file, directory, mutex, archive, and server-version helpers use that same handle. Explicit-handle overloads are unchanged. Before the first OAds SetConnection, the ACE default remains the fallback. Zero is rejected as a new OAds default. This targets the Vouch case where `OAds_DirExist(cName)` answered YES for a directory absent on the server but `OAds_DirExist(SetAdsConxn(), cName)` answered NO. That difference establishes a wrong implicit connection in that run; it does not prove whether a thread switch, intervening overwrite, or compiled binding difference caused the old default to diverge.

**What to test:** `oads_hb.c` is compiled into Vouch. Rebuild Vouch with the mtfix19 copy of that file and restore the original one-argument `OAds_DirExist(cName)` call. Merely replacing the DLL does not replace the compiled glue. Use matching mtfix19 client and server builds for a clean new Vouch organization. Confirm the absent directory returns NO, creation succeeds, and the first tables open. Report the exact return/error and both client/server logs if it fails. In apps concurrently using independent connections or organizations, pass the explicit handle rather than switching one process-wide OAds default mid-operation.

A separate 700-instance B_BIG WAN storm stalled and remains undiagnosed. This test build does not claim a storm fix or a safe upper bound.

**Validation:** The source diff passes whitespace checks and a C syntax-only check against mocked Harbour declarations; a real Harbour Vouch rebuild and fresh-org test are still required. Windows x86/x64 and Linux CI status belongs to this release's workflow once complete. macOS configured and built in the prior mtfix18 run, but its Test step timed out/cancelled, so macOS tests remain unverified until a completed run says otherwise.
