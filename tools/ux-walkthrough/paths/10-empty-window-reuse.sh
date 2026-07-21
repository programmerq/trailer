#!/usr/bin/env bash
# Golden path 10 — EMPTY-WINDOW REUSE (CF-5), the Preview-style no-orphan rule.
#
# Persona-(A) goal: when the user has the empty launch window in front of them
# and opens (or pastes, or batch-opens) a document, that document should land
# in the window they are already looking at — not spawn a second window and
# orphan the empty one — WHILE never clobbering a window that already holds a
# real document.
#
# This path exists to replace the PR's "CF-5 reuse can't be checked offscreen"
# caveat with DRIVEN, WM-visible results. The reuse decision lives entirely in
# Application::openFiles() (src/app/Application.cpp, the `reuseCandidate` /
# `takeReuseOrFresh` logic): it only fires under a real activeWindow()/single
# live window, and it is exactly the class of behaviour a real X server +
# window manager is needed to observe. Under QT_QPA_PLATFORM=offscreen there is
# no WM-visible window count to assert on.
#
# THE ORACLE is the TOP-LEVEL APP WINDOW COUNT before vs after each open,
# measured by WM_CLASS ("trailer") — the same selector the harness uses to find
# the app window. Reuse == the count stays 1; no-clobber == the count goes to 2.
#
# Four WM-driven scenarios (each a fresh app instance, launched empty unless
# noted, so the reuse is genuinely from an ALREADY-OPEN empty window — never
# from a CLI file argument, which would bypass the empty-window path):
#   A  File -> Open (Ctrl+O) a single file  -> reuse (1 -> 1)
#   B  New from Clipboard (Ctrl+N) an image -> reuse (1 -> 1)
#   C  File -> Open a 2-image batch          -> reuse into one tabbed window (1 -> 1)
#   Control  a document is already open, open another -> NEW window (1 -> 2)
#
# HONEST SELECTOR NOTES:
#  * Scenarios A and C drive the real File -> Open QFileDialog. The harness
#    type()/press() verbs call activate(), which re-raises the MAIN window and
#    would dismiss the modal file dialog (same hazard path 09 documents for
#    menus), so the dialog is driven with RAW xdotool after focusing the "Open"
#    dialog window: type the path(s) into its filename field, press Return.
#    Multi-select uses the Qt dialog's quoted-list syntax ("a" "b").
#  * Scenario B drives Ctrl+N (QKeySequence::New -> New from Clipboard), which
#    PR #86 made cross-platform (see path 05), so the Linux Ctrl+N clipboard
#    open is real, not a reproduction.
#
# BOUNDARY: the macOS-native no-window / Dock-reopen behaviour and ⌘-key
# specifics stay on the owner real-Mac checklist; this tier proves the
# cross-platform openFiles() reuse guard under a real WM.
set -uo pipefail
export PATH_NAME="empty-window-reuse"
# shellcheck source=../lib/harness.sh
source "$HARNESS_LIB"

# --- fixtures --------------------------------------------------------------
DOC_A="$RUN_DIR/reuse-doc-a.png"
DOC_B="$RUN_DIR/reuse-doc-b.png"
convert -size 480x340 gradient:orange-purple \
    -gravity center -pointsize 34 -fill white -annotate 0 "DOC A\nreuse test" \
    "$DOC_A" 2>/dev/null || _fail "could not generate fixture DOC A (ImageMagick)"
convert -size 480x340 gradient:teal-yellow \
    -gravity center -pointsize 34 -fill black -annotate 0 "DOC B\nreuse test" \
    "$DOC_B" 2>/dev/null || _fail "could not generate fixture DOC B (ImageMagick)"

# --- local oracle helpers --------------------------------------------------
# Count top-level app windows by WM_CLASS. NOTE: the File -> Open dialog shares
# the app's WM_CLASS, so this is only meaningful when NO modal dialog is open —
# every call site below counts AFTER the dialog has closed and the UI settled.
win_count() { xdotool search --class 'trailer' 2>/dev/null | wc -l | tr -d ' '; }

_ANY_FAIL=0
# Emit the machine-checkable assertion line the run log captures, e.g.
#   REUSE-A before=1 after=1 PASS
# and flip the failure flag (the whole path exits non-zero) on a miss.
assert_counts() {  # tag before after expected_after
    local tag="$1" before="$2" after="$3" want="$4" verdict
    if [ "$after" = "$want" ]; then verdict="PASS"; else verdict="FAIL"; _ANY_FAIL=1; fi
    printf '%s before=%s after=%s expected_after=%s %s\n' "$tag" "$before" "$after" "$want" "$verdict"
    note "$tag before=$before after=$after expected_after=$want $verdict"
    [ "$verdict" = "PASS" ]
}

