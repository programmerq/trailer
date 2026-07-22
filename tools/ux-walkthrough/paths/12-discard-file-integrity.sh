#!/usr/bin/env bash
# Golden path 12 — DISCARD LEAVES THE BACKING FILE BYTE-IDENTICAL (merged #90).
#
# Persona-(A) goal: a user opens a PDF, draws on it, then closes and chooses
# "Discard" at the unsaved-changes prompt. The merged #90 fix guarantees that an
# explicit Discard writes ONLY a recovery sidecar and NEVER touches the backing
# file — so the on-disk PDF must be byte-identical before and after.
#
# THE ORACLE is a sha256 of the backing PDF, captured BEFORE the edit and AGAIN
# after Discard, compared exactly:
#   INTEGRITY before=<sha> after=<sha> PASS/FAIL
# A Save would rewrite the file (annotation baked in) and change the hash; a
# Discard must leave it untouched. This is the hard, machine-checkable proof.
#
# NOCRASH — the crash-vs-Discard hole, closed. MODAL_GONE + DOC_CLOSED +
# unchanged-sha are all NEGATIVE observations: a crash right after the modal
# would satisfy every one of them too (a crash writes nothing to the backing
# file and leaves no windows). So we add a POSITIVE non-crash proof of the
# process's own fate. Observed empirically on Linux/xcb: despite
# Application::setQuitOnLastWindowClosed(false) (src/app/Application.cpp:64),
# choosing Discard on the LAST open document makes the process EXIT CLEANLY with
# status 0 (probe: process gone at t=0s, `wait` status 0, deterministic across
# runs) — it does NOT stay alive windowless. So the NOCRASH oracle reaps the
# launched child and asserts exit status 0; a crash would instead deliver a
# signal / non-zero status. `launch()` backgrounds the binary and stores its pid
# in $_APP_PID (see lib/harness.sh), so the child is `wait`-able here.
#
# This is the reason Tier-1 uses a REAL X server: the unsaved-changes prompt is
# deliberately SKIPPED under offscreen/minimal (src/ui/MainWindow.cpp
# confirmCloseDirtyDoc) and fires ONLY on a real platform such as xcb. The
# offscreen UAT tier can force a Discard response programmatically but cannot
# exercise the real modal + the real Discard button that a user clicks.
#
# SELECTOR NOTES (documented fragility):
#  * Dirtying uses the freehand markup tool, driven exactly like path 07:
#    Ctrl+Shift+A surfaces the markup toolbar, the Freehand button is clicked by
#    a client-relative pixel offset (icon-only toolbar, no shortcut), then the
#    mouse is dragged across the page. If the markup toolbar layout changes,
#    update TOOL_FREEHAND_DX/DY (shared with path 07).
#  * The unsaved-changes prompt is a QMessageBox (Save default / Discard /
#    Cancel, left-to-right). This theme renders NO mnemonic underlines, so the
#    Discard button is reached by keyboard FOCUS, not a mnemonic: the modal opens
#    with Save focused, one Tab moves focus to Discard (verified empirically —
#    the focus rectangle lands on Discard), and Space activates the focused
#    button. We do NOT use pixel coordinates. A shot of the open modal is
#    captured first so the button/focus state is visible in evidence, and three
#    independent oracles below confirm it was DISCARD (not Save, not Cancel):
#    the modal is gone, the backing file is byte-identical (not Save, which would
#    rewrite it), and the document actually closed (not Cancel, which would keep
#    it open and still dirty).
set -uo pipefail
export PATH_NAME="discard-file-integrity"
# shellcheck source=../lib/harness.sh
source "$HARNESS_LIB"

# Freehand button offset from the window's top-left — shared with path 07.
# Markup toolbar tool order: Select | Rectangle Ellipse Line Arrow Freehand ...
# Freehand sits ~156 px in and ~48 px down from the client-area origin at the
# default 18-px icon size. See path 07's SELECTOR NOTE for the geometry caveat.
TOOL_FREEHAND_DX=156
TOOL_FREEHAND_DY=48

