#!/usr/bin/env bash
# Parse a ctest summary line out of a captured `ctest -L uat` log and print
# PASSED=<n> / TOTAL=<n> assignments (empty when undeterminable) for the
# caller to `eval`.
#
# Shared by nightly.yml's nightly-windows ("UAT suite (Wine)") and
# nightly-macos ("UAT suite") steps, which both run UAT non-gating and
# surface a real pass/total count in the nightly release body. Factored out
# 2026-07-26 after a bug was found (and fixed here) while writing the macOS
# side: ctest OMITS the "F tests failed" clause entirely when F=0 -- one
# format is "N% tests passed, F tests failed out of T" (F>0), the other is
# "N% tests passed out of T" (F=0, clause absent -- confirmed by this
# project's own CI logs: the macOS bootstrap run's unit-test step logged
# "100% tests passed out of 62" with no "0 tests failed" clause, while a
# local Linux ctest run printed the fuller "100% tests passed, 0 tests
# failed out of 62" for the identical 0-failures case). Grepping for
# "[0-9]+ tests failed" alone -- the pre-2026-07-26 approach both steps used
# -- silently misreports a full pass as "count unavailable"; for Wine that
# bug was latent (Wine UAT has never reached 100%, see docs/backlog/
# 2026-07-24-wine-uat-failures-triage.md, whose whole point is to get there)
# but would have started misreporting the exact day that item closes -- a
# silent failure mode that only triggers on success, the worst kind. Only
# default the failed-count to 0 when the leading percentage actually reads
# 100 (not merely "clause absent") -- a garbled/truncated non-100% line
# falls through to "unknown" rather than reporting a false full pass.
#
# Usage: parse-ctest-uat-summary.sh <ctest-log-file>
# Prints two lines to stdout: PASSED=<n-or-empty> and TOTAL=<n-or-empty>.
set -uo pipefail

LOG_FILE="$1"

SUMMARY_LINE=$(grep -E '[0-9]+% tests passed.*out of [0-9]+' "$LOG_FILE" | tail -1)
PERCENT=$(echo "$SUMMARY_LINE" | grep -oE '[0-9]+% tests passed' | grep -oE '^[0-9]+')
TOTAL=$(echo "$SUMMARY_LINE" | grep -oE 'out of [0-9]+' | grep -oE '[0-9]+')
FAILED=$(echo "$SUMMARY_LINE" | grep -oE '[0-9]+ tests failed' | grep -oE '^[0-9]+')

if [ -z "$FAILED" ] && [ "$PERCENT" = "100" ]; then
  FAILED=0
fi

if [ -n "$TOTAL" ] && [ -n "$FAILED" ]; then
  PASSED=$((TOTAL - FAILED))
else
  PASSED=""
  TOTAL=""
fi

echo "PASSED=$PASSED"
echo "TOTAL=$TOTAL"
