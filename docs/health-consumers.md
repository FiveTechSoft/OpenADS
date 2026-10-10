# Health consumers

These consumers use the aggregate schema1 backend in `docs/health-backend.md`.
They do not change the backend wire contract, lock ownership or count caching.

## Harbour

`OAds_ServerStats(hMgmt, @nError)` returns a hash, or NIL and a numeric error.
The handle must come from `AdsMgConnect`, not a default data connection.
`OAds_ServerStatsRemote(host, port, user, password, @nError)` owns its management
connect/read/disconnect lifecycle, serialized for MT callers. It accepts a
literal hostname/IPv4 address and port1..65535, not a URI/path or IPv6 literal.
It never changes rddads management/data handles. Load credentials from secure
settings, not command-line arguments. Management transport remains plain TCP.

Both wrappers use an8192-byte buffer and fail rather than return partial data;
validate a complete JSON hash, preserve null as NIL and wide numeric values.
Matching DLL/server support is required. Older pairs return failure/unavailable.

Compile `contrib/oads_hb/oads_stats_display.prg` with the app for
`OAds_ServerStatsText(hStats)` and `OAds_ServerStatsShow(hStats,top,left,bottom,right)`.
Text is76columns: Statistic24,Current20,Max used16,Rejected16. All scalar,
usage and semantics fields appear; unknowns say Unavailable and cap0 says
Unlimited(0). The screen helper clears only its inclusive rectangle, clips
long lines, omits excess rows and preserves cursor/color. Existing CT-window
coordinates are local to the current window; the app supplies its box interior.
No implicit connect, refresh loop or hbct dependency is added.

Smoke fixtures in `tests/smoke/harbour/` cover remote MT/validation/login,
text formatting, GT bounds/color/cursor and optional CT window rendering.

## Embedded Studio

The Server tab adds manual Refresh server health and count meanings/raw JSON.
`GET /api/server/health` uses the existing HTTP authentication gate and sends
Cache-Control:no-store. An attached wire server supplies server-side values;
an embedded local console without one reports local_process. No new listener,
authentication bypass or polling timer. Existing HTTP empty-user development
mode is unchanged and must not be treated as a protected deployment.

## DA-Web

Server Info adds the same health table. Unknowns stay unavailable, never false
zero. Cap0 means unlimited. All dynamic text/raw JSON is escaped. Refresh is
manual, with the existing Server Info refresh control.

Daemon-management credentials are separate from Data Dictionary credentials.
Successful credentials are held in the server-side PHP session, scoped to the
selected dictionary endpoint, for20minutes. Expiry, endpoint changes, dictionary
reconnect/disconnect or Forget management login clear them. Password is cleared
from the browser field immediately after submit, never put in URL/localStorage,
logs or returned JSON. Credentials need HTTPS from browser to portal, except
literal loopback development. This does not encrypt the AdsMg management socket;
use a trusted/firewalled route or configured TLS proxy.

Every Server Info JSON POST requires the session CSRF token: login/logout and
existing kill/sql/locks requests. Direct API consumers must first GET Server Info
to obtain management_csrf and include it in subsequent JSON POSTs. The selected
DD connection must exist in the PHP session. ADSHANDLE FFI declarations use the
actual32-bit ABI, including on64-bit PHP. Health failures do not invent values
or discard otherwise available legacy Server Info data.

DA-Web files must be deployed separately from binary DLL/server packages.
No new portal-wide authentication system is claimed; existing session security,
protected storage, HTTPS and network isolation remain deployment responsibilities.
Windows32-bit Harbour and full portal deployment testing are separate from local
Linux fixtures.

## Sandbox checks

`node tests/health-consumers/render_health.js` checks escaping, nulls, wide
numbers, unlimited caps and older-extension fallback text.
`OPENADS_BUILD=/absolute/build PHP_BIN=php python3 tests/health-consumers/portal_fixture.py`
starts a loopback-only disposable daemon and PHP server. PHP_FFI_EXTENSION may
name ffi or its absolute module path. It uses fixed sandbox ports26391..26393,
only test credentials and its own temporary PHP session/data directory. Free
those ports first. It covers nativeFFI/auth/expiry/endpoint/local mode, CSRF,
HTTPS gate and the new Studio route. The HTTPS test uses a sandbox copy of the
PHP endpoint with stdin substituted for php://input, not a production change.
The fixture never kills a real user: legacy kill is tested only with invalid
CSRF, so the action is rejected before any management request.
