Built for Pritpal Bedi - bedipritpal/OpenADS, forked from FiveTechSoft/OpenADS.

## v1.09.68-mtfix17 - fresh-organization directory fix candidate

This is a test build, not a production recommendation. Keep mtfix15 as the recommended working build while checking this candidate against a fresh Vouch organization. It retains the opt-in table-create diagnostics from mtfix16.

The remote client no longer caches positive or negative directory-existence answers or skips DirMake after an earlier success. DirExist and DirMake now reach the server every time, so a directory removed or created outside the current connection is not hidden by an old answer. Server-side DirExist also treats a missing path as a normal false answer, not an internal error. File-existence and other optimizations are unchanged. Each directory call may now cost one extra network round trip compared with a cache hit.

The regression test creates a directory, sees it, removes it directly on the server, checks that the same connection sees it missing, and verifies that DirMake recreates it, including a repeat mkdir after another removal. This matches a plausible cause of the observed first-create failure (`uxxyyzza.dbf`: exclusive open saw a missing parent directory), but the diagnostic trace did not prove the full raw table path or all of the Vouch folder steps. A successful fresh-org Vouch run remains the needed validation.

**What to test:** Use the mtfix17 client and server from this same build for one new Vouch organization, including its first table create. Report the organization-create result, any `uxxyyzza.dbf` or other missing-folder error, and the client and server diagnostic logs if it still fails. Then return to mtfix15 for regular work until this candidate is validated. The diagnostic environment variables from mtfix16 are optional and should be unset again after capture.

**CI:** Windows x86/x64 and Linux (including TLS) passed, along with Harbour smoke. macOS configured and built, but its test step was cancelled at the CI timeout, so macOS tests remain unverified. The same timeout affected prior builds; this is not evidence that mtfix17's macOS tests passed. No strict no-symlink change is included here.
