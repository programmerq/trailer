#!/usr/bin/env bash
# Compare one non-gating nightly UAT lane's CURRENT run against the
# PREVIOUS nightly's published uat-summary.json baseline, and print a
# VERDICT (plus a short REASON_CODE and the previous counts) for the
# caller to `eval`. This is the ratchet: nightly.yml's publish job calls
# this once per non-gating lane (wine_uat, macos_uat) and reds the RUN
# (never the lane, never the artifact) only when a lane's VERDICT is
# `worse` -- see nightly.yml's "Compare UAT results against previous
# nightly" and "Fail the run if UAT regressed vs the previous nightly"
# steps.
#
# Baseline source: a small `uat-summary.json` asset attached to each real
# nightly release (written by nightly-publish's "Build release body" step,
# published alongside the binaries by "Create tag + GitHub Release"). This
# was chosen over a committed docs/uat-baselines.json (would need the
# nightly job to push a commit to main -- more privilege, more risk, and
# pollutes history with infra data unrelated to source) and over a GitHub
# Actions cache (best-effort, evictable, not human-inspectable, not
# versioned with what actually shipped). A release asset is atomic with
# the artifacts it describes, trivially fetched by tag (`gh release
# download <tag> -p uat-summary.json`), and keeps the release itself the
# human-readable record (AGENTS.md G2 spirit: what shipped is what's
# visible).
#
# WHAT COUNTS AS "WORSE" (the axis this script implements):
#   1. Crash state is checked FIRST and dominates: a lane that crashes
#      this run when the previous run had no crash is ALWAYS worse,
#      regardless of pass counts (a crash is categorically more serious
#      than an ordinary assertion failure -- see nightly-macos's "UAT
#      suite" step). Symmetrically, a crash clearing is always better.
#   2. When crash state is UNCHANGED (both crashed or both clean), the
#      ABSOLUTE PASSED COUNT is compared: curr.passed vs prev.passed.
#      Fewer tests passing than last night is `worse`; more is `better`;
#      equal is `same`. The suite's TOTAL is reported separately (see
#      SUITE_DELTA below) and never affects the verdict.
#
#      This replaced a pass-RATIO comparison (integer cross-
#      multiplication of curr.passed*prev.total vs prev.passed*
#      curr.total) on 2026-08-02. The ratio axis was chosen to stop a
#      grown suite from reading as an improvement, but it has the
#      opposite and much more damaging failure mode: ADDING a test that
#      fails, while every existing test still passes, LOWERS the ratio
#      and reds the run as REGRESSED even though nothing regressed.
#      Observed twice on real nightlies:
#
#        nightly-20260801  macOS  40/41 -> 40/42  ratio says REGRESSED,
#                          passed count FLAT -- a pure false positive.
#        nightly-20260802  macOS  40/42 -> 41/43  ratio says IMPROVED,
#                          but only because passed happened to rise too;
#                          the cross-products differ by 2 (1722 vs 1720).
#
#      That false positive is not an edge case here -- it is this
#      repo's normal growth pattern. AGENTS.md ("CI cadence") requires
#      every confirmed defect to land as a regression guard in the `uat`
#      suite, and such a guard is written RED, before the fix. Under the
#      ratio rule, following the documented process reds the nightly.
#
#      Known residual blind spot, accepted for now: because only counts
#      (not per-test identity) are carried in the baseline, a test that
#      regressed can be masked by a different, newly-added test that
#      passes on the same night (40/41 -> 41/43 could be +2 new passes
#      and 1 regression). Closing that needs the summary to carry the
#      per-test name->status map and a set comparison; it is a schema
#      change, not a tweak to this rule. Tracked in
#      docs/backlog/2026-08-02-uat-ratchet-per-test-identity.md.
#
# DEGRADE GRACEFULLY (never a false regression): no previous summary file,
# a file that fails to parse, a file missing this lane's key, or a
# previous night where this lane itself didn't reach `success` -- all four
# collapse to a single `no_baseline` verdict, which is NOT `worse`. This
# is what makes the very first ratcheted run (no asset exists yet on
# yesterday's pre-ratchet release) print green rather than red, with no
# separate "first run" special case needed.
#
# RATCHETS FORWARD automatically: nightly-publish writes THIS run's own
# counts into dist/uat-summary.json regardless of the verdict, and that
# file becomes tomorrow's baseline. An improved night raises the floor;
# see the PR body for why a *sliding* previous-night comparison (not a
# historical high-water mark) was chosen.
#
# Usage:
#   compare-uat-baseline.sh <lane-key> <prev-summary-file-or-empty> \
#       <curr-job-result> <curr-passed> <curr-total> <curr-crashed>
#
#   <lane-key>               JSON key inside the summary's "lanes" object
#                             (e.g. "wine_uat", "macos_uat").
#   <prev-summary-file>      Path to the previous nightly's downloaded
#                             uat-summary.json, or "" / a nonexistent path
#                             when there is none.
#   <curr-job-result>        This run's lane job `result` (e.g. "success").
#                             Anything other than "success" short-circuits
#                             to `not_applicable` -- the existing "any lane
#                             result != success" check in nightly-publish
#                             already reds the run for that case; this
#                             script has nothing to add.
#   <curr-passed/total>      This run's parsed UAT counts, or "" if
#                             unavailable.
#   <curr-crashed>           "true"/"false" (defaults to "false" if empty).
#
# Prints, for `eval` by the caller:
#   VERDICT=better|same|worse|unknown|no_baseline|not_applicable
#   REASON_CODE=<short slug, see the case statements below>
#   PREV_PASSED=<n-or-empty>
#   PREV_TOTAL=<n-or-empty>
#   PREV_CRASHED=true|false|<empty>
#   SUITE_DELTA=<signed-int-or-empty>   curr.total - prev.total. Purely
#                             informational -- the caller renders it as a
#                             note next to the verdict ("+1 test added")
#                             so a changed suite size is visible rather
#                             than silently folded into the counts. It is
#                             NOT part of the verdict and never gates.
set -uo pipefail

