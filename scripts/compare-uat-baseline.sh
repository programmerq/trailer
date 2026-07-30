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
#   2. When crash state is UNCHANGED (both crashed or both clean), pass
#      counts are compared as a RATIO, not a raw passed-count delta, via
#      integer cross-multiplication (no float epsilon): curr.passed *
#      prev.total  vs  prev.passed * curr.total. Raw-count comparison
#      misbehaves when the suite grows -- 21/40 -> 22/45 is MORE tests
#      passing but a LOWER ratio (0.525 -> 0.489) -- so ratio is the
#      axis, and that specific case is correctly NOT treated as an
#      improvement (see the scenario matrix in the PR body).
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

emit() {
  echo "VERDICT=$1"
  echo "REASON_CODE=$2"
  echo "PREV_PASSED=$3"
  echo "PREV_TOTAL=$4"
  echo "PREV_CRASHED=$5"
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

# Crash state unchanged (both crashed, or both clean) -- compare pass
# RATIO via integer cross-multiplication.
if [ -z "$CURR_PASSED" ] || [ -z "$CURR_TOTAL" ] || [ -z "$PREV_PASSED" ] || [ -z "$PREV_TOTAL" ]; then
  emit "unknown" "count-unavailable" "$PREV_PASSED" "$PREV_TOTAL" "$PREV_CRASHED"
  exit 0
fi

CURR_SIDE=$((CURR_PASSED * PREV_TOTAL))
PREV_SIDE=$((PREV_PASSED * CURR_TOTAL))

if [ "$CURR_SIDE" -lt "$PREV_SIDE" ]; then
  emit "worse" "ratio-worse" "$PREV_PASSED" "$PREV_TOTAL" "$PREV_CRASHED"
elif [ "$CURR_SIDE" -gt "$PREV_SIDE" ]; then
  emit "better" "ratio-better" "$PREV_PASSED" "$PREV_TOTAL" "$PREV_CRASHED"
else
  emit "same" "ratio-same" "$PREV_PASSED" "$PREV_TOTAL" "$PREV_CRASHED"
fi
