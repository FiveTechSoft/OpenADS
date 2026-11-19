# Optional V2 mutual authentication over TLS

This stage adds V2 on top of the V1 challenge-authentication contribution.
It ports the existing fork implementation without changing RDD behavior,
table/index semantics, releases, tags, or workflows.

## Application contract

A protected HRB exports both `OADS_AUTH_SERVER_V2` and
`OADS_AUTH_VERIFY_V2`, or neither. A partial pair fails worker startup.
A V2 module refuses V1-only login. A V1-only module keeps its existing flow.
The V2 client does not downgrade to V1.

`OAdsBeginMutualAuth` returns a 32-byte nonce, a locally derived TLS binding,
a pending handle, and an opaque server proof of 1 to 4096 bytes. The Harbour
entry point is `OADS_BEGINMUTUAL`. The application must verify that proof
using its own trusted material before calling `OADS_COMPLETEAUTH` with a
separate client proof. If verification fails, cancel the pending handle.
Neither this API nor TLS encryption alone authenticates the server.

The transcript length-frames the nonce, local TLS exporter, selector, and
exact requested path. The issue hook receives the resolved path separately.
The existing 30-second single-use challenge and database credential checks
remain in force. Proofs ride on BeginAuth and CompleteAuth: no additional
application exchange, but the TLS handshake still has its own latency.

The exporter adapter is pinned to mbedTLS 3.6.2 session internals. Revalidate
it when changing that dependency. A TLS-terminating relay derives a different
binding at each endpoint. A transparent encrypted TCP relay is only forwarding.

See `tests/hrb/mutual/README.txt` for the exact byte contract and limits.

## Tests and limits

Configure with TLS, tests, and Harbour UDF support enabled and provide the
Harbour SDK/compiler paths. Build `auth_mutual_probe`, then run
`ctest -R '^auth_mutual_v2$' --output-on-failure` in the build directory.
The mock HRB is built only for this test and is never installed or packaged.
Its key is public. It provides no production identity or license assurance.

The probe covers valid mock login through C++ and the C ABI, server proof
verification, reflection, cross-session replay, wrong key, tampering, nonce
echo, oversized proof, selector substitution, V1 downgrade, plain transport,
a live TLS-terminating relay, malformed framing, and deterministic expiry.
The caller must still implement and test its own proof algorithm and trusted
key material. These mock tests do not certify a proprietary HRB, protect keys
from endpoint compromise, or replace Windows/macOS and real-client acceptance.

Platform limit: the worker spawn path shares the V1 spike's POSIX restriction
(glibc-only close-from). Builds against other libcs, macOS included, fail
closed and cannot start the auth worker, so V2 is unavailable there.
