#!/usr/bin/env bash
# Derive the 32-byte ed25519 PUBLIC key from the update-signing PRIVATE
# key, and print it as 64 lowercase hex characters.
#
# This is the single source of truth for that derivation. CI calls it to
# feed -DTRAILER_UPDATE_PUBKEY into the build (nightly.yml's "Derive the
# update-channel public key" step); a developer holding the private key
# can call it the same way to produce an update-capable local build:
#
#   cmake -S . -B build \
#     -DTRAILER_UPDATE_PUBKEY="$(scripts/derive-update-pubkey.sh key.pem)"
#
# Keeping it in one place matters because the incantation is easy to get
# subtly wrong: an Ed25519 SubjectPublicKeyInfo in DER is 44 bytes, of
# which only the LAST 32 are the raw key the verifier needs. Embedding
# the full 44 (or the PEM, or a base64 of either) produces a build that
# compiles, ships, and then rejects every signature it is given.
#
# The PUBLIC key is not a secret — printing it to a CI log is fine and
# intentional. The PRIVATE key never leaves this process: it is read from
# a file or stdin and piped straight into openssl, never written to disk
# by this script and never echoed.
#
# Usage:
#   derive-update-pubkey.sh <private-key.pem>     # from a file
#   derive-update-pubkey.sh -                     # PEM on stdin
#   derive-update-pubkey.sh --base64 -            # base64-of-PEM on stdin
#                                                 # (the shape of the
#                                                 # TRAILER_UPDATE_SIGNING_KEY
#                                                 # GitHub secret)
set -euo pipefail

BASE64=0
if [ "${1:-}" = "--base64" ]; then
    BASE64=1
    shift
fi

if [ "$#" -ne 1 ]; then
    echo "usage: $0 [--base64] <private-key.pem|->" >&2
    exit 2
fi
SRC="$1"

if ! command -v openssl >/dev/null 2>&1; then
    echo "derive-update-pubkey.sh: openssl not found on PATH" >&2
    exit 1
fi

# Read the key into a variable rather than a temp file so the private
# half never lands on disk under this script's control.
if [ "$SRC" = "-" ]; then
    KEY_DATA="$(cat)"
else
    if [ ! -f "$SRC" ]; then
        echo "derive-update-pubkey.sh: private key not found: $SRC" >&2
        exit 1
    fi
    KEY_DATA="$(cat "$SRC")"
fi

if [ "$BASE64" -eq 1 ]; then
    if ! KEY_DATA="$(printf '%s' "$KEY_DATA" | base64 -d 2>/dev/null)"; then
        echo "derive-update-pubkey.sh: input is not valid base64" >&2
        exit 1
    fi
fi

# Take the WHOLE SubjectPublicKeyInfo and validate it, rather than
# blindly trimming the tail. `tail -c 32` alone is not a check: an RSA or
# P-256 key also yields 32 trailing bytes, so it would emit 64 plausible
# hex characters of garbage and produce a build that ships and then
# rejects every signature it is given — the precise failure this whole
# arrangement exists to prevent. Only Ed25519 has this exact 44-byte
# encoding.
#
# `od` rather than `xxd`: xxd ships with vim, which is not guaranteed on
# a minimal runner image, while od is coreutils and is already relied on
# elsewhere in this repo's scripts.
DER_HEX="$(printf '%s\n' "$KEY_DATA" \
    | openssl pkey -pubout -outform DER 2>/dev/null \
    | od -An -tx1 \
    | tr -d ' \n')"

# 302a300506032b6570032100 = SEQUENCE { SEQUENCE { OID 1.3.101.112 (Ed25519) },
# BIT STRING (33 bytes, 0 unused) } — the fixed 12-byte prefix on every
# Ed25519 SPKI, followed by the 32 raw key bytes. Note 1.3.101.110
# (…2b656e…) is X25519, a DIFFERENT algorithm with an identically-sized
# encoding, which is why the OID is matched and not just the length.
ED25519_SPKI_PREFIX="302a300506032b6570032100"

if [ "${#DER_HEX}" -ne 88 ] || [ "${DER_HEX#"$ED25519_SPKI_PREFIX"}" = "$DER_HEX" ]; then
    echo "derive-update-pubkey.sh: input is not an ed25519 private key." >&2
    echo "       Expected an 88-hex-char SubjectPublicKeyInfo beginning" >&2
    echo "       $ED25519_SPKI_PREFIX; got ${#DER_HEX} hex chars${DER_HEX:+ beginning ${DER_HEX:0:24}}." >&2
    echo "       Generate a correct key with:" >&2
    echo "         openssl genpkey -algorithm ed25519 -out key.pem" >&2
    exit 1
fi

# The raw key is the last 32 bytes (64 hex chars) after that prefix.
printf '%s\n' "${DER_HEX: -64}"
