---
title: TLS deployment
layout: default
parent: Home (EN)
nav_order: 7
permalink: /en/tls-deployment/
---

# TLS deployment for Studio

OpenADS Studio listens on **plaintext HTTP** today
(`openads_serverd --http-port N`). The wire protocol has its
own `tls://` URI for client-side TLS (M12.12, vendored
mbedtls 3.6 LTS), but the embedded HTTP console is wired
through `cpp-httplib`, which only ships TLS via OpenSSL —
not bundled in the daemon to keep the binary lean.

To run Studio on HTTPS in production, terminate TLS in front
of the daemon. Three battle-tested options follow; all of them
keep `openads_serverd` listening on `127.0.0.1` so only the
proxy is reachable from the network.

## Option 1 — Caddy (recommended)

Caddy auto-provisions a Let's Encrypt certificate, redirects
HTTP → HTTPS, sets sane TLS defaults, and reloads on config
change. **Caddyfile**:

```caddyfile
studio.example.com {
    reverse_proxy 127.0.0.1:6263
}
```

```sh
# install (Linux)
sudo apt-get install caddy
sudo nano /etc/caddy/Caddyfile        # paste the snippet above
sudo systemctl reload caddy
```

The studio is now reachable at
`https://studio.example.com/`. The cert is renewed
automatically; Caddy serves OCSP-stapled responses by default.

## Option 2 — nginx

```nginx
server {
    listen 443 ssl http2;
    server_name studio.example.com;

    ssl_certificate     /etc/letsencrypt/live/studio.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/studio.example.com/privkey.pem;

    # Proxy everything to the local Studio port.
    location / {
        proxy_pass         http://127.0.0.1:6263;
        proxy_http_version 1.1;
        proxy_set_header   Host              $host;
        proxy_set_header   X-Real-IP         $remote_addr;
        proxy_set_header   X-Forwarded-For   $proxy_add_x_forwarded_for;
        proxy_set_header   X-Forwarded-Proto https;
    }
}

server {
    listen 80;
    server_name studio.example.com;
    return 301 https://$server_name$request_uri;
}
```

Pair with `certbot --nginx -d studio.example.com` for a
Let's Encrypt cert.

## Option 3 — stunnel (TLS-only termination)

Smaller surface than full HTTP proxies — stunnel just wraps the
plaintext socket in TLS. Useful when an existing infra runs no
web server.

```ini
# /etc/stunnel/openads.conf
[studio]
accept  = 0.0.0.0:443
connect = 127.0.0.1:6263
cert    = /etc/stunnel/openads-fullchain.pem
key     = /etc/stunnel/openads-key.pem
```

```sh
sudo apt-get install stunnel4
sudo systemctl enable --now stunnel4
```

## Option 4 — SSH tunnel (zero-config, single user)

When only one admin needs to reach Studio and there's already
an SSH login, no proxy is needed:

```sh
ssh -L 6263:localhost:6263 user@server.example.com
# now open http://localhost:6263 in any browser
```

The SSH session encrypts the entire HTTP traffic; the daemon
keeps listening on `127.0.0.1:6263` inside the remote box.

## Recommended deployment posture

```
+--------+      HTTPS        +-------+   plaintext HTTP   +-----------------+
| Browser| ---------------→ | Caddy | -----------------→ | openads_serverd |
+--------+   public TLS      +-------+    127.0.0.1:6263  +-----------------+
                              ↑                                ↓
                            Let's Encrypt                  data dir
```

Daemon flags for this shape:

```sh
openads_serverd \
    --port 6262 \
    --http-port 6263 \
    --data /srv/openads/data \
    --http-user admin:strong-password
# bind --http-port to 127.0.0.1 by passing --host 127.0.0.1 if you don't
# want any path other than the proxy reaching the console.
```

Pair the proxy's TLS with **`--http-user`** so even on a hijacked
TLS terminator the credentials are required for any API call.

## Native HTTPS in serverd

A dedicated `OPENADS_WITH_OPENSSL=ON` CMake option is on the
roadmap. It would link cpp-httplib's OpenSSL backend, accept
`--tls-cert <pem> --tls-key <pem>` flags, and let serverd serve
HTTPS without a proxy. Status: planned for a future
`studio.web.x` milestone; until then, terminate TLS at a proxy.


## Safe listener defaults (security hardening)

`openads_serverd` now binds `127.0.0.1` by default, including the setup
wizard. Explicit `host` values in existing INI files still override this default. Existing clients and wire payloads are unchanged.
A non-loopback bind without `auth_user` is rejected unless the operator
explicitly selects `--allow_anonymous` (`allow_anonymous=true` in INI).
The same gate applies to an enabled Studio listener without `http_user`.
All non-loopback TCP listeners emit a cleartext warning, including those
with authentication. **This is exposure mitigation, not native TLS**:
use the documented TLS proxy, and firewall its cleartext backend so only
the proxy can reach it. Do not expose TCP credentials to untrusted networks.
