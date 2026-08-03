#!/usr/bin/env bash
# Regression tests for scripts/derive-update-pubkey.sh.
#
# tests/test_update_pubkey.cpp already drives the script's FILE mode as
# part of its end-to-end signer/verifier interop check. This covers what
# that test does not: the --base64 mode (the shape CI actually uses, since
# TRAILER_UPDATE_SIGNING_KEY is a base64-encoded PEM), and the rejection
# paths — where a silent wrong answer is worse than a loud failure,
# because it produces a build that ships and then rejects every signature.
#
# Exits non-zero on the first failed assertion; prints a PASS/FAIL summary.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT="$REPO_ROOT/scripts/derive-update-pubkey.sh"
TMPDIR_="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_"' EXIT

PASSES=0
FAILURES=0

ok()   { PASSES=$((PASSES + 1));   printf 'ok   %s\n' "$1"; }
bad()  { FAILURES=$((FAILURES + 1)); printf 'FAIL %s\n     %s\n' "$1" "${2:-}"; }

KEY="$TMPDIR_/key.pem"
openssl genpkey -algorithm ed25519 -out "$KEY" 2>/dev/null

# The independent reference: what the key's public half actually is,
# computed WITHOUT going through the script under test.
REFERENCE="$(openssl pkey -in "$KEY" -pubout -outform DER | tail -c 32 | od -An -tx1 | tr -d ' \n')"

# --- the three input modes must all agree with the reference -----------
FROM_FILE="$(bash "$SCRIPT" "$KEY")"
[ "$FROM_FILE" = "$REFERENCE" ] \
  && ok "file mode matches openssl directly" \
  || bad "file mode matches openssl directly" "got $FROM_FILE want $REFERENCE"

FROM_STDIN="$(bash "$SCRIPT" - < "$KEY")"
[ "$FROM_STDIN" = "$REFERENCE" ] \
  && ok "stdin mode matches" \
  || bad "stdin mode matches" "got $FROM_STDIN"

FROM_B64="$(base64 < "$KEY" | tr -d '\n' | bash "$SCRIPT" --base64 -)"
[ "$FROM_B64" = "$REFERENCE" ] \
  && ok "--base64 mode matches (the shape the GitHub secret uses)" \
  || bad "--base64 mode matches" "got $FROM_B64"

# --- shape of the output ------------------------------------------------
[ "${#FROM_FILE}" -eq 64 ] \
  && ok "output is exactly 64 hex chars" \
  || bad "output is exactly 64 hex chars" "length ${#FROM_FILE}"

case "$FROM_FILE" in
  *[!0-9a-f]*) bad "output is lowercase hex only" "got $FROM_FILE" ;;
  *)           ok "output is lowercase hex only" ;;
esac

# The 32-vs-44 trap this script exists to hide: the raw key must be the
# LAST 32 bytes of the DER SubjectPublicKeyInfo, not the whole 44.
FULL_DER="$(openssl pkey -in "$KEY" -pubout -outform DER | od -An -tx1 | tr -d ' \n')"
[ "$FROM_FILE" != "$FULL_DER" ] && [ "${FULL_DER: -64}" = "$FROM_FILE" ] \
  && ok "trims the DER SPKI prefix (44 bytes -> last 32)" \
  || bad "trims the DER SPKI prefix" "full DER $FULL_DER"

# --- rejection paths ----------------------------------------------------
bash "$SCRIPT" "$TMPDIR_/nope.pem" >/dev/null 2>&1 \
  && bad "missing key file is rejected" "exited 0" \
  || ok "missing key file is rejected"

printf 'not a pem at all\n' > "$TMPDIR_/garbage.pem"
bash "$SCRIPT" "$TMPDIR_/garbage.pem" >/dev/null 2>&1 \
  && bad "non-PEM input is rejected" "exited 0" \
  || ok "non-PEM input is rejected"

printf 'this is not base64 %%%%\n' | bash "$SCRIPT" --base64 - >/dev/null 2>&1 \
  && bad "invalid base64 is rejected" "exited 0" \
  || ok "invalid base64 is rejected"

# An RSA key is valid PEM and openssl will happily emit its public half —
# it is just not 32 bytes. The length check must catch it rather than
# emitting a truncated "key".
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "$TMPDIR_/rsa.pem" 2>/dev/null
bash "$SCRIPT" "$TMPDIR_/rsa.pem" >/dev/null 2>&1 \
  && bad "a non-ed25519 key is rejected" "exited 0 — would have emitted a truncated key" \
  || ok "a non-ed25519 key is rejected"

bash "$SCRIPT" >/dev/null 2>&1 \
  && bad "no arguments is rejected" "exited 0" \
  || ok "no arguments is rejected"

echo
echo "derive-update-pubkey: $PASSES passed, $FAILURES failed"
[ "$FAILURES" -eq 0 ] || exit 1
