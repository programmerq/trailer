#!/usr/bin/env bash
# Regression tests for scripts/find-openssl-ed25519.sh.
#
# The bug this guards is the one that red-gated the 2026-08-04 macOS
# nightly (run 30907188776, job 91985006187): a guard that checked
# whether *an* openssl exists rather than whether it can do the job.
# macOS's LibreSSL answers `openssl version` and then rejects
# `genpkey -algorithm ed25519`, so tests/test_update_pubkey.cpp FAILED
# where it should have SKIPPED, and the DMG was never built.
#
# The macOS behaviour is reproduced here ON LINUX with a stub `openssl`
# that refuses exactly the way LibreSSL refuses — which is the whole
# point: no Mac is needed to prove the resolver rejects it, and this
# stays a guard on every PR from ci.yml's Linux-only version-gating job.
#
# Each negative case ships a stub `brew` alongside the stub `openssl`,
# so a developer running this on a Mac with a real Homebrew openssl@3
# installed gets the same verdict as CI instead of the resolver quietly
# finding a good binary and turning a negative case green.
#
# Exits non-zero on the first failed assertion; prints a PASS/FAIL summary.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT="$REPO_ROOT/scripts/find-openssl-ed25519.sh"
TMPDIR_="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_"' EXIT

PASSES=0
FAILURES=0

ok()  { PASSES=$((PASSES + 1));     printf 'ok   %s\n' "$1"; }
bad() { FAILURES=$((FAILURES + 1)); printf 'FAIL %s\n     %s\n' "$1" "${2:-}"; }

# The real openssl this host has, if any — several cases need one to
# stand in for "a good binary somewhere else".
REAL_OPENSSL="$(bash "$SCRIPT" 2>/dev/null)" || REAL_OPENSSL=""

# --- stub toolchains ----------------------------------------------------

# A LibreSSL impersonator: answers `version`, refuses ed25519 genpkey with
# LibreSSL's own wording and its usage dump, exactly as seen in the
# nightly log.
mk_libressl_stub() {
    local dir="$1"
    mkdir -p "$dir"
    cat > "$dir/openssl" <<'STUB'
#!/usr/bin/env bash
case "${1:-}" in
  version)
    echo "LibreSSL 3.3.6"
    exit 0
    ;;
  genpkey)
    echo "Algorithm ed25519 not found" >&2
    echo "usage: genpkey [-algorithm alg] [cipher] [-genparam] [-out file]" >&2
    echo "    [-outform der | pem] [-paramfile file] [-pass arg]" >&2
    exit 1
    ;;
esac
echo "unknown command" >&2
exit 1
STUB
    chmod +x "$dir/openssl"
    mk_failing_brew "$dir"
}

# A brew that knows nothing, so the macOS discovery branch contributes no
# candidate. Without this, these cases would pass on Linux (no brew) and
# fail on a Mac that has openssl@3 — a test that only holds on one OS.
mk_failing_brew() {
    local dir="$1"
    cat > "$dir/brew" <<'STUB'
#!/usr/bin/env bash
exit 1
STUB
    chmod +x "$dir/brew"
}

# --- 1. the happy path on any host that has a real openssl --------------
if [ -n "$REAL_OPENSSL" ]; then
    [ -x "$REAL_OPENSSL" ] \
      && ok "prints an executable path on a host with a capable openssl" \
      || bad "prints an executable path" "got '$REAL_OPENSSL'"

    # The returned binary must genuinely do the thing, not merely exist —
    # this asserts the script's contract rather than trusting its exit code.
    if "$REAL_OPENSSL" genpkey -algorithm ed25519 -out "$TMPDIR_/real.pem" 2>/dev/null \
       && [ -s "$TMPDIR_/real.pem" ]; then
        ok "the path it returns really can generate an ed25519 key"
    else
        bad "the path it returns really can generate an ed25519 key" "$REAL_OPENSSL could not"
    fi
else
    printf 'note: no capable openssl on this host — positive cases skipped\n'
fi

# --- 2. THE REGRESSION: a LibreSSL-shaped openssl must be rejected ------
LIBRESSL_DIR="$TMPDIR_/libressl"
mk_libressl_stub "$LIBRESSL_DIR"

OUT="$(PATH="$LIBRESSL_DIR:$PATH" TRAILER_OPENSSL='' bash "$SCRIPT" 2>"$TMPDIR_/err.txt")"
RC=$?
[ "$RC" -ne 0 ] \
  && ok "a LibreSSL-shaped openssl is REJECTED (the macOS nightly failure)" \
  || bad "a LibreSSL-shaped openssl is REJECTED" "exited 0 and printed '$OUT'"

[ -z "$OUT" ] \
  && ok "prints nothing on stdout when it fails (safe to \$(...) capture)" \
  || bad "prints nothing on stdout when it fails" "stdout was '$OUT'"