# Drive the real File -> Open dialog with RAW xdotool (harness type()/press()
# would re-raise the main window and dismiss the modal). $1 is the exact text to
# type into the filename field (already quoted for multi-select if needed).
open_via_dialog() {
    activate                                   # bring the main window forward
    xdotool key --clearmodifiers ctrl+o
    _settle; _ms_sleep 800
    local open_id=""
    local waited=0
    while [ "$waited" -lt 20 ]; do
        open_id="$(xdotool search --name '^Open$' 2>/dev/null | head -1)"
        [ -n "$open_id" ] && break
        _ms_sleep 300; waited=$((waited + 1))
    done
    [ -n "$open_id" ] || _fail "File -> Open dialog did not appear"
    xdotool windowactivate --sync "$open_id" 2>/dev/null \
        || xdotool windowactivate "$open_id" 2>/dev/null || true
    _ms_sleep 400
    xdotool type --clearmodifiers -- "$1"
    _ms_sleep 400
    xdotool key --clearmodifiers Return
    _settle; _ms_sleep 900                     # let the dialog close + doc open
}

# ===========================================================================
# SCENARIO A — File -> Open reuse: empty launch window, open one file, still 1.
# ===========================================================================
step "reuse-A-file-open" \
    "Empty launch window; File -> Open a single file reuses that window (count stays 1)."
launch                                         # no file arg -> empty launch window
A_BEFORE="$(win_count)"
A_WIN_BEFORE="$_WIN_ID"
note "empty launch: app-window count=$A_BEFORE (win $A_WIN_BEFORE)"
assert_title "Trailer"                         # empty state is titled just "Trailer"
open_via_dialog "$DOC_A"
A_AFTER="$(win_count)"
assert_title "reuse-doc-a"                     # SAME window now shows the document
note "post-open window id still $_WIN_ID (reused in place)"
shot
assert_counts "REUSE-A" "$A_BEFORE" "$A_AFTER" 1 || true
teardown

# ===========================================================================
# SCENARIO B — Ctrl+N clipboard reuse: empty launch window, paste, still 1.
# ===========================================================================
step "reuse-B-clipboard-new" \
    "Empty launch window; New from Clipboard (Ctrl+N) opens the clip image into that window (count stays 1)."
# xclip forks and holds the CLIPBOARD selection for the X session's life.
xclip -selection clipboard -t image/png -i "$DOC_B" 2>/dev/null
_ms_sleep 400
CLIP_BYTES="$(xclip -selection clipboard -t image/png -o 2>/dev/null | wc -c)"
[ "$CLIP_BYTES" -gt 0 ] || _fail "clipboard did not accept the image"
note "clipboard holds image/png: ${CLIP_BYTES} bytes"
launch                                         # fresh empty launch window
B_BEFORE="$(win_count)"
note "empty launch: app-window count=$B_BEFORE"
assert_title "Trailer"
press "ctrl+n"                                 # QKeySequence::New -> newFromClipboard
_ms_sleep 900
B_AFTER="$(win_count)"
assert_title "trailer-clipboard"               # SAME window now holds the pasted image
shot
assert_counts "REUSE-B" "$B_BEFORE" "$B_AFTER" 1 || true
teardown

# ===========================================================================
# SCENARIO C — batch reuse: empty launch window, open 2 images -> one tabbed
# window (count stays 1; the batch shares a tab strip rather than 2 frames).
# ===========================================================================
step "reuse-C-batch-open" \
    "Empty launch window; opening a 2-image batch reuses that window as the shared tab strip (count stays 1)."
launch
C_BEFORE="$(win_count)"
note "empty launch: app-window count=$C_BEFORE"
assert_title "Trailer"
# Qt's file dialog multi-selects from a quoted list in the filename field.
open_via_dialog "\"$DOC_A\" \"$DOC_B\""
C_AFTER="$(win_count)"
note "batch of 2 images opened; a single window holds both as tabs (title shows the active tab)"
shot
assert_counts "REUSE-C" "$C_BEFORE" "$C_AFTER" 1 || true
teardown

# ===========================================================================
# CONTROL — no-clobber: a real document is already open; opening another file
# must spawn a NEW window (count goes 1 -> 2), never overwrite the active doc.
# ===========================================================================
step "noclobber-control-new-window" \
    "A document is already open; opening another file spawns a NEW window (count 1 -> 2), active doc untouched."
launch "$DOC_A"                                # start with a REAL document (not empty)
CTL_BEFORE="$(win_count)"
note "launched with a document: app-window count=$CTL_BEFORE (win $_WIN_ID holds DOC A)"
assert_title "reuse-doc-a"
open_via_dialog "$DOC_B"
CTL_AFTER="$(win_count)"
note "active document window must be untouched; the second file opened in its own window"
shot
assert_counts "NOCLOBBER" "$CTL_BEFORE" "$CTL_AFTER" 2 || true
teardown

# --- verdict ---------------------------------------------------------------
if [ "$_ANY_FAIL" -ne 0 ]; then
    _log "path complete WITH FAILURES (see the *_ FAIL lines above)"
    exit 1
fi
_log "path complete — all CF-5 reuse / no-clobber assertions PASS"
