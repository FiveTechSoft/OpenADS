# HRB challenge auth spike - UNMERGED, NOT FOR PRODUCTION

Prepared for Pritpal Bedi. Owner supplies VouchBuildCredentials and the
server .hrb. This branch does not implement or certify that private scheme.

Proposed Harbour API:

    cChallenge := OAds_BeginAuth( cUrl, @hPending )
    IF Len( cChallenge ) == 32
       cBlob := VouchBuildCredentials( cChallenge )
       hConn := OAds_CompleteAuth( hPending, cBlob )
    ENDIF

OAds_CancelAuth(hPending) abandons the attempt. These are new APIs, not
changes to existing AdsConnect60. Pending handles are never DB connections.
Complete consumes the pending attempt on success or failure. Every challenge
is 32 CSPRNG bytes associated with one encrypted transport and a deadline.
No credentials/challenge are transmitted until encrypted transport is ready.

Transport: TLS1.3 internally with automatically generated server identity,
explicitly unverified. No certificate administration or per-machine keys.
Passive sniffing is blocked; active MITM/relay remains possible on EVERY
connection unless the application adds a reviewed mutual-authentication and
channel-binding scheme. A hook saying yes does not authenticate the server.
No downgrade to plaintext/another auth mode on handshake or login failure.

Server authentication provider is separate from index UDF registration.
Only configured local module/function is callable. Missing/broken required
module prevents startup. The module receives binary challenge, opaque blob,
version and server-owned context. It checks the authenticated embedded
challenge and credentials. OpenADS never decrypts or parses the private blob.
It rejects oversize input and malformed results and logs neither blobs nor
credentials. The module must check its crypto integrity tag before parsing.
Harbour == compares binary strings exactly; no trimming/codepage conversion.

Failure containment: use supervised worker processes with bounded IPC,
pool/pending limits and kill/restart on timeout/crash. In-process Harbour
protected calls catch language errors, not infinite loops/native crashes.
Worker isolation is not a security sandbox: trusted module runs as the
server account. Protect module path/file and never accept network code.

Owner delegated final choice after leaning to option 2; selected route: HRB returns canonical DB username/password with yes.
The existing daemon and dictionary login and ACL checks run unchanged.
Hook success is an additional gate, never anonymous/admin DB access.
Result is a binary Harbour string: "OAUTH" + Chr(1) + Chr(allow) +
u16LE(user length) + user + u16LE(password length) + password.
Only allow=0/1 is valid. Deny requires both fields empty; success requires a
nonempty username. Username max256 bytes, password max4096; no NULs and no
trailing bytes. These are the same ordinary DB credential limits.
Version/function: OADS_AUTH_V1(cChallenge,cBlob,cContext), where context is
server-owned. No JSON coercions or truthy-value conversions. HRB must return
exact framed string, not logical true. Credentials are transient and wiped
where practical; arbitrary module/VM copies cannot be guaranteed wiped.

Tests planned: pending lifetime/single-use/cancel/replay; malformed/oversize
blob/result; error/hang/crash worker; concurrency; challenge/account/root/
channel swap; active relay; plaintext downgrade; real Harbour client;
DBFCDX locks and dictionary permissions; native matched Winlibs10.2 x86/x64.
No main/upstream/PR/release until owner tests and passes.

## Current local implementation and limits

Explicit client URI: auth://host:port/path. Server: --auth_module /absolute/module.hrb
requires TLS and Harbour-UDF support at build time. It enables a dedicated
listener, rejects ordinary Connect and management login, and refuses HTTP or
explicit certificate configuration for this spike. Existing TLS/Connect mode
is unchanged without --auth_module. Worker executable is installed beside
serverd. No RDDADS-core edit, no compiler/Qt upgrade.

Pending maximum64 per client process; timeout30s; blob max4096 bytes; server
pre-login frame maximum64KiB. Fresh worker per call; maximum4 globally; worker
load/auth/unload deadline2s; no queued attempts on saturation. Normal daemon
and dictionary rate limits remain, with a pre-hook per-IP auth limiter.
Process startup cost is intentional containment, not benchmarked yet.

HRB file is read once from a protected regular non-symlink file. Linux requires
service-owned0600; Windows allows only current service identity, SYSTEM and
Administrators. The checked bytes are loaded in memory, avoiding check/reopen
race. Parent directories and worker executable installation must be protected
by the deployer. Module executes arbitrary trusted code as server account.
No sandbox claim; a malicious module can itself spawn other processes or use
its account privileges. The timeout kills the supervised worker, not an
unrestricted module's deliberately detached children. Never load untrusted
modules. Each request is a fresh process, no persistent module static state.

Client does not automatically open extra pooled sessions: each lane would
need a new challenge and owner-built blob. Session capabilities match the existing compiled wire client
(OpenTable mode prefix is mandatory for that client). Credentials returned by the module are not logged and buffers
are wiped where practical. Normal engine currently retains session password
for its lazy ABI connection, just as existing login does. No promise of zero
credential copies in the VM, standard string allocator or IPC kernel.

Local proof so far:4 lifecycle/schema cases62 assertions; synthetic real HRB
worker accept/deny/malformed/2s hang/recovery; Linux C API full TLS auth success,
existing daemon credential check, consumed handle/replayed challenge/cancel.
Fixture is TEST ONLY, not cryptographic authentication. Native x86/x64,
dictionary ACL/DBFCDX and TLS record attack gauntlet still outstanding.

Linux TLS record tamper/replay isolation and normal dictionary-login /
wrong-password / two-session DBFCDX record-lock handoff probes passed locally.
Local dedicated listener also denies prelogin DB op, ordinary Connect,
management login, TLS1.2 downgrade and plaintext challenge. Eight concurrent
hanging worker requests: at least four deny immediately under the global
limit; supervised remaining calls time out; next-good worker probe passes.
Native job is separate proof, not established by these Linux results.

ACTIVE RELAY DEMONSTRATED LOCALLY: an unrelated-certificate TLS-terminating
proxy opened a second TLS connection to daemon and forwarded challenge/blob.
Synthetic HRB-approved database login SUCCEEDED through that proxy. This
protocol is deliberately NOT MITM-safe. TLS ciphertext tamper/replay rejection
does not imply server identity or application relay rejection.