# Click a point given as (dx,dy) offset from the trailer window's top-left.
click_win_offset() {
    activate
    local geo X Y WIDTH HEIGHT
    geo="$(xdotool getwindowgeometry --shell "$_WIN_ID" 2>/dev/null)"
    eval "$geo"   # sets X= Y= WIDTH= HEIGHT=
    xdotool mousemove "$((X + $1))" "$((Y + $2))"
    _settle
    xdotool click 1
    _settle
}

# Drag the mouse through a list of "dx,dy" window-offset points, left button
# held — i.e. draw one freehand stroke. First point = press, last = up.
drag_stroke() {
    activate
    local geo X Y WIDTH HEIGHT first=1 pt dx dy
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

_ANY_FAIL=0

# --- fixture: a real PDF, hash recorded BEFORE any edit --------------------
FIX="$RUN_DIR/discard-fixture.pdf"
convert -size 600x800 xc:white \
    -gravity center -pointsize 40 -fill black -annotate 0 "fixture" \
    "$FIX" 2>/dev/null || _fail "could not generate PDF fixture (ImageMagick)"
SHA_BEFORE="$(sha256sum "$FIX" | awk '{print $1}')"
_log "backing PDF sha256 BEFORE: $SHA_BEFORE"

# --- Step 1: open the PDF clean --------------------------------------------
step "opened-clean" "PDF opens clean; window title has no unsaved-changes marker."
launch "$FIX"
assert_title "discard-fixture"
shot

# Is the active document dirty (title carries the '•' marker)?
is_dirty() { xdotool getwindowname "$_WIN_ID" 2>/dev/null | grep -q '•'; }

# --- Step 2: draw a freehand stroke -> the doc goes dirty ------------------
step "drew-stroke-dirty" \
    "Freehand markup: with the markup toolbar shown, pick Freehand and drag across the page; the title gains the '•' unsaved marker."
# The markup toolbar is shown by default for editable docs, but its visibility
# is persisted in QSettings (Ctrl+Shift+A TOGGLES it), so across repeated harness
# runs sharing one $HOME it may start hidden OR shown — we cannot assume. The
# freehand tool has no shortcut and the toolbar is icon-only, so tool selection +
# drag is inherently less deterministic than a shortcut, and the tool auto-reverts
# to Select after each commit (path 07 finding F3). So: attempt a stroke; if it
# didn't dirty the doc, TOGGLE the toolbar and retry. This converges on the
# toolbar-visible state regardless of the persisted default, re-picking Freehand
# each time. A visible-toolbar attempt reliably draws; the loop guarantees several.
DREW=0
for attempt in 1 2 3 4 5 6; do
    click_win_offset "$TOOL_FREEHAND_DX" "$TOOL_FREEHAND_DY"   # (re-)pick Freehand
    # An L-shaped multi-point stroke over the page body (below the toolbar stack).
    drag_stroke "300,300" "380,300" "430,330" "470,370" "470,430" "470,520"
    _ms_sleep 400
    if is_dirty; then DREW=1; note "freehand stroke registered on attempt $attempt"; break; fi
    note "attempt $attempt did not dirty the doc; toggling the markup toolbar and retrying"
    press "ctrl+shift+a"   # flip toolbar visibility, then retry in the other state
    _ms_sleep 500
done
[ "$DREW" = 1 ] || _fail "could not dirty the PDF with a freehand stroke after retries"
assert_title "•"       # dirty marker in the title
note "the freehand annotation dirtied the doc without touching the backing file yet"
shot

# --- Step 3: close -> unsaved-changes prompt -------------------------------
step "close-prompts" "Ctrl+W raises the 'Unsaved changes' Save/Discard/Cancel prompt."
press "ctrl+w"         # action.file.close
_ms_sleep 700
assert_window "Unsaved changes"
note "the real modal offscreen capture is blind to — Save (default) / Discard / Cancel"
shot                   # evidence: the open modal with its buttons/focus visible

# --- Step 4: Discard -> the backing file must be untouched ------------------
step "discard-keeps-file" \
    "Discard (Tab to the Discard button, Space) drops the in-memory edits and closes; the backing PDF is byte-identical (only a recovery sidecar was ever written)."
# Drive the modal directly (no pixel coordinates): focus the Unsaved-changes
# window, Tab moves focus off the default Save onto Discard, Space activates it.
MODAL_ID="$(xdotool search --name 'Unsaved changes' 2>/dev/null | head -1)"
[ -n "$MODAL_ID" ] || _fail "no Unsaved-changes modal to drive Discard on"
xdotool windowactivate --sync "$MODAL_ID" 2>/dev/null || xdotool windowactivate "$MODAL_ID" 2>/dev/null || true
_ms_sleep 300
xdotool key --clearmodifiers Tab       # Save (default) -> Discard (focus rectangle lands on Discard)
_ms_sleep 300
xdotool key --clearmodifiers space     # activate the focused Discard button
_ms_sleep 900
shot

# Oracle 1: the modal actually resolved (Discard dismissed it). If it is still
# up, the keystroke did not land — fail loudly rather than silently mis-assert.
MODAL_GONE=1
if xdotool search --name 'Unsaved changes' >/dev/null 2>&1; then
    MODAL_GONE=0
    note "Unsaved-changes modal still present after Discard keystroke"
    _ANY_FAIL=1
fi
# Oracle 2: it was DISCARD, not CANCEL — the document closed (no trailer window
# still titled 'discard-fixture'). Cancel would leave it open and still dirty.
DOC_CLOSED=1
if xdotool search --name 'discard-fixture' >/dev/null 2>&1; then
    DOC_CLOSED=0
    note "a 'discard-fixture' window is still open — this was Cancel, not Discard"
    _ANY_FAIL=1
fi
note "MODAL_GONE=$MODAL_GONE DOC_CLOSED=$DOC_CLOSED (both 1 => Discard, not Cancel)"

SHA_AFTER="$(sha256sum "$FIX" | awk '{print $1}')"
note "backing PDF sha256 AFTER:  $SHA_AFTER"
if [ "$SHA_AFTER" = "$SHA_BEFORE" ]; then
    printf 'INTEGRITY before=%s after=%s PASS\n' "$SHA_BEFORE" "$SHA_AFTER"
    note "INTEGRITY before=$SHA_BEFORE after=$SHA_AFTER PASS"
else
    printf 'INTEGRITY before=%s after=%s FAIL\n' "$SHA_BEFORE" "$SHA_AFTER"
    note "INTEGRITY before=$SHA_BEFORE after=$SHA_AFTER FAIL"
    _ANY_FAIL=1
fi

# Oracle 4 (NOCRASH): POSITIVE proof the app did not crash after Discard. On
# Linux/xcb the observed behaviour is a CLEAN EXIT (status 0) once Discard closes
# the last document (see header note) — so reap the launched child and assert its
# exit status is 0. A crash-after-modal would give a non-zero / signal status yet
# would still satisfy MODAL_GONE + DOC_CLOSED + unchanged-sha; this closes that
# false-pass hole. Bounded poll first so a hypothetical stayed-alive regression
# fails loudly instead of hanging on an unbounded `wait`.
NOCRASH=0
EXIT_STATUS="still-alive"
_reaped=0
for _t in 1 2 3 4 5 6 7 8; do
    if kill -0 "$_APP_PID" 2>/dev/null; then
        _ms_sleep 500
    else
        _reaped=1; break
    fi
done
if [ "$_reaped" = 1 ]; then
    wait "$_APP_PID"; EXIT_STATUS=$?
    _APP_PID=""            # reaped for its status — stop teardown re-killing a dead pid
    [ "$EXIT_STATUS" = 0 ] && NOCRASH=1
fi
if [ "$NOCRASH" = 1 ]; then
    printf 'NOCRASH exit=%s expected=0 PASS\n' "$EXIT_STATUS"
    note "NOCRASH exit=$EXIT_STATUS expected=0 PASS (clean exit after Discard, not a crash)"
else
    printf 'NOCRASH exit=%s expected=0 FAIL\n' "$EXIT_STATUS"
    note "NOCRASH exit=$EXIT_STATUS expected=0 FAIL (process crashed or did not exit cleanly)"
    _ANY_FAIL=1
fi

# --- verdict ---------------------------------------------------------------
if [ "$_ANY_FAIL" -ne 0 ]; then
    _log "path complete WITH FAILURES (see the FAIL lines above)"
    exit 1
fi
_log "path complete — Discard left the backing PDF byte-identical (#90)"
