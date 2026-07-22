#!/usr/bin/env bash
# Golden path 11 — TOOLS -> TAKE SCREENSHOT capture + honest cancel (merged #77).
#
# Persona-(A) goal: from the empty state, reach the objectName'd capture action
# #77 wired under Tools, run a Whole-Screen capture to completion (the grab opens
# as a document), and — the owner's taste rule — confirm that CANCELLING the
# capture dialog just returns the app to its prior state with NO narration /
# confirmation popup ("no popup that just says something happened").
#
# TOOLS vs FILE (verified against merged code): there are two capture entry
# points post-#86. This path drives Tools -> "&Take Screenshot"
# (objectName action.tools.takeScreenshot, src/ui/MainWindow.cpp onTakeScreenshot)
# which opens the capture-MODE DIALOG (whole-screen / window / region), then
# routes to Application::captureScreenshot. The File -> Screenshot direct submenu
# is a different, dialogless surface driven by path 09. Reaching Tools -> Take
# Screenshot by mnemonic (Alt+T, then T) is stable across the #86 File-menu IA
# rename and needs no pixel geometry (the action carries no shortcut — #86
# stripped the OS-reserved dead binding as a lying control).
#
# THE ORACLE is hard/machine-checkable (path-10 style):
#   SHOT-A  — after accepting Whole Screen, the captured screen opens as a NEW
#             document. On Linux the grab is a transient import opened with
#             markUntitled=true (src/app/Application.cpp captureScreenshot #else
#             branch -> openFiles(path, markUntitled=true)), so the reused launch
#             window's title flips from the bare empty "Trailer" to
#             "Untitled — Trailer" — the honest observable that capture ran
#             end-to-end under Xvfb (grabWindow -> save -> openFiles). PASS iff an
#             "Untitled" document window exists after accepting.
#   CANCEL-B — reopening the dialog and pressing Escape leaves the trailer-class
#             app window count UNCHANGED (no orphaned/extra window) ...
#   CANCEL-B-no-dialog — ... and leaves NO lingering "Take Screenshot" dialog and
#             NO narration/confirmation popup behind (title-allowlist grep). This
#             is the owner's no-narration-dialogs rule made checkable.
#   CANCEL-B-no-newwin — ... and, class-independent, adds NO new TOP-LEVEL window
#             beyond the pre-dialog baseline. The allowlist grep only knows the
#             titles it lists; this delta catches an off-list narration popup
#             ("Information"/"Success"/…) the grep would miss.
#
# HONEST PLATFORM BOUNDARIES:
#   * Window/region capture are disabled on Linux (a G3 non-lying-control state
#     with an explanatory note in the dialog); only whole-screen runs here.
#   * The macOS native TCC "Screen Recording" permission prompt is real-Mac only
#     and routed to the owner checklist. Under Xvfb there is no OS permission
#     gate, so the whole app-side flow runs to completion.
set -uo pipefail
export PATH_NAME="tools-screenshot-capture"
# shellcheck source=../lib/harness.sh
source "$HARNESS_LIB"

# The Linux capture backend writes the grab to a temp path (always present) and,
# on some configurations, PicturesLocation. A bare container has no XDG user
# dirs, so ensure ~/Pictures exists for parity with paths 02/09 — honest
# environment normalisation, not faking the feature (the grab -> save -> openFiles
# chain genuinely runs headless under Xvfb).
mkdir -p "$HOME/Pictures"

# --- local oracle helpers (path-10 hard-oracle style) ----------------------
# Count top-level app windows by WM_CLASS. Only meaningful when NO modal dialog
# is open (the Take Screenshot dialog shares the app WM_CLASS), so every call
# site below counts AFTER any dialog has closed and the UI settled.
win_count() { xdotool search --class 'trailer' 2>/dev/null | wc -l | tr -d ' '; }

# Count ALL top-level X windows currently present (every named window in the
# tree), regardless of WM_CLASS. win_count() only sees WM_CLASS 'trailer', so a
# narration/confirmation popup with a DIFFERENT class — or any window whose title
# isn't on the CANCEL allowlist grep below — would slip past both. This total
# count backstops that: a clean cancel opens NO window, so ANY increase over the
# pre-dialog baseline means something leaked. Compared before-open vs after-Escape
# at settled states, so constant WM/root windows cancel out of the delta.
toplevel_count() { xdotool search --name '.*' 2>/dev/null | wc -l | tr -d ' '; }

_ANY_FAIL=0
# Emit a machine-checkable count assertion, e.g.  CANCEL-B before=1 after=1 ... PASS
assert_counts() {  # tag before after expected_after
    local tag="$1" before="$2" after="$3" want="$4" verdict
    if [ "$after" = "$want" ]; then verdict="PASS"; else verdict="FAIL"; _ANY_FAIL=1; fi
    printf '%s before=%s after=%s expected_after=%s %s\n' "$tag" "$before" "$after" "$want" "$verdict"
    note "$tag before=$before after=$after expected_after=$want $verdict"
}
# Emit a machine-checkable boolean assertion, e.g.  SHOT-A capture_doc=1 expected=1 PASS
assert_bool() {  # tag field actual expected
    local tag="$1" field="$2" actual="$3" want="$4" verdict
    if [ "$actual" = "$want" ]; then verdict="PASS"; else verdict="FAIL"; _ANY_FAIL=1; fi
    printf '%s %s=%s expected=%s %s\n' "$tag" "$field" "$actual" "$want" "$verdict"
    note "$tag $field=$actual expected=$want $verdict"
}

