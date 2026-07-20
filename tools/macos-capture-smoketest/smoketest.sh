#!/usr/bin/env bash
#
# smoketest.sh — local macOS smoke test for Trailer's ScreenCaptureKit
# picker capture backend.
#
# Purpose: offset the manual GPSC.2 checklist in PR #72. It automates the
# reset + assertion scaffolding around a single picker capture:
#
#   [SETUP]      tccutil reset ScreenCapture <bundle-id>
#   [BASELINE]   confirm no ScreenCapture grant exists for <bundle-id>
#   [DRIVE]      trigger one picker capture (launch app / replay clicks / prompt)
#   [ASSERT]     confirm NO Screen-Recording grant was created for <bundle-id>
#   [DIAGNOSTIC] CGPreflightScreenCaptureAccess() for the harness process
#
# The authoritative per-app check is the system TCC.db query in [ASSERT];
# reading that database requires Full Disk Access (see README.md). Without
# FDA the harness degrades to the preflight diagnostic + your own visual
# confirmation that no "Screen Recording" prompt appeared.
#
# macOS-only. It cannot run on Linux/CI. shellcheck-clean.

set -euo pipefail

# --------------------------------------------------------------------------
# Constants
# --------------------------------------------------------------------------

# Trailer's CFBundleIdentifier (CMakeLists.txt: MACOSX_BUNDLE_GUI_IDENTIFIER).
readonly DEFAULT_BUNDLE_ID="io.github.programmerq.trailer"

readonly TCC_SERVICE="kTCCServiceScreenCapture"
readonly SYSTEM_TCC_DB="/Library/Application Support/com.apple.TCC/TCC.db"
readonly USER_TCC_DB="${HOME}/Library/Application Support/com.apple.TCC/TCC.db"

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly SCRIPT_DIR
readonly PREFLIGHT_SWIFT="${SCRIPT_DIR}/preflight.swift"

# --------------------------------------------------------------------------
# State
# --------------------------------------------------------------------------

BUNDLE_ID="${DEFAULT_BUNDLE_ID}"
APP_PATH=""
CLICKS_FILE=""
MODE="window"     # informational only: window | display

OVERALL="PASS"    # PASS | FAIL | INCONCLUSIVE
FDA_AVAILABLE=1   # set to 0 when the system TCC.db can't be read

# --------------------------------------------------------------------------
# Output helpers
# --------------------------------------------------------------------------

pass() { printf '%s PASS: %s\n' "$1" "$2"; }
fail() { printf '%s FAIL: %s\n' "$1" "$2"; OVERALL="FAIL"; }
skip() { printf '%s SKIP: %s\n' "$1" "$2"; }
info() { printf '%s %s\n'      "$1" "$2"; }

# Downgrade PASS -> INCONCLUSIVE (never overrides an existing FAIL).
mark_inconclusive() {
  if [[ "${OVERALL}" == "PASS" ]]; then
    OVERALL="INCONCLUSIVE"
  fi
}

