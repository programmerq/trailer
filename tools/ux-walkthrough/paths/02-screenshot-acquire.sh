#!/usr/bin/env bash
# Golden path 2 — SCREENSHOT-ACQUIRE.
#
# Persona-(A) goal: invoke the screenshot/acquire action and confirm the
# captured image opens as a document.
#
# Linux/Tier-1 reality: Tools -> Take Screenshot (Ctrl+Shift+3,
# objectName action.tools.takeScreenshot) IS present on Linux. It shows a
# capture-mode dialog, then whole-screen capture goes through
# QScreen::grabWindow(0) (src/ui/MainWindow.cpp onTakeScreenshot #else
# branch) and opens the grab as a document — which works under Xvfb.
#
# HONEST PLATFORM BOUNDARIES:
#   * Window/region capture are disabled on Linux (a G3 non-lying-control
#     state with an explanatory note in the dialog); only whole-screen runs.
#   * The macOS native TCC "Screen Recording" permission prompt is real-Mac
#     only and is routed to the owner checklist (SKILL.md). Under Xvfb there
#     is no OS permission gate, so we drive the whole app-side flow to
#     completion.
set -uo pipefail
export PATH_NAME="screenshot-acquire"
# shellcheck source=../lib/harness.sh
source "$HARNESS_LIB"

# --- Step 1: empty state ---------------------------------------------------
step "empty-state" "App open, no document — the state a user invokes Acquire from."
launch     # no file -> empty state
shot

# --- Step 2: open the capture dialog --------------------------------------
step "take-screenshot-dialog" \
    "Tools -> Take Screenshot opens the capture-mode dialog (whole-screen; window/region disabled on Linux)."
press "ctrl+shift+3"   # objectName action.tools.takeScreenshot
assert_window "Take Screenshot"
boundary "Window/region modes disabled on Linux (non-lying control + note); macOS TCC prompt -> owner real-Mac checklist."
shot

# --- Step 3: confirm capture; captured screen opens as a document ----------
step "captured-image-opens" \
    "Accepting the dialog captures the X screen and opens it as a new document."
press "Return"         # accept default (Whole screen) -> grabWindow -> openFiles
_ms_sleep 800
# The newly-opened document window replaces/adds to the app; re-find it.
_WIN_ID="$(_find_window)"
assert_title "Trailer"
note "oracle: the captured screen opens as a document; zoom readout should be visible"
shot

_log "path complete"
