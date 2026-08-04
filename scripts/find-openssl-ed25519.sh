#!/usr/bin/env bash
# Print the path of an `openssl` that can actually perform the three
# ed25519 operations Trailer's update-signing scripts depend on — or fail
# with a diagnosis naming every candidate that was tried and why each was
# rejected.
#
# WHY THIS EXISTS: existence is not capability.
#
# macOS ships **LibreSSL** as /usr/bin/openssl, and LibreSSL's `genpkey`
# rejects `-algorithm ed25519` outright:
#
#     Algorithm ed25519 not found
#     usage: genpkey [-algorithm alg] [cipher] [-genparam] [-out file]
#     ...
#
# `openssl version` answers happily on that same binary, so a guard
# written as "is openssl installed?" passes and the real invocation then
# fails. That is precisely how the 2026-08-04 macOS nightly (run
# 30907188776, macOS job 91985006187) went red on
# tests/test_update_pubkey.cpp and withheld the DMG: the test's guard was
# `openssl version`, which LibreSSL answers, and the very next line ran
# `genpkey -algorithm ed25519` and got the usage dump above.
#
# The probe below runs the OPERATIONS rather than reading a name or a
# version string. That is deliberate: version-sniffing needs a table of
# which fork/version gained which feature, and is wrong the moment a fork
# words its banner differently or backports support. Running the command
# and looking at whether it worked cannot be wrong about this host.
#
# Checks, in the order the shipped scripts actually use them:
#   1. genpkey -algorithm ed25519       key generation (the one that failed)
#   2. pkey -pubout -outform DER        scripts/derive-update-pubkey.sh
#   3. pkeyutl -sign -rawin             scripts/sign-update-feed.sh
# All three, not just the first, so a fork that gains ed25519 keygen but
# still lacks raw signing is rejected here (an honest skip) instead of
# passing this gate and failing later inside a test.
#
# Candidates, in order; the first one that passes all three probes wins:
#   1. $TRAILER_OPENSSL, if set — the escape hatch for MacPorts, Nix, or a
#      hand-built openssl somewhere nothing could be expected to guess.
#   2. `openssl` on PATH — the only candidate on Linux and in CI.
#   3. Homebrew's keg-only openssl@3, located by ASKING brew for its
#      prefix (`brew --prefix openssl@3`) rather than hardcoding
#      /opt/homebrew/opt/openssl@3/bin/openssl. A hardcoded list would be
#      wrong on Intel Macs (/usr/local), wrong under a relocated Homebrew
#      prefix, and would go stale silently; the query is right in all
#      three cases. Note openssl@3 is keg-only, so it is deliberately NOT
#      symlinked into /opt/homebrew/bin and never shadows LibreSSL on
#      PATH — which is why it has to be asked for by name.
#
# Usage:
#   openssl_bin="$(scripts/find-openssl-ed25519.sh)" || exit 1
#   PATH="$(dirname "$openssl_bin"):$PATH"   # the scripts call plain `openssl`
#
# On success: prints the absolute path on stdout, exits 0. Nothing else is
# ever written to stdout, so the command substitution above is safe.
# On failure: prints what was tried to stderr, exits 1.

# Deliberately NOT `set -e`: probing candidates that fail is the entire
# job of this script.
set -uo pipefail

if ! TMPDIR_="$(mktemp -d)"; then
    # Without a scratch dir the probes below would write to "/probe-key.pem",
    # fail on permissions, and blame every candidate for something that is
    # not their fault. Say what actually went wrong instead.
    echo "find-openssl-ed25519.sh: mktemp -d failed; cannot probe." >&2
    exit 1
fi
trap 'rm -rf "$TMPDIR_"' EXIT

NL='
'

# Newline-delimited rather than a bash array: /bin/bash on macOS is 3.2,
# where expanding an empty array under `set -u` is itself an
# unbound-variable error — and "no candidates at all" is a state this
# script must survive to report.
CANDIDATES=""