grep -qi 'ed25519' "$TMPDIR_/err.txt" \
  && ok "the failure names ed25519, so a log reader knows what is missing" \
  || bad "the failure names ed25519" "stderr: $(cat "$TMPDIR_/err.txt")"

grep -qi 'libressl' "$TMPDIR_/err.txt" \
  && ok "the failure names LibreSSL, the actual cause on macOS" \
  || bad "the failure names LibreSSL" "stderr: $(cat "$TMPDIR_/err.txt")"

# --- 3. capability beats position: a bad override falls through ---------
# TRAILER_OPENSSL is consulted FIRST, so pointing it at the stub proves
# the probe rejects even an explicitly requested binary and keeps looking,
# rather than trusting whatever it was handed.
if [ -n "$REAL_OPENSSL" ]; then
    FALLTHROUGH="$(TRAILER_OPENSSL="$LIBRESSL_DIR/openssl" bash "$SCRIPT" 2>/dev/null)"
    if [ "$FALLTHROUGH" = "$REAL_OPENSSL" ]; then
        ok "an incapable \$TRAILER_OPENSSL falls through to a capable one"
    else
        bad "an incapable \$TRAILER_OPENSSL falls through" "got '$FALLTHROUGH' want '$REAL_OPENSSL'"
    fi

    # ...and a capable override is honoured over PATH.
    OVERRIDE_DIR="$TMPDIR_/override"
    mkdir -p "$OVERRIDE_DIR"
    ln -s "$REAL_OPENSSL" "$OVERRIDE_DIR/openssl"
    PICKED="$(PATH="$LIBRESSL_DIR:$PATH" TRAILER_OPENSSL="$OVERRIDE_DIR/openssl" bash "$SCRIPT" 2>/dev/null)"
    [ "$PICKED" = "$OVERRIDE_DIR/openssl" ] \
      && ok "a capable \$TRAILER_OPENSSL is honoured over a LibreSSL on PATH" \
      || bad "a capable \$TRAILER_OPENSSL is honoured" "got '$PICKED'"
fi

# --- 4. the whole chain is probed, not just key generation --------------
# A hypothetical fork that gains ed25519 genpkey but still lacks
# `pkeyutl -rawin` must be rejected HERE (an honest skip) rather than
# passing the gate and failing later inside sign-update-feed.sh.
if [ -n "$REAL_OPENSSL" ]; then
    HALF_DIR="$TMPDIR_/halfway"
    mkdir -p "$HALF_DIR"
    cat > "$HALF_DIR/openssl" <<STUB
#!/usr/bin/env bash
if [ "\${1:-}" = "pkeyutl" ]; then
  echo "pkeyutl: unknown option -rawin" >&2
  exit 1
fi
exec "$REAL_OPENSSL" "\$@"
STUB
    chmod +x "$HALF_DIR/openssl"
    mk_failing_brew "$HALF_DIR"

    PATH="$HALF_DIR:$PATH" TRAILER_OPENSSL='' bash "$SCRIPT" >/dev/null 2>"$TMPDIR_/half-err.txt" \
      && bad "an openssl without pkeyutl -rawin is rejected" "exited 0" \
      || ok "an openssl without pkeyutl -rawin is rejected"

    grep -qi 'rawin' "$TMPDIR_/half-err.txt" \
      && ok "...and the failure names the operation that refused" \
      || bad "the failure names pkeyutl -rawin" "stderr: $(cat "$TMPDIR_/half-err.txt")"
fi

# --- 5. no openssl at all is a clean failure, not a crash ---------------
EMPTY_DIR="$TMPDIR_/empty"
mkdir -p "$EMPTY_DIR"
mk_failing_brew "$EMPTY_DIR"
# A PATH holding ONLY the handful of tools the resolver itself needs, and
# no openssl. Shadowing is not an option — `command -v` finds the first
# executable of that name anywhere on PATH — so the sole entry has to be
# a dir we control, stocked with just enough to let the script run far
# enough to report the zero-candidate case rather than dying on it.
for tool in bash mktemp rm head; do
    ln -s "$(command -v "$tool")" "$EMPTY_DIR/$tool"
done
PATH="$EMPTY_DIR" TRAILER_OPENSSL='' bash "$SCRIPT" >/dev/null 2>"$TMPDIR_/none-err.txt" \
  && bad "no openssl anywhere is a clean failure" "exited 0" \
  || ok "no openssl anywhere is a clean failure"

grep -qi 'no openssl' "$TMPDIR_/none-err.txt" \
  && ok "...and says so rather than dying on an unbound variable" \
  || bad "reports the no-candidate case" "stderr: $(cat "$TMPDIR_/none-err.txt")"

echo
echo "find-openssl-ed25519: $PASSES passed, $FAILURES failed"
[ "$FAILURES" -eq 0 ] || exit 1
