# OpenADS 1.09.74 - stable upstream release

This upstream release includes the reviewed port series through PR192 (383dac64933db2e8eba0efaa0f96967f1ae4c99f), with version 1.09.74 headers, the MinGW import generator and matching tests. Upstream's existing release workflow is preserved.

The five binary assets are copied byte-for-byte from Pritpal Bedi's published release, not rebuilt on the upstream tag. Their exact built source tree is c160c4587a29ae8952431231cd81d38ee52e71b6, available at fork tag v1.09.74 (b312c71696dbd22dae1bd6fbd84b6ae049efa825). Its product sources, matching headers, Harbour glue and display source match this upstream release. The upstream source tag differs only in release workflow and this updated release-note file. Binary build provenance and original assets: https://github.com/bedipritpal/OpenADS/releases/tag/v1.09.74 .

Thanks to Pritpal Bedi for the multi-instance Vouch tests, billing-counter traces, stale-read reports, record-lock and navigation checks, tea-break idle-session report, and final application clearance. His logs and repeatable tests drove the fixes and kept record integrity and the DBFCDX lock contract ahead of speed alone.

## Database and index behavior

- CDX bottom navigation reaches the rightmost index leaf directly and refreshes it for changes made by peers.
- Physical record counts and ordered-position counts follow their distinct contracts. Ordered-position SET behavior is unchanged.
- Open replies carry bounded production-index metadata. The client checks that the metadata belongs to the matching index bag before using it.
- Server-side no-link table opens reject symlinked roots, directories, table/index/memo leaves, including a .cdx symlink to .z01. Deployments using those layouts need ordinary files/directories. This is not a complete OS filesystem sandbox; hard links, mounts, path-based rename/unlink operations and exhaustive directory-rename races are not claimed covered.
- The mtfix39 lock-row refresh and mtfix40 remote-file wait handling are retained in the upstream-based line.
- The mtfix41 healthy-idle session policy is retained: established database sessions are not disconnected by an idle cutoff unless the operator explicitly enables one.
- The release keeps upstream's stronger login handling, management authentication, malformed-frame checks and resource limits. It does not restore older fork network behavior.

## Health and management

- OAdsGetServerStats supplies bounded JSON health data through local handles or authenticated remote management handles.
- Matching Harbour wrappers and the bounded text/GT display source are included in the packages.
- Studio's health GET and DA-Web's health display consume the same health backend. Studio keeps its existing authentication gate, no-store responses and manual refresh.
- DA-Web management credentials stay in a server-side PHP session, scoped to the endpoint, and expire after 20 minutes. Reconnect/disconnect, logout, expiry and endpoint changes clear them.
- Non-loopback browser entry of management credentials requires HTTPS.
- All DA-Web management JSON POST actions require the session CSRF token, including existing management actions. Clients that POST directly must first obtain and then send that token.
- Health display is read-only. Management actions still require their own authentication.

## Vouch clearance

Pritpal tested the actual 1.09.74 candidate with the 1.09.74 client and EC2 server on October 10, 2026. He ran 10- and 100-instance storm tests, checked browser response under load, and continued multi-instance Vouch testing. He reported that Vouch speed matched mtfix41, the B_BIG browser returned instantly, and every breakpoint tested in Vouch since the beginning worked. His final report was that performance was much better, followed by explicit clearance to publish.

These are application observations from his workload, not a claim that every deployment will see a measured speed increase or that all platforms received the same application test.

## Packages and installation

All five binary packages are included:

- openads-1.09.74-windows-x86.zip
- openads-1.09.74-windows-x64.zip
- openads-1.09.74-linux-x64.tar.gz
- openads-1.09.74-linux-glibc231.tar.gz
- openads-1.09.74-macos-universal.tar.gz

Windows ZIPs carry both ACE DLL names, MSVC imports, refreshed MinGW DLL imports and MinGW static ACE libraries. Linux and macOS packages carry both shared-library names. Packages carry matching headers, OAds glue and stats display source, license, notices and sample server INI.

Use the standard Linux package on Ubuntu 24.04. Use the glibc2.31 compatibility package for older Linux systems such as Ubuntu 20.04 that cannot load the Ubuntu-24-built library. The compatibility package was checked for a glibc requirement no higher than 2.31. Match the client DLL, import/static libraries and OAds glue to this release; compile application code with the matching glue. Preserve your existing server INI and data when changing binaries.

