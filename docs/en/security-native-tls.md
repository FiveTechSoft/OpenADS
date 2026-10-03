# Native TLS data listener

Build with OPENADS_WITH_TLS=ON. Run openads_serverd with
`--tls_cert server-chain.pem --tls_key server-key.pem`. Both files are required;
the unencrypted PEM private key must be readable only by the service account.
Startup rejects unreadable files, invalid PEM and a certificate/key mismatch.
All primary and extra TCP data listeners then require TLS. There is no plain
fallback. Clients use tls:// and a trusted CA bundle with hostname verification.
Use a certificate whose SAN matches the client hostname.

The accepted socket uses mbedTLS BIO callbacks. Handshake and reads are
non-blocking in both the reactor and legacy session loops. A handshake/Connect
must finish within 30 seconds. Replies use bounded one-frame backpressure and a
30-second write deadline. Stalled clients do not busy-wait inside a worker.

This does not add HTTPS to Studio. Keep Studio behind a TLS proxy and firewall.
Without the TLS flags, legacy plain TCP still works and serverd warns on a
non-loopback bind. TLS server mode authenticates users through Connect, not
client certificates. Private keys are not logged. Certificate renewal is the
operator's responsibility and requires a restart.
