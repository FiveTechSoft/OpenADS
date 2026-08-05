---
title: Service deployment
layout: default
parent: Home (EN)
nav_order: 8
permalink: /en/service-deployment/
---

# Service deployment

Since **v1.0.0-rc14** the same `openads_serverd` binary doubles as
an OS-native service on all three platforms. The graceful-shutdown
path is shared with the interactive Ctrl+C handler, so a clean
service stop runs the same cleanup (flush open tables, drain
in-flight wire frames, release locks).

## Windows — SCM service

The binary self-registers via the Service Control Manager:

```bat
:: Register (SERVICE_AUTO_START at boot). Flags after
:: --install-service are baked into the registered binPath.
openads_serverd --install-service ^
    --port 6262 ^
    --http-port 6263 ^
    --data C:\app\data ^
    --error-log-path C:\OpenADS\logs

:: Start it via SCM
sc start openads_serverd

:: Stop / remove
sc stop  openads_serverd
openads_serverd --uninstall-service
```

Run an elevated `cmd` for the install / uninstall steps.

Without `--error-log-path` (or `OPENADS_ERROR_LOG_PATH` /
`error_log_path` in an ini baked into binPath), the service uses
the platform default for `ads_err.dbf` — on Windows often
`C:\ads_err.dbf`. Prefer a dedicated writable folder for production.
See [Error log and journal paths](error-log/).

The service runs as `SERVICE_WIN32_OWN_PROCESS`. The control
handler honours `SERVICE_CONTROL_STOP` and
`SERVICE_CONTROL_SHUTDOWN`. Running the binary with `--service`
**outside** the SCM prints an explanation rather than hanging.

## Linux — systemd

`scripts/openads-serverd.service`:

```ini
[Unit]
Description=OpenADS Server Daemon
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=openads
ExecStart=/usr/local/bin/openads_serverd \
    --port 6262 --http-port 6263 \
    --data /var/lib/openads \
    --error-log-path /var/log/openads
Restart=on-failure

# Hardening
ProtectSystem=strict
NoNewPrivileges=yes
PrivateTmp=yes
RestrictAddressFamilies=AF_INET AF_INET6
ReadWritePaths=/var/lib/openads /var/log/openads

[Install]
WantedBy=multi-user.target
```

Install + enable:

```sh
sudo useradd --system --no-create-home openads
sudo cp scripts/openads-serverd.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now openads-serverd
sudo systemctl status openads-serverd
```

## macOS — launchd

`scripts/com.openads.serverd.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>com.openads.serverd</string>
  <key>ProgramArguments</key>
  <array>
    <string>/usr/local/bin/openads_serverd</string>
    <string>--port</string><string>6262</string>
    <string>--http-port</string><string>6263</string>
    <string>--data</string><string>/usr/local/var/openads</string>
  </array>
  <key>KeepAlive</key><true/>
  <key>RunAtLoad</key><true/>
  <key>StandardOutPath</key>
  <string>/var/log/openads-serverd.out.log</string>
  <key>StandardErrorPath</key>
  <string>/var/log/openads-serverd.err.log</string>
</dict>
</plist>
```

Install system-wide (boot-time start):

```sh
sudo cp scripts/com.openads.serverd.plist /Library/LaunchDaemons/
sudo launchctl load /Library/LaunchDaemons/com.openads.serverd.plist
```

Or per-user (login-time start):

```sh
cp scripts/com.openads.serverd.plist ~/Library/LaunchAgents/
launchctl load ~/Library/LaunchAgents/com.openads.serverd.plist
```

## Health check

```sh
curl http://localhost:6263/api/health
# {"status":"ok","mode":"remote-server","version":"1.0.0-rc23", ... }
```

The `mode` field is what the Studio mode badge uses (rc10).