LANE="${1:-}"
PREV_FILE="${2:-}"
CURR_RESULT="${3:-}"
CURR_PASSED="${4:-}"
CURR_TOTAL="${5:-}"
CURR_CRASHED="${6:-false}"
[ -z "$CURR_CRASHED" ] && CURR_CRASHED=false

# Extract one flat scalar field from inside the named lane's `{ ... }`
# block. The schema this reads is only ever produced by nightly.yml's own
# "Build release body" step -- not user input, not third-party JSON -- so
# a small range+grep extractor is fine here rather than adding a jq
# dependency to every runner label this script executes on (trailer-small,
# the nightly-publish job's runner, has no confirmed standalone `jq`
# install -- see the PR body).
json_field() {
  local file="$1" lane="$2" field="$3"
  [ -f "$file" ] || return 1
  sed -n "/\"${lane}\"[[:space:]]*:[[:space:]]*{/,/}/p" "$file" \
    | grep -oE "\"${field}\"[[:space:]]*:[[:space:]]*(\"[^\"]*\"|null|true|false|[0-9]+)" \
    | head -1 \
    | sed -E 's/^"[^"]+"[[:space:]]*:[[:space:]]*//' \
    | tr -d '"'
}

# Set by the count-comparison branch below when both totals are known;
# stays empty for every early-exit path (not applicable / no baseline /
# counts unavailable), where "how much did the suite grow" has no answer.
SUITE_DELTA=""

emit() {
  echo "VERDICT=$1"
  echo "REASON_CODE=$2"
  echo "PREV_PASSED=$3"
  echo "PREV_TOTAL=$4"
  echo "PREV_CRASHED=$5"
  echo "SUITE_DELTA=$SUITE_DELTA"
}

if [ "$CURR_RESULT" != "success" ]; then
  # The job itself didn't go green -- nothing for the ratchet to compare;
  # the pre-existing "any lane result != success" check already reds the
  # run for this case.
  emit "not_applicable" "lane-not-successful" "" "" ""
  exit 0
fi

PREV_JOB_RESULT=""
PREV_PASSED=""
PREV_TOTAL=""
PREV_CRASHED=""
if [ -n "$PREV_FILE" ] && [ -f "$PREV_FILE" ]; then
  PREV_JOB_RESULT="$(json_field "$PREV_FILE" "$LANE" "job_result" || true)"
  PREV_PASSED="$(json_field "$PREV_FILE" "$LANE" "passed" || true)"
  PREV_TOTAL="$(json_field "$PREV_FILE" "$LANE" "total" || true)"
  PREV_CRASHED="$(json_field "$PREV_FILE" "$LANE" "crashed" || true)"
  [ "$PREV_PASSED" = "null" ] && PREV_PASSED=""
  [ "$PREV_TOTAL" = "null" ] && PREV_TOTAL=""
  [ "$PREV_CRASHED" = "null" ] && PREV_CRASHED=""
fi

if [ "$PREV_JOB_RESULT" != "success" ]; then
  # Covers every "no usable baseline" case in one branch: no file at all
  # (first-ever ratcheted run against a pre-ratchet release), a file that
  # doesn't parse or doesn't contain this lane (corrupt/foreign asset),
  # and a previous night where this lane itself wasn't green (nothing to
  # ratchet against). All degrade to "not a regression".
  emit "no_baseline" "no-baseline" "$PREV_PASSED" "$PREV_TOTAL" "$PREV_CRASHED"
  exit 0
fi

[ -z "$PREV_CRASHED" ] && PREV_CRASHED=false

# Crash state dominates and is checked before counts.
if [ "$CURR_CRASHED" = "true" ] && [ "$PREV_CRASHED" != "true" ]; then
  emit "worse" "crash-appeared" "$PREV_PASSED" "$PREV_TOTAL" "$PREV_CRASHED"
  exit 0
fi
if [ "$PREV_CRASHED" = "true" ] && [ "$CURR_CRASHED" != "true" ]; then
  emit "better" "crash-cleared" "$PREV_PASSED" "$PREV_TOTAL" "$PREV_CRASHED"
  exit 0
fi

# Crash state unchanged (both crashed, or both clean) -- compare the
# ABSOLUTE passed count. See the "WHAT COUNTS AS WORSE" header above for
# why this is not a ratio.
if [ -z "$CURR_PASSED" ] || [ -z "$CURR_TOTAL" ] || [ -z "$PREV_PASSED" ] || [ -z "$PREV_TOTAL" ]; then
  emit "unknown" "count-unavailable" "$PREV_PASSED" "$PREV_TOTAL" "$PREV_CRASHED"
  exit 0
fi

# Informational only -- deliberately computed BEFORE the verdict so it is
# reported on every comparable run, including the ones where the suite
# size is exactly what explains a flat pass count.
SUITE_DELTA=$((CURR_TOTAL - PREV_TOTAL))

if [ "$CURR_PASSED" -lt "$PREV_PASSED" ]; then
  emit "worse" "passed-fewer" "$PREV_PASSED" "$PREV_TOTAL" "$PREV_CRASHED"
elif [ "$CURR_PASSED" -gt "$PREV_PASSED" ]; then
  emit "better" "passed-more" "$PREV_PASSED" "$PREV_TOTAL" "$PREV_CRASHED"
else
  emit "same" "passed-same" "$PREV_PASSED" "$PREV_TOTAL" "$PREV_CRASHED"
fi
