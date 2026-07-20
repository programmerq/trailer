#!/usr/bin/env bash
# Golden path 7 — FREEHAND DRAW -> ZOOM -> DRAW-OVER  (Area 1, PR #91).
#
# Persona-(A) goal: from "I want to sketch on this image", draw a freehand
# stroke, zoom in, confirm the existing stroke stays glued to the document
# (no drift), then draw a SECOND stroke at the new zoom ("draw-over") and
# confirm it lands under the cursor. Finally round-trip back to 100% and
# confirm both strokes are still correctly anchored.
#
# This exercises the tool-state x zoom-transform integration seam that #91
# reworked (freehand latency + selection precedence; zoom drift left open for
# a real-Mac check). We drive the REAL binary so the doc->view / view->doc
# coordinate callbacks (AnnotationOverlay setDocumentToView / setViewToDocument,
# wired through ImageDocument::mapDocToView / mapViewToDoc) are exercised live.
#
# SELECTOR NOTE (documented fragility): unlike the shortcut-driven paths, the
# Freehand tool has NO keyboard shortcut and the markup toolbar is icon-only,
# so this path must CLICK the Freehand button by pixel offset from the window
# origin. Offsets are derived from the window geometry (not absolute screen
# coords) so they survive the WM placing the window anywhere. If the markup
# toolbar layout changes, update TOOL_FREEHAND_DX/DY below. Everything else
# (zoom, actual-size) is driven by stable shortcuts.
#
# HARNESS BOUNDARY: dpr=1 under Xvfb. The residual #91 "zoom drift" suspect is
# continuous trackpad pinch-zoom on Retina (dpr=2), which a headless X server
# cannot generate — that stays on the owner real-Mac checklist. This path
# proves the discrete-zoom anchoring seam, which is what is reachable here.
set -uo pipefail
export PATH_NAME="draw-zoom"
# shellcheck source=../lib/harness.sh
source "$HARNESS_LIB"

# --- Freehand button offset from the window's top-left ----------------------
# Markup toolbar is the second toolbar row. Tool order:
#   Select | Rectangle Ellipse Line Arrow Freehand Text Note ...
# Freehand ("~") sits ~157 px in and ~79 px down from the window origin at the
# default 18-px icon size. Tuned against main @ 6aab23f; see SELECTOR NOTE.
TOOL_FREEHAND_DX=157
TOOL_FREEHAND_DY=79

# Click a point given as (dx,dy) offset from the trailer window's top-left.
click_win_offset() {
    activate
    local geo x y
    geo="$(xdotool getwindowgeometry --shell "$_WIN_ID" 2>/dev/null)"
    eval "$geo"   # sets X= Y= WIDTH= HEIGHT=
    xdotool mousemove "$((X + $1))" "$((Y + $2))"
    _settle
    xdotool click 1
    _settle
}

# Drag the mouse through a list of "dx,dy" window-offset points with the left
# button held — i.e. draw one freehand stroke. First point = press, last = up.
drag_stroke() {
    activate
    local geo X Y first=1 pt dx dy
    geo="$(xdotool getwindowgeometry --shell "$_WIN_ID" 2>/dev/null)"
    eval "$geo"
    for pt in "$@"; do
        dx="${pt%,*}"; dy="${pt#*,}"
        xdotool mousemove "$((X + dx))" "$((Y + dy))"
        if [ "$first" = 1 ]; then
            _ms_sleep 200; xdotool mousedown 1; first=0
        fi
        _ms_sleep 60
    done
    _ms_sleep 200
    xdotool mouseup 1
    _settle
}

# Generate an 800x600 grid fixture so stroke anchoring is measurable to the
# pixel (100-px cells; a red dot marks doc (100,100)).
FIX="$RUN_DIR/draw-source.png"
convert -size 100x100 xc:'#fafafa' -fill none -stroke '#8080c0' -strokewidth 2 \
    -draw "rectangle 0,0 99,99" "$RUN_DIR/_cell.png" 2>/dev/null || \
    _fail "could not generate cell (ImageMagick)"
convert -size 800x600 tile:"$RUN_DIR/_cell.png" \
    -fill '#cc2020' -draw "circle 100,100 100,110" \
    -fill '#222' -pointsize 30 -gravity center -annotate 0 "GRID 800x600" \
    "$FIX" 2>/dev/null || _fail "could not generate fixture (ImageMagick)"

# --- Step 1: open the image, reset to a known 100% -------------------------
step "opened-actual" "Image opens; Ctrl+0 pins a known 1:1 (100%) baseline before drawing."
launch "$FIX"
assert_title "draw-source"
press "ctrl+0"      # normalise zoom so stroke coords are predictable
note "oracle: at Actual Size the grid should render 1:1 (800 px wide) and the readout should be 100%"
shot

# --- Step 2: surface the markup toolbar and pick Freehand ------------------
step "freehand-selected" "Ctrl+Shift+A shows the markup toolbar; the Freehand (~) tool highlights when picked."
press "ctrl+shift+a"
click_win_offset "$TOOL_FREEHAND_DX" "$TOOL_FREEHAND_DY"
note "Freehand has no shortcut + the toolbar is hidden by default (discoverability note, cognitive-walkthrough Q2)"
shot

# --- Step 3: draw a stroke at 100% -----------------------------------------
step "stroke-at-100" "An L-shaped freehand stroke draws under the cursor; the title gains the unsaved-changes marker."
# L-shape in window-offset coords (image origin ~ (100,52) below menu+toolbars).
drag_stroke "380,305" "480,305" "530,325" "580,355" "580,405" "580,505"
note "expect: dark stroke follows the drag path; title shows the '•' dirty marker"
shot

# --- Step 4: zoom in — does the stroke stay glued? -------------------------
step "zoom-in-anchoring" "Ctrl+= twice; the EXISTING stroke must stay glued to the same grid intersections (no drift)."
press "ctrl+equal"; press "ctrl+equal"
note "oracle: stroke and grid scale/move together; the stroke's endpoints keep the same doc coords (no offset)"
note "KNOWN-HARD SEAM (#91): watch for the stroke sliding relative to the grid — that is the zoom-drift bug"
shot

# --- Step 5: draw-over at the new zoom -------------------------------------
step "draw-over-at-zoom" "Re-select Freehand (it auto-reverts to Select on commit), then draw a 2nd stroke at the zoomed level."
# NOTE: onAnnotationCommitted flips the tool back to Select after every commit,
# so a draw-over MUST re-pick Freehand first or the drag becomes a rubber-band
# select and silently draws nothing (finding F3). Re-pick, then draw.
click_win_offset "$TOOL_FREEHAND_DX" "$TOOL_FREEHAND_DY"
drag_stroke "760,485" "820,485" "880,500" "940,485"
note "oracle: the new stroke lands exactly under the cursor path (viewToDoc mapping holds at non-100% zoom)"
note "if nothing appears: the tool likely reverted to Select and was not re-picked (finding F3)"
shot

# --- Step 6: round-trip back to 100% ---------------------------------------
step "roundtrip-100" "Ctrl+0; BOTH strokes must render at their correct document coordinates at 100% (zero drift)."
press "ctrl+0"
note "oracle: stroke #1 (drawn at 100%) and stroke #2 (drawn while zoomed) both sit at their true doc positions"
note "also observe the title: the '•' unsaved marker wrongly clears on zoom when the doc is dirty only via annotations (finding F4)"
shot

_log "path complete"