DA-Web files are deployed separately from the binary packages. They are available in the tagged source, not bundled as a ready-made PHP deployment in these archives.

The Linux startup message about no ace64.dll/openace64.dll is a Windows-name DLL probe. It does not block ordinary RDD table/index/lock work without AEP external procedures. AEP module setup is separate and must use the appropriate module path; do not rename a Linux shared library into a Windows DLL to silence the message.

## Compatibility and operating limits

- Default remote record-count caching remains. The fork's optional OPENADS_FRESH_COUNTS switch was parked during upstream review and is absent from this release. A cached count is not a peer-fresh count.
- The established database-session idle cutoff is disabled by default. A positive established_session_idle_seconds server INI setting or matching CLI flag opts into disconnecting inactive database sessions and releasing their workareas and locks. Handshake, partial-frame, reply-drain and management-session timers remain separate. Real disconnects still clean up.
- Management sockets remain plain TCP, including credentials. Browser-to-PHP HTTPS does not encrypt PHP-to-daemon management traffic. Use a trusted management network or TLS proxy, firewall the backend and protect PHP session storage. The standard daemon's cleartext warning is real, not an assurance that an exposed listener is safe.
- Harbour remote health accepts a hostname or IPv4 address, not a URI or IPv6 literal. The fixed 8192-byte buffer fails on a larger reply rather than displaying truncated JSON.
- Native TLS, local-process health and safe application recovery after a real connection failure are separate concerns.
- No automatic reconnect or replay is added.
- This release does not guarantee survival of host OOM/SIGKILL, unlimited client sessions, or immunity to resource exhaustion. A client pool can consume multiple server sessions per application process; plan server capacity accordingly.

## Validation and build provenance

The reviewed upstream source passed all 11 source CI checks, covering Windows x86/x64, macOS, Linux normal/TLS, Harbour, PHP and SQL gates:
https://github.com/FiveTechSoft/OpenADS/actions/runs/38024837993

The exact versioned candidate passed its source checks and all five package build legs across the original run and recovery runs. The artifact build applied the reviewed patch and checked its resulting Git tree in each job:
https://github.com/bedipritpal/OpenADS/actions/runs/38027722881

The first older-Linux and macOS attempts stopped before compilation because of build-runner mechanics: Git safe.directory ordering and a missing sha256sum command. Their corrected recovery jobs passed. The original combined run remains failed; it is not presented as wholly green:
https://github.com/bedipritpal/OpenADS/actions/runs/38028322171
https://github.com/bedipritpal/OpenADS/actions/runs/38028898158

Archive checks covered ZIP CRC/tar readability, required files and version stamps, aliases, matching Harbour glue/display source, Windows PE architecture and exports, x86 stdcall health exports, MinGW import symbols, macOS universal binary headers, and the older-Linux glibc ceiling. Windows package jobs also ran the MinGW static/import checks and health PE-link probes.

Fresh local normal Linux ACE/daemon builds and selected regression tests passed: 50 cases and 1,027 assertions covering health, path jail, open budgets, management, idle policy, CDX and encryption. PHP management sandbox and renderer contract tests passed. This selected local run is not a claim of full local CTest coverage. The Ubuntu-24 binary was not run in the older local inspection environment; Pritpal ran it on his Ubuntu-24 EC2 server.

## SHA-256 checksums

```text
f216341c0b0123750a3feab3f8dd6744cd836e02611622748205074f98b0eee4  openads-1.09.74-linux-glibc231.tar.gz
ec2078aa5a6e7c0c33a3bfac0252296d89b0ca6398b3d9b55064a4cf661fb850  openads-1.09.74-linux-x64.tar.gz
6390b7c88d0b6ffb84b32d52b3488e325989b8e27c6acd1206ab33f9e66e6212  openads-1.09.74-macos-universal.tar.gz
01ba4ff825d1ce7e6d897ad0794ef322425c9adb0554cbd79feda09295214a1b  openads-1.09.74-windows-x64.zip
a38325db08bd8ebc23a165796f22c6927b2e7642b6e0d99987fd6308941054ac  openads-1.09.74-windows-x86.zip
```
