#!/usr/bin/env bash
# Regression tests for scripts/compare-uat-baseline.sh — the nightly UAT
# ratchet.
#
# Why this file exists: the ratchet originally shipped "verified by
# direct execution against a fixture scenario matrix" recorded only in a
# PR body. That matrix was not committed, so nothing stopped the rule
# from drifting — and it did: the pass-RATIO axis it shipped with reds a
# run as REGRESSED whenever a failing test is ADDED, even though no
# existing test regressed. That false positive reached two real
# nightlies (nightly-20260801, nightly-20260802) before anyone noticed,
# because there was no executable statement of what the rule should do.
# The matrix now lives here and runs in ci.yml, next to
# scripts/test-version-gating.sh.
#
# Exits non-zero on the first failed assertion; prints a PASS/FAIL
# summary.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT="$REPO_ROOT/scripts/compare-uat-baseline.sh"
TMPDIR_="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_"' EXIT

PASSES=0
FAILURES=0

# Write a baseline summary in the exact schema nightly.yml's "Build
# release body" step emits (schema_version 1). `null` is spelled the way
# json_num_or_null/json_bool_or_null spell it, so the parser is exercised
# against real shapes rather than a convenient subset.
make_baseline() {
  local file="$1" job_result="$2" passed="$3" total="$4" crashed="$5"
  cat > "$file" <<EOF
{
  "schema_version": 1,
  "tag": "nightly-20260101",
  "generated_at": "2026-01-01T00:00:00Z",
  "lanes": {
    "wine_uat": {
      "job_result": "$job_result",
      "passed": $passed,
      "total": $total,
      "crashed": false
    },
    "macos_uat": {
      "job_result": "$job_result",
      "passed": $passed,
      "total": $total,
      "crashed": $crashed
    }
  }
}
EOF
}

# check <description> <expected-verdict> <expected-suite-delta> \
#       <lane> <prev-file> <curr-result> <curr-passed> <curr-total> <curr-crashed>
# Pass "-" for expected-suite-delta to assert it is empty.
check() {
  local desc="$1" want_verdict="$2" want_delta="$3"
  shift 3
  local out
  out="$(bash "$SCRIPT" "$@")"
  local VERDICT="" REASON_CODE="" PREV_PASSED="" PREV_TOTAL="" PREV_CRASHED="" SUITE_DELTA=""
  eval "$out"
  [ "$want_delta" = "-" ] && want_delta=""
  if [ "$VERDICT" = "$want_verdict" ] && [ "$SUITE_DELTA" = "$want_delta" ]; then
    PASSES=$((PASSES + 1))
    printf 'ok   %s (verdict=%s reason=%s suite_delta=%s)\n' \
      "$desc" "$VERDICT" "$REASON_CODE" "${SUITE_DELTA:-<empty>}"
  else
    FAILURES=$((FAILURES + 1))
    printf 'FAIL %s\n     want verdict=%s suite_delta=%s\n     got  verdict=%s suite_delta=%s (reason=%s)\n' \
      "$desc" "$want_verdict" "${want_delta:-<empty>}" \
      "$VERDICT" "${SUITE_DELTA:-<empty>}" "$REASON_CODE"
  fi
}

BASE="$TMPDIR_/prev.json"

# ---------------------------------------------------------------------
# The regressions this rule change exists to prevent. Both are REAL
# nightlies, not hypotheticals — see the script's header comment.
# ---------------------------------------------------------------------
make_baseline "$BASE" success 40 41 true
check "nightly-20260801 macOS 40/41 -> 40/42: added failing test is NOT a regression" \
  same 1   macos_uat "$BASE" success 40 42 true

make_baseline "$BASE" success 40 42 true
check "nightly-20260802 macOS 40/42 -> 41/43: one more test passing is an improvement" \
  better 1 macos_uat "$BASE" success 41 43 true

# The same shape, exaggerated: a batch of new UATs landing red at once
# (the documented workflow for a confirmed defect) must stay green.
make_baseline "$BASE" success 22 43 false
check "22/43 -> 22/48: five failing guards added, nothing regressed" \
  same 5   wine_uat "$BASE" success 22 48 false

# ---------------------------------------------------------------------
# Real regressions must still be caught.
# ---------------------------------------------------------------------
make_baseline "$BASE" success 23 41 false
check "nightly-20260801 Wine 23/41 -> 20/42: three fewer passing IS a regression" \
  worse 1  wine_uat "$BASE" success 20 42 false

make_baseline "$BASE" success 40 40 false
check "40/40 -> 39/40: a single regression in a static suite" \
  worse 0  wine_uat "$BASE" success 39 40 false

make_baseline "$BASE" success 40 40 false
check "40/40 -> 39/45: regression not masked by suite growth" \
  worse 5  wine_uat "$BASE" success 39 45 false

# ---------------------------------------------------------------------
# Steady state and improvement.
# ---------------------------------------------------------------------
make_baseline "$BASE" success 21 40 false
check "21/40 -> 21/40: a known, already-backlogged bad count stays green" \
  same 0   wine_uat "$BASE" success 21 40 false

make_baseline "$BASE" success 21 40 false
check "21/40 -> 40/40: suite fixed" \
  better 0 wine_uat "$BASE" success 40 40 false

# A test being DELETED does not invent a regression when it was failing,
# but does flag one when it was passing — deletion is rare and deserves
# a human look, so the conservative reading is deliberate.
make_baseline "$BASE" success 21 40 false
check "21/40 -> 21/38: two FAILING tests deleted reads as unchanged" \
  same -2  wine_uat "$BASE" success 21 38 false

make_baseline "$BASE" success 40 40 false
check "40/40 -> 38/38: two PASSING tests deleted is flagged for a human" \
  worse -2 wine_uat "$BASE" success 38 38 false

# ---------------------------------------------------------------------
# Crash state dominates counts, in both directions.
# ---------------------------------------------------------------------
make_baseline "$BASE" success 20 40 false
check "clean -> crashed outranks a better count" \
  worse -   macos_uat "$BASE" success 39 40 true

make_baseline "$BASE" success 39 40 true
check "crashed -> clean outranks a worse count" \
  better -  macos_uat "$BASE" success 20 40 false

# ---------------------------------------------------------------------
# Degrade gracefully — none of these may ever read as `worse`.
# ---------------------------------------------------------------------
check "no baseline file at all (first ratcheted run)" \
  no_baseline - wine_uat "$TMPDIR_/does-not-exist.json" success 20 40 false

check "empty baseline path" \
  no_baseline - wine_uat "" success 20 40 false

printf 'not json at all\n' > "$TMPDIR_/corrupt.json"
check "unparseable baseline" \
  no_baseline - wine_uat "$TMPDIR_/corrupt.json" success 20 40 false

make_baseline "$BASE" failure null null false
check "previous night's lane was not green" \
  no_baseline - wine_uat "$BASE" success 20 40 false

make_baseline "$BASE" success 20 40 false
check "this run's lane did not go green" \
  not_applicable - wine_uat "$BASE" failure "" "" false

make_baseline "$BASE" success 20 40 false
check "this run's counts unavailable" \
  unknown - wine_uat "$BASE" success "" "" false

make_baseline "$BASE" success null null false
check "baseline carries null counts" \
  unknown - wine_uat "$BASE" success 20 40 false

echo
echo "compare-uat-baseline: $PASSES passed, $FAILURES failed"
[ "$FAILURES" -eq 0 ] || exit 1
