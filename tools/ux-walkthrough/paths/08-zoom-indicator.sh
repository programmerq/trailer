#!/usr/bin/env bash
# Golden path 8 — ZOOM INDICATOR / STEPS / OPEN-AT-100%  (Area 2, #76/#80/#88).
#
# Persona-(A) goal: confirm the zoom-% readout tells the truth. Open an image
# and check it opens at the oracle default (1:1 for an image that fits), read
# the indicator, step the zoom in/out and confirm the readout tracks each step,
# and probe the min/max clamp.
#
# SUCCESS-CRITERIA ORACLE (declare before judging):
#   * An image that fits the viewport opens at a TRUE 100% render, and the
#     status-bar indicator reads "100%" (not a fit-to-fill >100%, and not a
#     stale/placeholder value).
#   * The indicator (objectName "zoomIndicator", MainWindow.cpp:555) is visible
#     whenever a zoomable doc is open, and its value tracks the actual render.
#   * Zoom In/Out step by a sensible ratio and the readout updates each step.
#   * A visible min/max clamp exists (kZoomMin 5% / kZoomMax 3200% for images).
#
# WHY A GRID FIXTURE: the harness cannot OCR the indicator, so the judge
# persona reads it from the screenshot. The 100-px grid lets the judge ALSO
# measure the true render magnification (cell px / 100) and compare it against
# the printed readout — the whole point of #88 (open-at-100%) and the
# readout-vs-render mismatch this path is built to catch.
#
# HARNESS BOUNDARY: dpr=1 under Xvfb; true-Retina 1:1 sharpness is an owner
# real-Mac item. Also: capture each step only AFTER the UI settles — the
# indicator updates on a repaint, and a too-fast grab catches a pre-update
# frame (an artifact, not an app bug). SETTLE_MS is bumped below for this
# reason.
set -uo pipefail
export PATH_NAME="zoom-indicator"
export SETTLE_MS="${SETTLE_MS:-900}"   # zoom readout needs a repaint to settle
# shellcheck source=../lib/harness.sh
source "$HARNESS_LIB"

# 800x600 grid: fits a normal window, so the open-at-100% oracle applies (a
# fitting image must NOT upscale). 100-px cells make the render measurable.
FIX="$RUN_DIR/zoom-source.png"
convert -size 100x100 xc:'#fafafa' -fill none -stroke '#8080c0' -strokewidth 2 \
    -draw "rectangle 0,0 99,99" "$RUN_DIR/_cell.png" 2>/dev/null || \
    _fail "could not generate cell (ImageMagick)"
convert -size 800x600 tile:"$RUN_DIR/_cell.png" \
    -fill '#cc2020' -draw "circle 100,100 100,110" \
    -fill '#2020cc' -draw "circle 700,500 700,510" \
    -fill '#222' -pointsize 30 -gravity center -annotate 0 "GRID 800x600" \
    "$FIX" 2>/dev/null || _fail "could not generate fixture (ImageMagick)"

# --- Step 1: open — the #88 open-at-100% default ---------------------------
step "open-default-zoom" "An 800x600 image (<= viewport) opens at a TRUE 100% render with a '100%' readout."
launch "$FIX"
assert_title "zoom-source"
note "ORACLE (#88): a fitting image opens 1:1 — grid cells must be 100 px on screen, NOT upscaled to fill the window"
note "ORACLE: the status-bar zoomIndicator must read 100% and MATCH the render (watch for a stale 5% / frozen value)"
note "JUDGE: measure a grid cell (px/100 = true zoom) and compare to the printed readout — they must agree"
shot

# --- Step 2: actual size, as a truth anchor --------------------------------
step "actual-size-truth" "Ctrl+0 = Actual Size gives a known 1:1; readout must read 100% and render 800 px wide."
press "ctrl+0"
note "this is the truth anchor: if open (step 1) differs from Actual Size here, the open-default is wrong (finding F1/F2)"
shot

# --- Step 3..5: zoom-in ladder ---------------------------------------------
step "zoom-in-1" "Ctrl+= : readout steps up one notch (x1.25) and the render grows to match."
press "ctrl+equal"
note "expect ~125%; readout must update"
shot

step "zoom-in-2" "Ctrl+= again: readout keeps climbing (x1.25) each tap."
press "ctrl+equal"
note "expect ~156%"
shot

step "zoom-in-3" "Ctrl+= again: readout continues to track the render."
press "ctrl+equal"
note "expect ~195%; steps are geometric x1.25 (non-round by the accepted zoom-polish decision, backlog 2026-07-16)"
shot

# --- Step 6: zoom-out ------------------------------------------------------
step "zoom-out" "Ctrl+- : readout steps DOWN and the render shrinks to match."
press "ctrl+minus"
note "expect the readout to drop one notch from the previous step"
shot

# --- Step 7: min clamp -----------------------------------------------------
step "zoom-floor-clamp" "Repeated Ctrl+- clamps at the floor (kZoomMin = 5%); the readout stops decreasing."
for i in $(seq 1 16); do press "ctrl+minus"; done
note "ORACLE: readout bottoms out at 5% and does not go lower (a visible min clamp)"
shot

# --- Step 8: max clamp -----------------------------------------------------
step "zoom-ceiling-clamp" "Repeated Ctrl+= clamps at the ceiling (kZoomMax = 3200%); the readout stops increasing."
# Extra settle per step: at high zoom the full-res pixmap rebuild is slow
# (an 800x600 source -> ~13200x9900 px, ~0.5 GB at 3200%) and rapid keys DROP
# (finding F6). We over-drive (more taps than the ~30 needed) AND add an extra
# per-tap pause so the readout reliably reaches the clamp. HONEST CAVEAT: if
# the host is slow this capture may still under-reach (e.g. show ~1654% instead
# of 3200%) purely because keys were dropped mid-rebuild — that is F6, not a
# broken clamp. The clamp value itself is kZoomMax = 3200% (ImageAdapter.cpp).
for i in $(seq 1 40); do press "ctrl+equal"; _ms_sleep 400; done
note "ORACLE: readout tops out at 3200% and does not go higher (a visible max clamp)"
note "PERF NOTE (F6): pixmap is rebuilt at full resolution each step; sluggish at extreme zoom, drops rapid keys"
shot

_log "path complete"
