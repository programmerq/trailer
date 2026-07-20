#!/usr/bin/env bash
# Golden path 4 — CLOSE-WITH-UNSAVED -> PROMPT.
#
# Persona-(A) goal: with unsaved edits, closing the window must warn before
# discarding — the user must SEE progress toward "don't lose my work".
#
# This path is the reason Tier-1 uses a REAL X server rather than
# QT_QPA_PLATFORM=offscreen: the unsaved-changes prompt is deliberately
# SKIPPED under the offscreen/minimal platforms (src/ui/MainWindow.cpp
# closeEvent / confirmCloseDirtyDoc) and fires ONLY on a real platform such
# as xcb. Offscreen static capture is structurally blind to this modal;
# Xvfb + xcb reproduces it.
#
# Resilient selectors: Ctrl+R rotate (action.tools.rotateRight) to dirty the
# doc; Ctrl+W close (action.file.close). The dirty state is also observable
# in the window title's "• " marker (MainWindow.cpp).
set -uo pipefail
export PATH_NAME="close-with-unsaved"
# shellcheck source=../lib/harness.sh
source "$HARNESS_LIB"

FIX="$RUN_DIR/edit-source.png"
convert -size 560x400 gradient:red-white \
    -gravity center -pointsize 34 -fill black -annotate 0 "EDIT ME\n560x400" \
    "$FIX" 2>/dev/null || _fail "could not generate fixture (ImageMagick)"

# --- Step 1: open a clean document -----------------------------------------
step "opened-clean" "Image opens clean; window title has no unsaved-changes marker."
launch "$FIX"
assert_title "Trailer"
shot

# --- Step 2: make an edit (dirty the document) -----------------------------
step "edited-dirty" "Rotate (Ctrl+R) edits the image; the title gains the '•' unsaved marker."
press "ctrl+r"         # action.tools.rotateRight -> pixel mutation -> dirty
_ms_sleep 400
assert_title "•"       # dirty marker in the title
shot

# --- Step 3: close -> unsaved-changes prompt -------------------------------
step "close-prompts" "Ctrl+W (Close Window) raises the 'Unsaved changes' Save/Discard/Cancel prompt."
press "ctrl+w"         # action.file.close
_ms_sleep 600
assert_window "Unsaved changes"
note "the modal that offscreen capture is blind to — Save/Discard/Cancel"
shot

# --- Step 4: cancel keeps the work -----------------------------------------
step "cancel-keeps-work" "Cancel (Esc) aborts the close; the dirty document remains open."
press "Escape"
_ms_sleep 400
_WIN_ID="$(_find_window)"
assert_title "•"       # still dirty, still open
shot

_log "path complete"
