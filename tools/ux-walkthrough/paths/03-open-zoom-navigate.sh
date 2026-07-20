#!/usr/bin/env bash
# Golden path 3 — OPEN-IMAGE -> ZOOM -> NAVIGATE.
#
# Persona-(A) goal: open an image, zoom in/out, and confirm the zoom-%
# readout updates (finding #5, H1) and navigation works.
#
# Drives resilient selectors (keyboard shortcuts, stable across the PR #86
# menu rename):
#   Ctrl+=  Zoom In     (action.view.zoomIn)
#   Ctrl+-  Zoom Out    (action.view.zoomOut)
#   Ctrl+0  Actual Size (action.view.actualSize)
#   PageDown/PageUp     Next/Previous Page (action.view.nextPage/previousPage)
#
# HONEST PLATFORM BOUNDARY: Trailer has no cross-file "next/previous IMAGE"
# navigation; next/previous is PAGE navigation, gated on pageCount>1
# (src/ui/MainWindow.cpp updateActionStates). For a single image those
# actions are correctly disabled (a G3 non-lying-control state). This path
# opens an image (per the spec) for the zoom half — the finding-#5 core —
# and drives PageDown to show the single-page navigation state; multi-page
# navigation is exercised by opening a PDF.
set -uo pipefail
export PATH_NAME="open-zoom-navigate"
# shellcheck source=../lib/harness.sh
source "$HARNESS_LIB"

FIX="$RUN_DIR/open-source.png"
convert -size 600x420 gradient:teal-yellow \
    -gravity center -pointsize 34 -fill black -annotate 0 "ZOOM TEST\n600x420" \
    "$FIX" 2>/dev/null || _fail "could not generate fixture (ImageMagick)"

# --- Step 1: open the image ------------------------------------------------
step "opened" "Image opens at oracle default zoom (100% / 1:1), window sized to the image."
launch "$FIX"
# Assert on the fixture stem (not bare "Trailer") so this proves the document
# opened rather than merely that the app is running.
assert_title "open-source"
note "oracle: zoom readout should read 100% on open"
shot

# --- Step 2: zoom in -------------------------------------------------------
step "zoom-in" "Zoom In (Ctrl+=) increases magnification; the zoom-% readout updates upward."
press "ctrl+equal"
note "watch the status-bar zoomIndicator (objectName zoomIndicator) for a >100% value"
shot

# --- Step 3: zoom in again -------------------------------------------------
step "zoom-in-again" "A second Zoom In increases magnification further; readout keeps updating."
press "ctrl+equal"
shot

# --- Step 4: zoom out ------------------------------------------------------
step "zoom-out" "Zoom Out (Ctrl+-) decreases magnification; readout updates downward."
press "ctrl+minus"
shot

# --- Step 5: actual size ---------------------------------------------------
step "actual-size" "Actual Size (Ctrl+0) returns to 100%; readout reads 100%."
press "ctrl+0"
note "oracle: readout should return to 100%"
shot

# --- Step 6: navigate ------------------------------------------------------
step "navigate-next" "PageDown navigates pages; for a single image, next/prev are correctly inert (pageCount==1)."
boundary "No cross-file image navigation exists; next/previous is page navigation (pageCount>1). Single image -> inert by design."
press "Next"           # PageDown; no-op for a single-page image
shot

_log "path complete"
