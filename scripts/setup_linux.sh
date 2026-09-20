#!/usr/bin/env bash
# OpenADS — one-shot Linux server bring-up from a release tarball.
#
#   ./scripts/setup_linux.sh [VERSION] [--opdump] [--dir DIR] [--force]
#   bash scripts/setup_linux.sh 1.09.66 --opdump
#   curl -fsSL https://raw.githubusercontent.com/FiveTechSoft/OpenADS/main/scripts/setup_linux.sh | bash -s -- 1.09.66
#
# VERSION is "1.09.66" or "v1.09.66" (default: $OPENADS_VERSION else 1.09.66).
# --opdump turns on the server opcode census (noisy; off by default).
# --dir sets the base directory (default: current directory).
# Idempotent — safe to re-run (skips download/extract/config when present;
# --force re-downloads and re-extracts).

set -euo pipefail

OPENADS_VERSION_DEFAULT="1.09.66"
OPENADS_REPO="${OPENADS_REPO:-https://github.com/FiveTechSoft/OpenADS}"

log() { printf '\033[1;36m[setup]\033[0m %s\n' "$*"; }
ok()  { printf '\033[1;32m[ ok ]\033[0m %s\n' "$*"; }
warn(){ printf '\033[1;33m[warn]\033[0m %s\n' "$*"; }
fail(){ printf '\033[1;31m[fail]\033[0m %s\n' "$*"; exit 1; }

# ---------- argument parsing ----------
VERSION="${OPENADS_VERSION:-$OPENADS_VERSION_DEFAULT}"
BASE_DIR="$PWD"
OPDUMP=0
FORCE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --opdump) OPDUMP=1; shift ;;
        --force)  FORCE=1; shift ;;
        --dir)    [[ $# -ge 2 ]] || fail "--dir needs a value: --dir DIR";
                  BASE_DIR="$2"; shift 2 ;;
        --dir=*)  BASE_DIR="${1#--dir=}"; shift ;;
        -h|--help)
            sed -n '2,10p' "$0"
            exit 0 ;;
        -*) fail "Unknown flag: $1 (see --help)" ;;
        *)  VERSION="$1"; shift ;;
    esac
done
VERSION="${VERSION#v}"  # tolerate "v1.09.66"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail "Bad version: '$VERSION' (want 1.09.66)"

# ---------- platform sanity ----------
[[ "$(uname -s)" == "Linux" ]] || fail "This script is for Linux."
[[ "$(uname -m)" == "x86_64" ]] || fail "Only x86_64 tarballs are published (found $(uname -m))."
command -v tar >/dev/null || fail "tar not found (apt install tar)."
if command -v curl >/dev/null; then FETCH="curl -fsSL -o"
elif command -v wget >/dev/null; then FETCH="wget -q -O"
else fail "Need curl or wget (apt install curl)."
fi

TARBALL="openads-${VERSION}-linux-x64.tar.gz"
URL="${OPENADS_REPO}/releases/download/v${VERSION}/${TARBALL}"
STAGE="${BASE_DIR}/${TARBALL%.tar.gz}"

mkdir -p "$BASE_DIR"
cd "$BASE_DIR"

# ---------- 1. download ----------
if [[ -f "$TARBALL" && "$FORCE" -eq 0 ]]; then
    log "Reusing $TARBALL (use --force to re-download)"
else
    log "Downloading $URL"
    # shellcheck disable=SC2086
    $FETCH "$TARBALL" "$URL" || fail "Download failed — check the version exists at $OPENADS_REPO/releases"
    ok "Saved $TARBALL"
fi

# ---------- 2. extract ----------
if [[ -d "$STAGE" && "$FORCE" -eq 0 ]]; then
    log "Reusing $STAGE"
else
    log "Extracting $TARBALL"
    rm -rf "$STAGE"
    tar -xzf "$TARBALL"
fi
[[ -x "$STAGE/openads_serverd" ]] || fail "$STAGE/openads_serverd missing after extract"
ok "Server: $STAGE/openads_serverd"
"$STAGE/openads_serverd" --version 2>&1 | sed 's/^/  /' || true

# ---------- 3. config (never overwrite an existing one) ----------
cd "$STAGE"
if [[ -f openads.ini ]]; then
    log "Reusing existing openads.ini"
else
    [[ -f openads.ini.sample ]] || fail "openads.ini.sample missing in $STAGE"
    cp openads.ini.sample openads.ini
    ok "Created openads.ini from sample (data dir defaults to .)"
fi

# ---------- 4. run (replaces this shell; Ctrl-C stops the server) ----------
log "Starting openads_serverd --config ./openads.ini (Ctrl-C to stop)"
if [[ "$OPDUMP" -eq 1 ]]; then
    warn "Opcode census ON (verbose)"
    export OPENADS_OPDUMP=1
fi
exec ./openads_serverd --config ./openads.ini
