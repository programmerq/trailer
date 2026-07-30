#!/usr/bin/env bash
# Sign a nightly update-feed payload with an ed25519 private key and emit
# the on-wire signed-feed JSON (see src/update/UpdateFeedParser.h for the
# format this must match byte-for-byte).
#
# Usage: sign-update-feed.sh <payload-file> <private-key-pem> <output-file>
#
#   payload-file      UTF-8 JSON text (the exact bytes to be verified —
#                      see UpdateFeedParser.h's "signed message" note).
#   private-key-pem    An ed25519 private key in PEM format, e.g. produced by
#                      `openssl genpkey -algorithm ed25519`.
#   output-file         Where the final `{"payload":...,"signature":...}`
#                      JSON is written.
#
# Signing uses the `openssl` CLI (OpenSSL 3.0+, which supports raw ed25519
# signing via `pkeyutl -rawin` — confirmed present on GitHub Actions'
# ubuntu-latest runners) rather than a new Python/pip dependency. This is a
# BUILD-HOST tool invocation only — it has no bearing on whether OpenSSL is
# linked into the shipped Trailer binary (it deliberately is not; see
# third_party/tweetnacl/NOTICE.md for that separate question, which this
# script does not answer or change).
#
# JSON assembly uses python3's stdlib `json` module (no pip install) so the
# payload string is escaped correctly regardless of what characters it
# contains — this is the ONLY thing that matters for the verifier on the
# other end: `payload` must decode (via ordinary JSON string unescaping) to
# EXACTLY the bytes that were signed. Never hand-quote this by hand.
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "Usage: $0 <payload-file> <private-key-pem> <output-file>" >&2
  exit 2
fi

PAYLOAD_FILE="$1"
KEY_FILE="$2"
OUT_FILE="$3"

if [ ! -f "$PAYLOAD_FILE" ]; then
  echo "sign-update-feed.sh: payload file not found: $PAYLOAD_FILE" >&2
  exit 1
fi
if [ ! -f "$KEY_FILE" ]; then
  echo "sign-update-feed.sh: private key not found: $KEY_FILE" >&2
  exit 1
fi

SIG_FILE="$(mktemp)"
trap 'rm -f "$SIG_FILE"' EXIT

# -rawin: sign the payload bytes directly (no digest prefix) — ed25519
# always signs the raw message itself; there is no separate hash step to
# select, unlike RSA/ECDSA signing modes.
openssl pkeyutl -sign -inkey "$KEY_FILE" -rawin -in "$PAYLOAD_FILE" -out "$SIG_FILE"

PAYLOAD_FILE="$PAYLOAD_FILE" SIG_FILE="$SIG_FILE" python3 - "$OUT_FILE" <<'PYEOF'
import base64
import json
import os
import sys

payload_path = os.environ["PAYLOAD_FILE"]
sig_path = os.environ["SIG_FILE"]
out_path = sys.argv[1]

with open(payload_path, "r", encoding="utf-8") as f:
    payload_text = f.read()
with open(sig_path, "rb") as f:
    signature_b64 = base64.b64encode(f.read()).decode("ascii")

with open(out_path, "w", encoding="utf-8") as f:
    json.dump({"payload": payload_text, "signature": signature_b64}, f)
    f.write("\n")
PYEOF

echo "Wrote signed feed: $OUT_FILE"
