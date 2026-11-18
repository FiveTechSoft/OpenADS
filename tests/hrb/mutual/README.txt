MTFIX48 MUTUAL AUTH TEST CONTRACT

Test only. Do not deploy this public-keyless mock HRB to a real server.
The fixture HMAC key is public, embedded in its sources, and provides NO real
license or identity assurance. Choose real trust material before production.
Shared-secret vs public-key design remains a Vouch decision.

Server auth.hrb exports BOTH:
 OADS_AUTH_SERVER_V2(cNonce32,cTranscript,cResolvedPath) -> binary server proof
 OADS_AUTH_VERIFY_V2(cNonce32,cTranscript,cClientProof) -> OAUTH result
A partial pair fails startup. With the pair present legacy login is denied.
V1-only modules retain their old behavior. V2 clients never fall back to V1.
No license/proof algorithm or key is in the DLL. HRB returns explicit database
credentials; the normal server database/ini credential checks still apply.
Server proof is 1..4096 bytes. Client proof is 1..4096 bytes. Nonce lifetime
is30 seconds, single-use, consumed before verification, including bad input.
Worker limits:4 concurrent by default, no queue,2-second timeout each call.

Client C API:
 OAdsBeginMutualAuth(url,firm,&pending,nonce32,binding32,proof,&proofBytes)
 proof must point to4096-byte capacity; proofBytes starts at4096.
 Returns local binding, nonce, bounded opaque server proof and pending handle.
 Harbour: cProof := OADS_BEGINMUTUAL(cUrl,cFirm,@hPending,@cNonce,@cBinding)
Vouch MUST independently validate cProof using trusted material, then create
its distinct client proof. On failure call OADS_CANCELAUTH(hPending).
After verification: hConn := OADS_COMPLETEAUTH(hPending,cClientProof).
Do not send a plain challenge echo. The real HRB must reject it.

Transcript bytes, EXACTLY (no hex conversion):
 literal ASCII "OpenADS-mutual-v2" (17 bytes)
 u32LE(32),nonce32
 u32LE(32),binding32
 u32LE(firm byte length),firm bytes (1..256 bytes)
 u32LE(requested path byte length),requested path bytes (max4096)
Path is the exact URL path parsed by the DLL, not the server-normalized root.
HRB gets the normalized server-owned root separately in the issue hook.
Any real proof format must authenticate the complete transcript, with distinct
server/client directions. A length-framed transcript is not itself a proof.

TLS binding is32-byte RFC8446 section7.5 exporter using:
 label="EXPORTER-OpenADS-Mutual-v2", empty context, output length32.
Each endpoint derives it from its OWN TLS1.3 session. Never accept a binding
supplied by a peer as the local binding. Full TLS1.3 handshake only; no0RTT.
The mbedTLS3.6.2 adapter accesses pinned negotiated session internals because
that release has no public normal-exporter API. Revalidate on library upgrade.
A TLS-terminating live relay has different bindings and cannot forward proofs.
A transparent encrypted TCP relay is harmless forwarding, not TLS termination.

Two application request/reply exchanges remain: BeginAuth/CompleteAuth.
Proofs piggyback, adding no extra application RTT. This is NOT total one RTT
including TLS handshaking. Cancellation/reconnect starts a new attempt.
Ephemeral TLS certificates remain internal; no owner PKI distribution burden.
Transport encryption alone does not authenticate the server.

Tests include independently local matching bindings; valid mock login; wrong
key, tamper, nonce echo, reflection, replay, firm swap, oversize, expiry,
plain-transport rejection, legacy downgrade and real terminating TLS relay.
The relay test also forces completion after ignoring the bad server proof;
the real server still rejects the wrong-channel client proof.
These tests do not certify a future HRB algorithm or protection from endpoint
compromise/key extraction. Full Vouch acceptance remains required before main.

WINDOWS EXTRACTION SETUP
ZIP extraction does not preserve the protected HRB ACL used during testing.
Before starting the server with mutual-test-only.hrb, follow ACL-SETUP.txt.
That step applies only to the public mock file, not your real auth module.
Use the matching openads_auth_worker.exe beside the new daemon.

MTFIX48 WORKER HRB ADDITIONS
Explicit callable symbols: hb_DateTime,hb_TSToUTC,hb_TSToStr,hb_StrToTS,
hb_TToS,hb_SToT,hb_UTCOffset,hb_UTCToTS. Native time_probe.hrb executes all
of them inside the actual worker. This does not add timestamp fields or
clock-skew rules to the existing mutual-auth protocol. hb_TimeToUTC is not
the function name; use hb_TSToUTC. Client-host HRB symbols remain Vouch's
responsibility. No compiler or Harbour upgrade.
