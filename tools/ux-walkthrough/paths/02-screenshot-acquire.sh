#!/usr/bin/env bash
# Golden path 2 — SCREENSHOT-ACQUIRE.
#
# Persona-(A) goal: invoke the screenshot/acquire action and confirm the
# captured image opens as a document.
#
# Linux/Tier-1 reality: Tools -> Take Screenshot (objectName
# action.tools.takeScreenshot) IS present on Linux. It shows a
# capture-mode dialog, then whole-screen capture goes through
# QScreen::grabWindow(0) (src/ui/MainWindow.cpp onTakeScreenshot #else
# branch) and opens the grab as a document — which works under Xvfb.
#
# SELECTOR NOTE: this action carries a stable objectName but NO keyboard
# shortcut — PR #86 removed the OS-reserved ⌘⇧3 binding as a lying control
# (DR 2026-07-18-file-menu-acquire-ia). We reach it by menu mnemonic
# (Tools -> Take Screenshot => Alt+T, then T), which is stable across the
# File-menu IA rename and needs no pixel geometry.
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

# The Linux capture writes to QStandardPaths::PicturesLocation (~/Pictures).
# A bare CI container has no XDG user dirs, so that directory doesn't exist and
# the grab's save() fails silently (a real desktop always has ~/Pictures). This
# is honest environment normalisation, not faking the feature: with the dir
# present the whole flow — grabWindow -> save -> openFiles -> document window —
# genuinely runs headless under Xvfb. Without it, step 3 cannot reach the
# captured-image-opens state.
mkdir -p "$HOME/Pictures"

# --- Step 1: empty state ---------------------------------------------------
step "empty-state" "App open, no document — the state a user invokes Acquire from."
launch     # no file -> empty state
shot

# --- Step 2: open the capture dialog --------------------------------------
step "take-screenshot-dialog" \
    "Tools -> Take Screenshot opens the capture-mode dialog (whole-screen; window/region disabled on Linux)."
menupick t t           # Tools -> Take Screenshot (objectName action.tools.takeScreenshot; no shortcut post-#86)
assert_window "Take Screenshot"
boundary "Window/region modes disabled on Linux (non-lying control + note); macOS TCC prompt -> owner real-Mac checklist."
shot

# --- Step 3: confirm capture; captured screen opens as a document ----------
step "captured-image-opens" \
    "Accepting the dialog captures the X screen and opens it as a new document."
press "Return"         # accept default (Whole screen) -> grabWindow -> openFiles
_ms_sleep 1200
# The captured screen opens as a NEW document window titled
# "trailer-screenshot-<stamp>.png"; find that window specifically and assert on
# its stem, not a bare "Trailer" (which the empty state also shows).
_WIN_ID="$(xdotool search --name 'trailer-screenshot' 2>/dev/null | head -1)"
[ -n "$_WIN_ID" ] || _WIN_ID="$(_find_window)"
assert_window "trailer-screenshot"
assert_title "trailer-screenshot"
note "oracle: the captured screen opened as a document; zoom readout should be visible"
shot

_log "path complete"
