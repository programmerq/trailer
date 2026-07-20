#!/usr/bin/env bash
# Regression tests for the VERSION dev-suffix gating.
#
# Two things are asserted here, both of which keep a -dev / -dev.N /
# -rc build from accidentally being treated as release-ready:
#
#   1. The release-ready regex used by release.yml's precheck and
#      release-autotag.yml. This script keeps a LITERAL COPY of that
#      regex and asserts its classification of representative versions,
#      including git-derived dev strings (X.Y.Z-dev+<count>.g<sha>[.dirty]).
#      If you change the regex in the workflows, change it here too.
#   2. bump-version.sh's parse/post-release/release behaviour.
#
# Exits non-zero on the first failed assertion group; prints a
# PASS/FAIL summary.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUMP="$REPO_ROOT/scripts/bump-version.sh"

# Keep this in lockstep with .github/workflows/release.yml and
# release-autotag.yml (`grep -qE -- '<this>'`).
GATE_REGEX='-dev(\.[0-9]+)?(\+[0-9A-Za-z.-]+)?$|-rc[0-9]*$'

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
assert_gate "0.3.1-dev"   match
# Legacy -dev.N counter still gated (back-compat).
assert_gate "0.3.1-dev.0" match
assert_gate "0.3.1-dev.1" match
# Git-derived dev strings must also be gated if they ever reach the guard.
assert_gate "0.3.1-dev+142.gabc1234"       match
assert_gate "0.3.1-dev+142.gabc1234.dirty" match
assert_gate "0.3.1-rc1"   match
# Clean releases ARE release-ready (must NOT match).
assert_gate "0.3.0" nomatch
assert_gate "0.3.1" nomatch

echo
echo "== bump-version.sh post-release / release =="

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

# assert_parse_reject VERSION -> parse_version must refuse the string.
# `set <value>` runs it through parse_version on write (and CURRENT on
# read), so a malformed suffix has to exit non-zero.
assert_parse_reject() {
    local version="$1"
    printf '%s\n' "$version" > "$SANDBOX/VERSION"
    if "$SANDBOX/scripts/bump-version.sh" set "$version" >/dev/null 2>&1; then
        echo "FAIL: parse accepts malformed '$version'"
        FAILURES=$((FAILURES + 1))
    else
        echo "PASS: parse rejects '$version'"
    fi
}

# parse succeeds on the bare -dev base (any subcommand that only reads is
# fine; use the no-op-ish 'set' to the same value, which validates on
# write).
printf '0.3.1-dev\n' > "$SANDBOX/VERSION"
if "$SANDBOX/scripts/bump-version.sh" set 0.3.1-dev >/dev/null 2>&1; then
    echo "PASS: parse accepts '0.3.1-dev'"
else
    echo "FAIL: parse rejects '0.3.1-dev'"
    FAILURES=$((FAILURES + 1))
fi

# post-release bumps the patch and appends the bare -dev base (no manual
# counter — the full dev string is git-derived at build time).
assert_eq "post-release 0.3.1"        "$(run_bump 0.3.1 post-release)"   "0.3.2-dev"
# release strips the bare -dev suffix.
assert_eq "release 0.3.1-dev"         "$(run_bump 0.3.1-dev release)"    "0.3.1"
# release also strips a legacy -dev.N counter (back-compat).
assert_eq "release 0.3.1-dev.3"       "$(run_bump 0.3.1-dev.3 release)"  "0.3.1"
# dev-bump is retired: it is now a guiding no-op that exits 0 and leaves
# VERSION untouched (git derives the full dev string at build time).
assert_eq "dev-bump is a no-op"       "$(run_bump 0.3.1-dev dev-bump)"   "0.3.1-dev"

echo
echo "== parse_version accepts a git-derived +metadata tail (no choke) =="
# The VERSION file never carries +metadata, but parse must not choke if a
# git-derived full string is fed to it.
if "$SANDBOX/scripts/bump-version.sh" set "0.3.1-dev+142.gabc1234.dirty" >/dev/null 2>&1; then
    echo "PASS: parse tolerates '0.3.1-dev+142.gabc1234.dirty'"
else
    echo "FAIL: parse chokes on '0.3.1-dev+142.gabc1234.dirty'"
    FAILURES=$((FAILURES + 1))
fi

echo
echo "== parse_version rejects malformed suffixes =="
assert_parse_reject "0.3.1-dev."
assert_parse_reject "0.3.1-devx"
assert_parse_reject "0.3.1-dev.0.1"

echo
if [[ "$FAILURES" -eq 0 ]]; then
    echo "ALL PASS"
    exit 0
else
    echo "$FAILURES FAILURE(S)"
    exit 1
fi