usage() {
  cat <<EOF
Usage: ${0##*/} [options]

Local macOS smoke test for Trailer's ScreenCaptureKit picker backend.
Automates tccutil reset + TCC-grant assertions around one picker capture,
to offset the manual GPSC.2 checklist in PR #72.

Options:
  --bundle-id ID     App bundle id to check (default: ${DEFAULT_BUNDLE_ID}).
  --app PATH         Path to Trailer.app (or its binary) to launch for [DRIVE].
  --clicks FILE      cliclick command file to replay for [DRIVE] (needs cliclick).
  --mode window|display
                     Informational: which picker target you intend to choose.
  -h, --help         Show this help and exit.

Requirements:
  * macOS (Darwin) — this script exits on any other OS.
  * sqlite3 (ships with macOS).
  * Full Disk Access for Terminal (or whatever runs this) to read the system
    TCC.db — the authoritative per-app grant check. Without it, the TCC checks
    are skipped and the run relies on the preflight diagnostic + your own eyes.
  * Optional: cliclick (to replay --clicks), swift (for the preflight helper).

Example:
  ${0##*/} --app /Applications/Trailer.app --mode window
EOF
}

# --------------------------------------------------------------------------
# Argument parsing
# --------------------------------------------------------------------------

while [[ $# -gt 0 ]]; do
  case "$1" in
    --bundle-id)
      [[ $# -ge 2 ]] || { echo "error: --bundle-id needs a value" >&2; exit 2; }
      BUNDLE_ID="$2"; shift 2 ;;
    --app)
      [[ $# -ge 2 ]] || { echo "error: --app needs a value" >&2; exit 2; }
      APP_PATH="$2"; shift 2 ;;
    --clicks)
      [[ $# -ge 2 ]] || { echo "error: --clicks needs a value" >&2; exit 2; }
      CLICKS_FILE="$2"; shift 2 ;;
    --mode)
      [[ $# -ge 2 ]] || { echo "error: --mode needs a value" >&2; exit 2; }
      case "$2" in
        window|display) MODE="$2" ;;
        *) echo "error: --mode must be 'window' or 'display'" >&2; exit 2 ;;
      esac
      shift 2 ;;
    -h|--help)
      usage; exit 0 ;;
    *)
      echo "error: unknown option '$1'" >&2
      usage >&2
      exit 2 ;;
  esac
done

# --------------------------------------------------------------------------
# Preconditions
# --------------------------------------------------------------------------

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "error: this smoke test is macOS-only (uname is '$(uname -s)', not Darwin)." >&2
  echo "       Run it on the owner's Mac; it cannot execute on Linux/CI." >&2
  exit 3
fi

if ! command -v sqlite3 >/dev/null 2>&1; then
  echo "error: sqlite3 not found — it ships with macOS; cannot query TCC.db." >&2
  exit 3
fi

HAVE_CLICLICK=0
command -v cliclick >/dev/null 2>&1 && HAVE_CLICLICK=1
HAVE_SWIFT=0
command -v swift >/dev/null 2>&1 && HAVE_SWIFT=1

# --------------------------------------------------------------------------
# TCC.db query helpers
# --------------------------------------------------------------------------

# Determine the authorization column name for the `access` table on this macOS.
# Modern schemas use `auth_value` (0=denied, 2=allowed, plus limited/unknown
# values); older ones used `allowed` (0/1). Echoes the column name, or empty
# if the db can't be read (caller treats that as "FDA unavailable").
tcc_auth_column() {
  local db="$1" cols
  cols="$(sqlite3 "${db}" 'PRAGMA table_info(access);' 2>/dev/null)" || return 1
  if grep -q '|auth_value|' <<<"${cols}"; then
    echo "auth_value"
  elif grep -q '|allowed|' <<<"${cols}"; then
    echo "allowed"
  else
    # Unknown schema — default to the modern column and let the query fail soft.
    echo "auth_value"
  fi
}

# Echo the highest authorization value found for TCC_SERVICE + bundle id in one
# db (empty string = no matching row). Returns non-zero if the db can't be read
# (typically missing Full Disk Access).
tcc_query_db() {
  local db="$1" col result
  [[ -f "${db}" ]] || { echo ""; return 0; }
  col="$(tcc_auth_column "${db}")" || return 1
  # MAX() collapses multiple rows to the most-permissive value; NULL -> ''.
  result="$(sqlite3 "${db}" \
    "SELECT IFNULL(MAX(${col}),'') FROM access \
     WHERE service='${TCC_SERVICE}' AND client='${BUNDLE_ID}';" 2>/dev/null)" \
    || return 1
  echo "${result}"
}

# Interpret an auth value string as "granted" (0) or "not granted" (1).
# Modern: 2 (and 3=limited) count as an authorized entry; 0/1 do not.
# Legacy `allowed`: 1 = granted, 0 = not. Empty = no row = not granted.
tcc_value_is_granted() {
  case "$1" in
    2|3) return 0 ;;   # allowed / limited on modern schema
    1)   return 0 ;;   # allowed on legacy `allowed` column
    *)   return 1 ;;   # 0, empty, or anything else -> not an authorized grant
  esac
}

# Query both databases. Sets globals:
#   TCC_READABLE  — 1 if at least one db was readable, 0 if all reads failed
#   TCC_GRANTED   — 1 if any readable db shows an authorized grant, else 0
#   TCC_DETAIL    — human-readable per-db summary
tcc_scan() {
  local sys_val user_val sys_ok=0 user_ok=0 detail=""
  TCC_READABLE=0
  TCC_GRANTED=0

  if sys_val="$(tcc_query_db "${SYSTEM_TCC_DB}")"; then
    sys_ok=1; TCC_READABLE=1
    detail+="system db: '${sys_val:-<no row>}'"
    tcc_value_is_granted "${sys_val}" && TCC_GRANTED=1
  else
    detail+="system db: UNREADABLE"
  fi

  if user_val="$(tcc_query_db "${USER_TCC_DB}")"; then
    user_ok=1; TCC_READABLE=1
    detail+="; user db: '${user_val:-<no row>}'"
    tcc_value_is_granted "${user_val}" && TCC_GRANTED=1
  else
    detail+="; user db: UNREADABLE"
  fi

  TCC_DETAIL="${detail}"
  # Silence "assigned but only used to build detail" style concerns.
  : "${sys_ok}" "${user_ok}"
}

print_fda_instructions() {
  cat <<EOF

  >> Full Disk Access required to read the system TCC.db.
     Grant it, then re-run for the authoritative per-app assertion:
       1. System Settings ▸ Privacy & Security ▸ Full Disk Access
       2. Enable your terminal (Terminal.app / iTerm) — or whatever app runs
          this script. Toggle it off/on if it was already listed.
       3. Fully quit and reopen that terminal, then re-run this script.
     Until then, TCC-based checks are SKIPPED and the run relies on the
     preflight diagnostic + your visual confirmation that no prompt appeared.
EOF
}

# --------------------------------------------------------------------------
# Header
# --------------------------------------------------------------------------

echo "=============================================================="
echo " Trailer macOS capture-permission smoke test (GPSC.2 offset)"
echo "=============================================================="
echo " bundle id : ${BUNDLE_ID}"
echo " mode      : ${MODE} (informational)"
echo " app       : ${APP_PATH:-<none — manual drive>}"
echo " clicks    : ${CLICKS_FILE:-<none>}"
echo " cliclick  : $([[ ${HAVE_CLICLICK} -eq 1 ]] && echo present || echo absent)"
echo " swift     : $([[ ${HAVE_SWIFT} -eq 1 ]] && echo present || echo absent)"
echo "--------------------------------------------------------------"

# --------------------------------------------------------------------------
# STEP 1 — [SETUP] tccutil reset
# --------------------------------------------------------------------------

echo
echo "[SETUP] resetting ScreenCapture TCC state for ${BUNDLE_ID}"
if tccutil reset ScreenCapture "${BUNDLE_ID}"; then
  pass "[SETUP]" "tccutil reset ScreenCapture ${BUNDLE_ID} (exit 0)"
else
  fail "[SETUP]" "tccutil reset failed — cannot guarantee a clean baseline"
fi

# --------------------------------------------------------------------------
# STEP 2 — [BASELINE] no grant before the capture
# --------------------------------------------------------------------------

echo
echo "[BASELINE] checking that no ScreenCapture grant exists pre-capture"
tcc_scan
if [[ "${TCC_READABLE}" -eq 1 ]]; then
  echo "  TCC read: ${TCC_DETAIL}"
  if [[ "${TCC_GRANTED}" -eq 0 ]]; then
    pass "[BASELINE]" "no authorized ${TCC_SERVICE} row for ${BUNDLE_ID}"
  else
    fail "[BASELINE]" "an authorized grant already exists — reset did not clear it"
  fi
else
  FDA_AVAILABLE=0
  echo "  TCC read: ${TCC_DETAIL}"
  print_fda_instructions
  skip "[BASELINE]" "cannot read TCC.db (needs Full Disk Access)"
  mark_inconclusive
fi

# --------------------------------------------------------------------------
# STEP 3 — [DRIVE] trigger one picker capture
# --------------------------------------------------------------------------

echo
echo "[DRIVE] triggering a picker capture"

if [[ -n "${APP_PATH}" ]]; then
  echo "  launching: ${APP_PATH}"
  if [[ -d "${APP_PATH}" || "${APP_PATH}" == *.app ]]; then
    open "${APP_PATH}" || info "[DRIVE]" "warning: 'open ${APP_PATH}' returned non-zero"
  else
    # A bare binary path: launch detached so the script keeps running.
    "${APP_PATH}" &
  fi
fi

if [[ -n "${CLICKS_FILE}" ]]; then
  if [[ ! -f "${CLICKS_FILE}" ]]; then
    fail "[DRIVE]" "clicks file not found: ${CLICKS_FILE}"
  elif [[ "${HAVE_CLICLICK}" -eq 0 ]]; then
    skip "[DRIVE]" "cliclick not installed — cannot replay ${CLICKS_FILE}"
    echo "  install with: brew install cliclick"
  else
    echo "  replaying clicks via cliclick -f ${CLICKS_FILE}"
    if cliclick -f "${CLICKS_FILE}"; then
      info "[DRIVE]" "cliclick replay finished"
    else
      # Fall back to line-by-line replay for older cliclick without -f.
      info "[DRIVE]" "cliclick -f failed; retrying line-by-line"
      while IFS= read -r line; do
        [[ -z "${line}" || "${line}" == \#* ]] && continue
        # shellcheck disable=SC2086
        cliclick ${line} || info "[DRIVE]" "warning: 'cliclick ${line}' failed"
      done < "${CLICKS_FILE}"
    fi
  fi
fi

if [[ -z "${APP_PATH}" && -z "${CLICKS_FILE}" ]]; then
  # Interactive manual drive — the reliable default.
  cat <<EOF
  Manual drive (no --app / --clicks given). In Trailer:
    1. Set  capture_backend = screencapturekit  (Settings ▸ [general]).
    2. Trigger a capture (Acquire, or Take Screenshot ▸ Window/Display).
    3. In the system 'choose what to share' picker, pick a ${MODE} and confirm.
    4. Watch closely: NO "Screen Recording" permission prompt should appear.
EOF
  read -r -p "  Press Enter once the capture completed... " _
fi

# --------------------------------------------------------------------------
# STEP 4 — [ASSERT] no grant was created (authoritative check)
# --------------------------------------------------------------------------

echo
echo "[ASSERT] checking that NO Screen-Recording grant was created (GPSC.2 core)"
if [[ "${FDA_AVAILABLE}" -eq 1 ]]; then
  tcc_scan
  echo "  TCC read: ${TCC_DETAIL}"
  if [[ "${TCC_READABLE}" -eq 1 ]]; then
    if [[ "${TCC_GRANTED}" -eq 0 ]]; then
      pass "[ASSERT]" "still no authorized ${TCC_SERVICE} row for ${BUNDLE_ID} — picker consent left no standing grant"
    else
      fail "[ASSERT]" "an authorized ${TCC_SERVICE} grant appeared for ${BUNDLE_ID} — the picker path created a standing grant (GPSC.2 regression)"
    fi
  else
    # FDA was present at baseline but the db went unreadable now — treat as skip.
    skip "[ASSERT]" "TCC.db became unreadable; rely on the diagnostic + your observation"
    mark_inconclusive
  fi
else
  skip "[ASSERT]" "no Full Disk Access — cannot read TCC.db; rely on the diagnostic below + your visual confirmation that no prompt appeared"
  mark_inconclusive
fi

# --------------------------------------------------------------------------
# STEP 5 — [DIAGNOSTIC] harness-process preflight
# --------------------------------------------------------------------------

echo
echo "[DIAGNOSTIC] CGPreflightScreenCaptureAccess() for THIS harness process"
echo "  NOTE: this reports the harness process's OWN Screen-Recording state, a"
echo "        sanity signal only (expected 'not-granted' on a clean run). It is"
echo "        NOT Trailer's grant — the authoritative per-app check is [ASSERT]."
if [[ "${HAVE_SWIFT}" -eq 1 && -f "${PREFLIGHT_SWIFT}" ]]; then
  if preflight_out="$(swift "${PREFLIGHT_SWIFT}" 2>/dev/null)"; then
    info "[DIAGNOSTIC]" "harness preflight: ${preflight_out} (harness holds Screen Recording)"
  else
    info "[DIAGNOSTIC]" "harness preflight: ${preflight_out:-not-granted} (harness has no Screen Recording — expected)"
  fi
else
  skip "[DIAGNOSTIC]" "swift not available or preflight.swift missing — skipping preflight"
fi

# --------------------------------------------------------------------------
# Summary
# --------------------------------------------------------------------------

echo
echo "--------------------------------------------------------------"
echo " Checklist mapping:"
echo "   [SETUP]/[BASELINE] -> fresh 'tccutil reset ScreenCapture' state"
echo "   [ASSERT]           -> GPSC.2 (b): NO Screen & System Audio Recording entry"
echo "   [DRIVE] + eyes     -> GPSC.2 (a): NO 'Screen Recording' prompt appeared"
echo "   [DIAGNOSTIC]       -> harness-process sanity signal (non-authoritative)"
echo "--------------------------------------------------------------"
printf ' RESULT: %s\n' "${OVERALL}"
echo "=============================================================="

case "${OVERALL}" in
  PASS)         exit 0 ;;
  INCONCLUSIVE) exit 0 ;;   # not a failure; needs FDA and/or human confirmation
  FAIL)         exit 1 ;;
  *)            exit 1 ;;
esac