# ===========================================================================
# SCENARIO A — complete: Tools -> Take Screenshot -> Whole Screen opens a doc.
# ===========================================================================
step "empty-state" "App open, no document — the state a user invokes Take Screenshot from."
launch                 # no file arg -> empty launch window
BASE="$(win_count)"
note "empty launch: app-window count=$BASE"
assert_title "Trailer"
shot

step "capture-dialog" \
    "Tools -> Take Screenshot opens the capture-mode dialog (Whole Screen default; window/region disabled on Linux)."
menupick t t           # Tools -> &Take Screenshot (objectName action.tools.takeScreenshot)
assert_window "Take Screenshot"
note "Whole Screen radio is checked by default; window/region are disabled non-lying controls on Linux"
shot

step "capture-completes" \
    "Accepting Whole Screen (Return) captures the X screen and opens it as a new (untitled) document."
press "Return"         # accept the default (Whole Screen) -> grabWindow -> save -> openFiles(markUntitled)
_ms_sleep 1400
# On Linux the captured screen opens as a transient import (markUntitled=true),
# so the reused launch window's title becomes "Untitled — Trailer". Detect that
# untitled document window (the empty state is bare "Trailer", so "Untitled"
# unambiguously means a captured doc opened).
_shot_win="$(xdotool search --name 'Untitled' 2>/dev/null | head -1)"
CAPTURE_DOC=0
if [ -n "$_shot_win" ]; then
    CAPTURE_DOC=1
    _WIN_ID="$_shot_win"
    assert_window "Untitled"
    note "capture ran end-to-end: the grabbed screen opened as a new untitled document (no narration dialog in between)"
fi
shot
assert_bool "SHOT-A" "capture_doc" "$CAPTURE_DOC" 1

# Distinct curated evidence: grab the Untitled capture DOCUMENT window itself
# (window-scoped via `import -window`, not the whole root) so the committed
# "capture completed" frame clearly shows the screenshot opened AS a document and
# is byte-DISTINCT from the full-root cancel frame. A root grab is unreliable
# here: with no compositor under Xvfb the just-closed capture dialog leaves GHOST
# pixels on the root, which made the completed and cancel root-grabs byte-
# identical (guardian review finding). The window grab captures only the Untitled
# doc's live pixels, so it never contains that ghost.
if [ "$CAPTURE_DOC" = 1 ] && [ -n "${_WIN_ID:-}" ]; then
    if import -silent -window "$_WIN_ID" "$RUN_DIR/${_CUR_LABEL}-untitled-window.png" 2>/dev/null; then
        note "window-scoped evidence grab -> ${_CUR_LABEL}-untitled-window.png (the Untitled capture document, ghost-free)"
    fi
fi

# ===========================================================================
# SCENARIO B — cancel = NO narration dialog: reopen, Escape, return to prior
# state with no lingering dialog and no confirmation popup.
# ===========================================================================
step "cancel-reopen-dialog" \
    "Reopen Tools -> Take Screenshot; the capture dialog appears again."
B_BEFORE="$(win_count)"
TL_BEFORE="$(toplevel_count)"       # baseline of ALL top-level windows before the dialog opens
note "pre-cancel app-window count=$B_BEFORE top-level count=$TL_BEFORE"
menupick t t
assert_window "Take Screenshot"
shot

step "cancel-no-dialog" \
    "Escape cancels the capture; the app returns to its prior state with NO narration/confirmation popup and no lingering dialog."
press "Escape"
_ms_sleep 700
B_AFTER="$(win_count)"
TL_AFTER="$(toplevel_count)"
# The capture dialog must be gone, and NOTHING may have popped up in its place —
# no "Screenshot saved", no narration/confirmation window. Both the dialog title
# and any narration-ish title count as a lingering popup.
LINGER=0
if xdotool search --name 'Take Screenshot' >/dev/null 2>&1; then LINGER=1; fi
if xdotool search --name '[Nn]arrat|[Cc]onfirm|[Ss]creenshot saved|[Ss]aved to' >/dev/null 2>&1; then LINGER=1; fi
# Window-count-delta backstop: independent of the title allowlist, assert NO new
# top-level window survived the cancel beyond the pre-dialog baseline. An
# off-list narration popup ("Information"/"Success"/…) that the greps above would
# miss still shows up as an extra top-level window here.
NEWWIN=$((TL_AFTER - TL_BEFORE))
[ "$NEWWIN" -lt 0 ] && NEWWIN=0     # fewer windows is fine; only NEW ones fail
note "post-cancel: dialog dismissed and no narration/confirmation popup (owner no-narration-dialogs rule); top-level before=$TL_BEFORE after=$TL_AFTER"
shot
assert_counts "CANCEL-B" "$B_BEFORE" "$B_AFTER" "$B_BEFORE"
assert_bool "CANCEL-B-no-dialog" "lingering_popups" "$LINGER" 0
assert_bool "CANCEL-B-no-newwin" "new_toplevels" "$NEWWIN" 0

# --- verdict ---------------------------------------------------------------
if [ "$_ANY_FAIL" -ne 0 ]; then
    _log "path complete WITH FAILURES (see the *_ FAIL lines above)"
    exit 1
fi
_log "path complete — Tools -> Take Screenshot capture completes and cancel raises no narration dialog"