add_candidate() {
    local resolved
    resolved="$(command -v "$1" 2>/dev/null)" || return 0
    [ -n "$resolved" ] || return 0
    # De-duplicate: $TRAILER_OPENSSL is very often just the PATH openssl,
    # and reporting the same rejection twice reads like two problems.
    case "$NL$CANDIDATES" in
        *"$NL$resolved$NL"*) return 0 ;;
    esac
    CANDIDATES="$CANDIDATES$resolved$NL"
}

if [ -n "${TRAILER_OPENSSL:-}" ]; then
    add_candidate "$TRAILER_OPENSSL"
fi
add_candidate openssl

if command -v brew >/dev/null 2>&1; then
    # openssl@3 is the current formula name; plain `openssl` is its alias
    # and is tried second in case the alias is what a given Homebrew
    # knows. Either way the capability probe is the real gate — a prefix
    # for a formula that is not installed simply has no bin/openssl and
    # is dropped by add_candidate.
    for formula in openssl@3 openssl; do
        prefix="$(brew --prefix "$formula" 2>/dev/null)" || continue
        [ -n "$prefix" ] || continue
        add_candidate "$prefix/bin/openssl"
    done
fi

# Run the three operations the shipped scripts run. Sets REASON on
# failure, describing which one refused, in the caller's words.
REASON=""
probe() {
    local ossl="$1"
    local key="$TMPDIR_/probe-key.pem"
    local msg="$TMPDIR_/probe-msg.bin"
    local der="$TMPDIR_/probe-pub.der"
    local sig="$TMPDIR_/probe.sig"
    rm -f "$key" "$msg" "$der" "$sig"
    printf 'trailer ed25519 capability probe\n' > "$msg"

    if ! "$ossl" genpkey -algorithm ed25519 -out "$key" >/dev/null 2>&1; then
        REASON="genpkey -algorithm ed25519 was rejected (LibreSSL says 'Algorithm ed25519 not found')"
        return 1
    fi
    if [ ! -s "$key" ]; then
        REASON="genpkey -algorithm ed25519 exited 0 but wrote no key"
        return 1
    fi
    if ! "$ossl" pkey -in "$key" -pubout -outform DER -out "$der" >/dev/null 2>&1; then
        REASON="pkey -pubout -outform DER failed (derive-update-pubkey.sh needs it)"
        return 1
    fi
    if ! "$ossl" pkeyutl -sign -inkey "$key" -rawin -in "$msg" -out "$sig" >/dev/null 2>&1; then
        REASON="pkeyutl -sign -rawin failed (sign-update-feed.sh needs it)"
        return 1
    fi
    if [ ! -s "$sig" ]; then
        REASON="pkeyutl -sign -rawin wrote an empty signature"
        return 1
    fi
    return 0
}

TRIED=""
while IFS= read -r candidate; do
    [ -n "$candidate" ] || continue
    REASON=""
    if probe "$candidate"; then
        printf '%s\n' "$candidate"
        exit 0
    fi
    version="$("$candidate" version 2>/dev/null | head -1)"
    TRIED="$TRIED  $candidate [${version:-version unknown}]$NL      $REASON$NL"
done <<EOF
$CANDIDATES
EOF

{
    echo "find-openssl-ed25519.sh: no openssl on this host can do ed25519."
    if [ -n "$TRIED" ]; then
        echo "Tried:"
        printf '%s' "$TRIED"
    else
        echo "  (no openssl executable was found at all)"
    fi
    echo "macOS ships LibreSSL as /usr/bin/openssl, which has no ed25519"
    echo "genpkey. Install a real OpenSSL 3.x:"
    echo "    brew install openssl@3        # keg-only; found via brew --prefix"
    echo "or point TRAILER_OPENSSL at one you already have:"
    echo "    TRAILER_OPENSSL=/path/to/bin/openssl $0"
} >&2
exit 1
