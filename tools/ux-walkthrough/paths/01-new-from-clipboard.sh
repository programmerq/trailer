#!/usr/bin/env bash
# Golden path 1 — NEW-FROM-CLIPBOARD.
#
# Persona-(A) goal: from "I have an image on my clipboard", get a correctly
# rendered, correctly sized document, using native conventions.
#
# HONEST PLATFORM BOUNDARY (Linux/Tier-1): the clipboard-consuming menu
# action ("New from Clipboard", ⌘-bound) lives in src/app/Application.cpp
# under `#ifdef Q_OS_MACOS` (installNoWindowMenuBar / newFromClipboard,
# lines ~389-560) and is NOT compiled on Linux. So there is no Linux menu
# item to invoke. This path therefore:
#   (a) proves the harness can put a real image on the X clipboard and read
#       it back (the input half of new-from-clipboard), and
#   (b) reproduces the EXACT observable outcome of Application::newFromClipboard
#       — clipboard image -> temp PNG -> openFiles() — so persona (A) can judge
#       the resulting document against the zoom/size oracle.
# The ⌘N binding and the menu wiring themselves are macOS-only and are routed
# to the owner real-Mac checklist (SKILL.md), not asserted here.
set -uo pipefail
export PATH_NAME="new-from-clipboard"
# shellcheck source=../lib/harness.sh
source "$HARNESS_LIB"

FIX="$RUN_DIR/clipboard-source.png"
# A recognisable non-trivial image so the judge can confirm it is THIS image
# that opened (label baked into the pixels).
convert -size 520x360 gradient:orange-purple \
    -gravity center -pointsize 40 -fill white -annotate 0 "CLIPBOARD\nIMAGE" \
    "$FIX" 2>/dev/null || _fail "could not generate fixture (ImageMagick)"

# --- Step 1: put the image on the X clipboard, show the paste target -------
step "empty-state-clipboard-primed" \
    "App shows the empty state; an image is genuinely on the clipboard ready to paste."
# xclip forks and holds the CLIPBOARD selection for the life of the X session.
xclip -selection clipboard -t image/png -i "$FIX" 2>/dev/null
_ms_sleep 400
CLIP_BYTES="$(xclip -selection clipboard -t image/png -o 2>/dev/null | wc -c)"
if [ "$CLIP_BYTES" -gt 0 ]; then
    note "clipboard holds image/png: ${CLIP_BYTES} bytes"
else
    _fail "clipboard did not accept the image"
fi
boundary "Linux build has NO clipboard-consuming menu action (macOS-only, Application.cpp #ifdef Q_OS_MACOS). ⌘N/menu wiring -> owner real-Mac checklist."
launch    # no file argument -> empty state
shot

# --- Step 2: reproduce newFromClipboard's outcome (clip -> PNG -> open) -----
step "clipboard-image-opens" \
    "The clipboard image opens as a document at oracle zoom (100%) and is visible."
PASTED="$RUN_DIR/pasted-from-clipboard.png"
xclip -selection clipboard -t image/png -o >"$PASTED" 2>/dev/null
[ -s "$PASTED" ] || _fail "could not read image back off the clipboard"
note "read clipboard back to $PASTED ($(wc -c <"$PASTED") bytes) — mirrors Application::newFromClipboard's temp-PNG step"
teardown           # close the empty window
launch "$PASTED"   # open the clipboard-derived image, exactly as newFromClipboard would
# Assert on the fixture stem, not just "Trailer" — the empty state is also
# titled "Trailer", so a bare "Trailer" match would not prove a document opened.
assert_title "pasted-from-clipboard"
note "oracle: default zoom on open should read 100% (1:1) for an at-or-below-viewport image; window sized to the image"
shot

_log "path complete"
