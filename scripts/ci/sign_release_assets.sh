#!/usr/bin/env bash
# Sign every release asset so the in-app updater can verify authenticity before
# installing (see src/utils/update_verify.cpp): detached ECDSA-P256 / SHA-256
# signatures published beside each asset as "<asset>.sig".
#
# The client fetches "<assetUrl>.sig" and checks it against the public key
# compiled into update_verify.cpp before handing the artifact to the platform
# installer. Transport is already TLS-verified, so this is defence-in-depth: it
# also covers a compromised release host.
#
# No-op unless the UPDATE_SIGNING_KEY secret (EC private key, PEM) is set, so
# forks release normally and client verification simply refuses those builds'
# unsigned assets rather than the workflow failing.
#
# Signs in place inside ./release, which the upload step globs as release/* —
# so the .sig files are published automatically alongside their assets.
set -euo pipefail

if [ -z "${UPDATE_SIGNING_KEY:-}" ]; then
    echo "UPDATE_SIGNING_KEY not set — skipping asset signing."
    exit 0
fi

if [ ! -d release ]; then
    echo "no release/ directory — nothing to sign."
    exit 0
fi

keyfile="$(mktemp)"
trap 'rm -f "$keyfile"' EXIT
printf '%s\n' "$UPDATE_SIGNING_KEY" > "$keyfile"

# Fail loudly if the secret isn't a usable private key, rather than silently
# publishing unsigned assets that every client would then reject.
if ! openssl pkey -in "$keyfile" -noout 2>/dev/null; then
    echo "::error::UPDATE_SIGNING_KEY is not a valid PEM private key"
    exit 1
fi

shopt -s nullglob
signed=0
for f in release/*; do
    [ -f "$f" ] || continue
    case "$f" in *.sig) continue ;; esac
    openssl dgst -sha256 -sign "$keyfile" -out "$f.sig" "$f"
    echo "signed $(basename "$f")"
    signed=$((signed + 1))
done

echo "Signed $signed asset(s)."
ls -la release/
