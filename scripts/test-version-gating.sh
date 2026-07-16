#!/usr/bin/env bash
# Regression tests for the VERSION dev-suffix gating.
#
# Two things are asserted here, both of which keep a -dev / -dev.N /
# -rc build from accidentally being treated as release-ready:
#
#   1. The release-ready regex used by release.yml's precheck and
#      release-autotag.yml. This script keeps a LITERAL COPY of that
#      regex and asserts its classification of representative versions.
#      If you change the regex in the workflows, change it here too.
#   2. bump-version.sh's parse/dev-bump/release behaviour on -dev.N.
#
# Exits non-zero on the first failed assertion group; prints a
# PASS/FAIL summary.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUMP="$REPO_ROOT/scripts/bump-version.sh"

# Keep this in lockstep with .github/workflows/release.yml and
# release-autotag.yml (`grep -qE -- '<this>'`).
GATE_REGEX='-dev(\.[0-9]+)?$|-rc[0-9]*$'

FAILURES=0

# assert_gate VERSION EXPECT   (EXPECT = match | nomatch)
assert_gate() {
    local version="$1" expect="$2" got
    if echo "$version" | grep -qE -- "$GATE_REGEX"; then
        got=match
    else
        got=nomatch
    fi
    if [[ "$got" == "$expect" ]]; then
        echo "PASS: gate '$version' -> $got"
    else
        echo "FAIL: gate '$version' -> $got (expected $expect)"
        FAILURES=$((FAILURES + 1))
    fi
}

# assert_eq LABEL ACTUAL EXPECTED
assert_eq() {
    local label="$1" actual="$2" expected="$3"
    if [[ "$actual" == "$expected" ]]; then
        echo "PASS: $label -> '$actual'"
    else
        echo "FAIL: $label -> '$actual' (expected '$expected')"
        FAILURES=$((FAILURES + 1))
    fi
}

echo "== release-ready gate regex =="
# -dev variants and rc are NOT release-ready (must match).
assert_gate "0.3.1-dev.0" match
assert_gate "0.3.1-dev.1" match
assert_gate "0.3.1-dev"   match
assert_gate "0.3.1-rc1"   match
# Clean releases ARE release-ready (must NOT match).
assert_gate "0.3.0" nomatch
assert_gate "0.3.1" nomatch

echo
echo "== bump-version.sh on -dev.N =="

# Run bump-version.sh against a throwaway VERSION file by pointing the
# script at a temp repo root layout (it cd's to its own ../, so we
# invoke a copy from a sandbox dir that has its own VERSION).
SANDBOX="$(mktemp -d)"
trap 'rm -rf "$SANDBOX"' EXIT
mkdir -p "$SANDBOX/scripts"
cp "$BUMP" "$SANDBOX/scripts/bump-version.sh"

run_bump() { # run_bump START ACTION -> echoes resulting VERSION
    local start="$1" action="$2"
    printf '%s\n' "$start" > "$SANDBOX/VERSION"
    if "$SANDBOX/scripts/bump-version.sh" "$action" >/dev/null 2>&1; then
        tr -d '[:space:]' < "$SANDBOX/VERSION"
    else
        echo "<error>"
    fi
}

# parse succeeds on -dev.N (any subcommand that only reads is fine; use
# the no-op-ish 'set' to the same value, which validates on write).
printf '0.3.1-dev.0\n' > "$SANDBOX/VERSION"
if "$SANDBOX/scripts/bump-version.sh" set 0.3.1-dev.0 >/dev/null 2>&1; then
    echo "PASS: parse accepts '0.3.1-dev.0'"
else
    echo "FAIL: parse rejects '0.3.1-dev.0'"
    FAILURES=$((FAILURES + 1))
fi

assert_eq "dev-bump 0.3.1-dev.0" "$(run_bump 0.3.1-dev.0 dev-bump)" "0.3.1-dev.1"
assert_eq "release 0.3.1-dev.0"  "$(run_bump 0.3.1-dev.0 release)"  "0.3.1"

echo
if [[ "$FAILURES" -eq 0 ]]; then
    echo "ALL PASS"
    exit 0
else
    echo "$FAILURES FAILURE(S)"
    exit 1
fi
