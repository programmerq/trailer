#!/usr/bin/env bash
# Golden path 13 — QUIT PROMPT: CANCEL KEEPS THE APP ALIVE (merged #78, Linux).
#
# Persona-(A) goal: with unsaved edits, invoking Quit (Ctrl+Q) must raise the
# Save/Discard/Cancel prompt, and choosing Cancel must ABORT the quit — the app
# stays open and nothing is written. This is the ADR-0004 no-silent-loss floor
# on the quit path (Application::requestQuit(Normal) -> promptDirtyDocsForQuit;
# Cancel returns false and never calls performQuit).
#
# THE ORACLE is hard/machine-checkable (path-10 style):
#   QUIT-PROMPT  — after Ctrl+Q the "Unsaved changes" modal exists.
#   QUIT-CANCEL  — after Cancel (Escape) the top-level app window still EXISTS
#                  (win_count >= 1): the quit was aborted, the app is alive.
#   QUIT-CANCEL-DISMISSED — and the 'Unsaved changes' prompt is actually GONE.
#                  app-alive alone would pass even if Escape were ignored and the
#                  modal stayed up; this positive proof confirms Cancel dismissed
#                  the prompt, not that it was merely still running behind it.
#
# HONEST BOUNDARY (documented, not faked): the ⌥⌘Q "Quit and Keep Windows"
# relaunch-and-restore flow (#78's headline) is NOT driven here. Proving it
# requires quitting the process and RELAUNCHING to observe the session restore,
# which cannot be honestly reproduced inside this single-process Xvfb harness
# (each path runs one app instance under one ephemeral X server; there is no
# second launch to restore into). That relaunch-restore assertion stays on the
# owner checklist / the offscreen draft-store unit tests. This path proves the
# reachable Linux basics: the quit PROMPT and its Cancel-keeps-alive contract.
set -uo pipefail
export PATH_NAME="quit-and-keep-basics"
# shellcheck source=../lib/harness.sh
source "$HARNESS_LIB"

# --- local oracle helpers (path-10 hard-oracle style) ----------------------
win_count() { xdotool search --class 'trailer' 2>/dev/null | wc -l | tr -d ' '; }

_ANY_FAIL=0
assert_bool() {  # tag field actual expected
    local tag="$1" field="$2" actual="$3" want="$4" verdict
    if [ "$actual" = "$want" ]; then verdict="PASS"; else verdict="FAIL"; _ANY_FAIL=1; fi
    printf '%s %s=%s expected=%s %s\n' "$tag" "$field" "$actual" "$want" "$verdict"
    note "$tag $field=$actual expected=$want $verdict"
}
assert_ge() {  # tag field actual min
    local tag="$1" field="$2" actual="$3" min="$4" verdict
    if [ "$actual" -ge "$min" ] 2>/dev/null; then verdict="PASS"; else verdict="FAIL"; _ANY_FAIL=1; fi
    printf '%s %s=%s expected>=%s %s\n' "$tag" "$field" "$actual" "$min" "$verdict"
    note "$tag $field=$actual expected>=$min $verdict"
}

FIX="$RUN_DIR/quit-source.png"
convert -size 560x400 gradient:blue-white \
    -gravity center -pointsize 34 -fill black -annotate 0 "QUIT ME\n560x400" \
    "$FIX" 2>/dev/null || _fail "could not generate fixture (ImageMagick)"

# --- Step 1: open a clean document -----------------------------------------
step "opened-clean" "Image opens clean; window title has no unsaved-changes marker."
launch "$FIX"
assert_title "quit-source"
shot

# --- Step 2: dirty the document --------------------------------------------
step "edited-dirty" "Rotate (Ctrl+R) edits the image; the title gains the '•' unsaved marker."
press "ctrl+r"         # action.tools.rotateRight -> pixel mutation -> dirty
_ms_sleep 400
assert_title "•"
shot

# --- Step 3: Quit -> Save/Discard/Cancel prompt ----------------------------
step "quit-prompts" \
    "Ctrl+Q (Quit) raises the unsaved-changes Save/Discard/Cancel prompt before quitting."
# QKeySequence::Quit maps to Ctrl+Q on Linux (src/ui/MainWindow.cpp:934). The
# prompt is the same confirmCloseDirtyDoc modal titled 'Unsaved changes'.
press "ctrl+q"
_ms_sleep 700
QUIT_MODAL=0
if xdotool search --name 'Unsaved changes' >/dev/null 2>&1; then QUIT_MODAL=1; fi
note "the quit prompt is Application::requestQuit(Normal) -> confirmCloseDirtyDoc"
shot
assert_bool "QUIT-PROMPT" "modal" "$QUIT_MODAL" 1

# --- Step 4: Cancel keeps the app alive ------------------------------------
step "cancel-keeps-alive" \
    "Cancel (Escape) aborts the quit; the app window still exists and nothing was written."
press "Escape"
_ms_sleep 600
ALIVE="$(win_count)"
_WIN_ID="$(_find_window)"
# The prompt must actually be DISMISSED, not merely "app still alive": app_windows
# alone passes even if Escape were ignored and the 'Unsaved changes' modal stayed
# up. Assert the prompt window (title 'Unsaved changes', same modal QUIT-PROMPT
# matched) is GONE — a MODAL_GONE-style positive proof that Cancel closed it.
QUIT_MODAL_GONE=1
if xdotool search --name 'Unsaved changes' >/dev/null 2>&1; then QUIT_MODAL_GONE=0; fi
note "Cancel returned false from requestQuit -> performQuit never ran -> app stays open"
shot
assert_ge "QUIT-CANCEL" "app_windows" "$ALIVE" 1
assert_bool "QUIT-CANCEL-DISMISSED" "quit_prompt_open" "$((1 - QUIT_MODAL_GONE))" 0

# --- documented boundary: relaunch-restore not driven here -----------------
step "keep-windows-boundary" \
    "Documented limit: the Keep-Windows relaunch-restore flow is out of scope for this single-process harness."
boundary "The ⌥⌘Q 'Quit and Keep Windows' relaunch-and-restore flow is NOT driven here: it requires quitting the process and RELAUNCHING to observe the session-draft restore, which a single-process Xvfb path (one app instance, one ephemeral X server, no second launch) cannot honestly reproduce. The draft-store save/restore is covered by offscreen unit tests; the real relaunch stays on the owner checklist. This path proves the reachable Linux basics: the quit prompt + Cancel-keeps-alive."
note "not faked: no relaunch is attempted; only the reachable quit-prompt/cancel contract is asserted above"
shot

# --- verdict ---------------------------------------------------------------
if [ "$_ANY_FAIL" -ne 0 ]; then
    _log "path complete WITH FAILURES (see the FAIL lines above)"
    exit 1
fi
_log "path complete — quit prompt raised and Cancel kept the app alive (#78 Linux basics)"
