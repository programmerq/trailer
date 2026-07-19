# shellcheck shell=bash
# ---------------------------------------------------------------------------
# ux-walkthrough drive harness — Tier-1 Linux/Xvfb step DSL.
#
# This file is SOURCED by the golden-path scripts in ../paths/*.sh. It is not
# executable on its own. It provides the small step DSL those scripts speak:
#
#   launch [args...]        launch the real trailer binary under the current
#                           X display, wait for its top-level window
#   step "label" "expect"   begin a numbered step; records label + expected
#                           effect for the judge persona
#   shot                    capture the whole X screen (root window, so modal
#                           dialogs and menus are included) for the current step
#   press <xdotool-keys>    send a real X key chord to the app window
#   type  <text>            type real X characters into the app window
#   menu  <alt-mnemonic>    open a menu bar entry by its mnemonic (Alt+F, …)
#   activate                raise + focus the trailer window
#   assert_window <regex>   fail unless an X window whose name matches exists
#   assert_title  <regex>   fail unless the trailer window title matches
#   note "text"             append a free-text line to the current step bundle
#   boundary "text"         mark a step as a documented platform boundary
#                           (the OS-capture / macOS-only edge) — not a failure
#   teardown                kill the app (also runs on EXIT)
#
# DESIGN NOTES
#  * Selectors are RESILIENT ON PURPOSE. Every golden path drives the app
#    through keyboard SHORTCUTS (Ctrl+O, Ctrl+=, Ctrl+W, …), never pixel
#    coordinates and never menu-item label TEXT. Shortcuts are stable across the
#    File-menu information-architecture rename in PR #86, which moves/renames
#    menu items but preserves their QKeySequence bindings. Where a widget needs
#    a stable handle, the corresponding QAction/QWidget carries a
#    setObjectName() (see src/ui/MainWindow.cpp) so a future AT-SPI / QTest tier
#    can target it by name rather than by geometry. The menu() verb below drives
#    a menu by its Alt mnemonic and exists for future menu-walking, but no
#    golden path currently uses it — the guarantee rests on shortcuts +
#    objectNames.
#  * We use a REAL X server (Xvfb), NOT QT_QPA_PLATFORM=offscreen. The whole
#    point of Tier 1 is real window-manager / menu / focus / modal behaviour —
#    e.g. the unsaved-changes close prompt is deliberately SKIPPED under the
#    offscreen/minimal platforms (src/ui/MainWindow.cpp closeEvent) and only
#    fires on xcb, which is exactly the "vanishing-modal" class this tier
#    exists to exercise.
#
# Each step emits, into $RUN_DIR:
#   NN-<label>.png    the screenshot
#   NN-<label>.txt    a metadata bundle: step label, expected effect, notes,
#                     and window inventory at capture time
# The per-step bundle is the shape the ux-walkthrough persona (A) consumes.
# ---------------------------------------------------------------------------

set -uo pipefail

# --- configuration (overridable from the environment) ----------------------
: "${TRAILER_BIN:?TRAILER_BIN must point at the built trailer binary}"
: "${RUN_DIR:?RUN_DIR must be set to the per-run artifact directory}"
: "${PATH_NAME:=unknown}"          # golden-path slug, set by each path script
: "${SETTLE_MS:=700}"              # pause after each input for the UI to settle
: "${WINDOW_TIMEOUT:=15}"          # seconds to wait for the app window

# Force the real X11 platform plugin, UNCONDITIONALLY — Tier-1's entire point
# is real window-manager / focus / modal behaviour. We override any inherited
# QT_QPA_PLATFORM (CI commonly exports "offscreen", under which Qt maps no real
# X window — so xdotool can't find it — AND the unsaved-changes close prompt is
# deliberately skipped, src/ui/MainWindow.cpp closeEvent). Set UXW_PLATFORM to
# override for debugging only.
export QT_QPA_PLATFORM="${UXW_PLATFORM:-xcb}"

_STEP=0
_APP_PID=""
_WIN_ID=""

mkdir -p "$RUN_DIR"

_ms_sleep() { perl -e "select(undef,undef,undef,$1/1000)" 2>/dev/null || sleep "$(awk "BEGIN{print $1/1000}")"; }
_settle()   { _ms_sleep "$SETTLE_MS"; }

_log() { printf '[ux-walkthrough:%s] %s\n' "$PATH_NAME" "$*" >&2; }
_fail() { printf '[ux-walkthrough:%s] FAIL: %s\n' "$PATH_NAME" "$*" >&2; teardown; exit 1; }

# Find the trailer top-level window id. Prefer the WM_CLASS ("trailer"/"Trailer"
# — the app sets its class from argv[0]); fall back to the app PID.
_find_window() {
    local id
    id="$(xdotool search --class 'trailer' 2>/dev/null | head -1)"
    if [ -z "$id" ] && [ -n "$_APP_PID" ]; then
        id="$(xdotool search --pid "$_APP_PID" 2>/dev/null | head -1)"
    fi
    printf '%s' "$id"
}

# Launch the real binary and wait for its first window to map.
launch() {
    _log "launching: $TRAILER_BIN $*"
    "$TRAILER_BIN" "$@" >"$RUN_DIR/app.stdout.log" 2>"$RUN_DIR/app.stderr.log" &
    _APP_PID=$!
    local waited=0
    while [ "$waited" -lt "$((WINDOW_TIMEOUT * 2))" ]; do
        if ! kill -0 "$_APP_PID" 2>/dev/null; then
            _fail "app exited before mapping a window (see app.stderr.log)"
        fi
        _WIN_ID="$(_find_window)"
        [ -n "$_WIN_ID" ] && break
        _ms_sleep 500
        waited=$((waited + 1))
    done
    [ -n "$_WIN_ID" ] || _fail "no top-level window appeared within ${WINDOW_TIMEOUT}s"
    _log "window id: $_WIN_ID"
    activate
    _settle
}

# Raise + focus the app window so key/type events land on it.
activate() {
    [ -n "$_WIN_ID" ] || _WIN_ID="$(_find_window)"
    if [ -n "$_WIN_ID" ]; then
        xdotool windowactivate --sync "$_WIN_ID" 2>/dev/null || xdotool windowactivate "$_WIN_ID" 2>/dev/null || true
        xdotool windowraise "$_WIN_ID" 2>/dev/null || true
    fi
}

# Send a key chord (xdotool syntax, e.g. "ctrl+plus", "ctrl+shift+3", "Return").
press() {
    activate
    xdotool key --clearmodifiers "$1"
    _settle
}

# Type literal characters into the focused window.
type() {
    activate
    xdotool type --clearmodifiers -- "$1"
    _settle
}

# Open a menu-bar menu by its Alt mnemonic (e.g. "f" for &File, "t" for &Tools).
menu() {
    activate
    xdotool key --clearmodifiers "alt+$1"
    _settle
}

# Begin a numbered step. Records the label + expected effect that the judge
# persona measures the screenshot against.
step() {
    _STEP=$((_STEP + 1))
    _CUR_LABEL="$(printf '%02d-%s' "$_STEP" "$(echo "$1" | tr ' /' '--')")"
    _CUR_EXPECT="${2:-}"
    {
        echo "path:      $PATH_NAME"
        echo "step:      $_STEP"
        echo "label:     $1"
        echo "expected:  $_CUR_EXPECT"
    } >"$RUN_DIR/$_CUR_LABEL.txt"
    _log "step $_STEP: $1"
}

# Append a free-text observation to the current step bundle.
note() { echo "note:      $*" >>"$RUN_DIR/$_CUR_LABEL.txt"; _log "  note: $*"; }

# Mark the current step as a documented platform boundary (honest edge, not a
# failure): e.g. an OS capture prompt or a macOS-only action absent on Linux.
boundary() { echo "boundary:  $*" >>"$RUN_DIR/$_CUR_LABEL.txt"; _log "  BOUNDARY: $*"; }

# Capture the whole X screen (root window) so modal dialogs / open menus are
# included, and record the window inventory alongside it.
shot() {
    [ -n "${_CUR_LABEL:-}" ] || _fail "shot called before step"
    _ms_sleep 250
    if import -silent -window root "$RUN_DIR/$_CUR_LABEL.png" 2>/dev/null; then
        :
    else
        # Fallback path if ImageMagick's X import is unavailable.
        xwd -root -silent 2>/dev/null | convert xwd:- "$RUN_DIR/$_CUR_LABEL.png" 2>/dev/null \
            || _fail "screenshot capture failed (import and xwd both failed)"
    fi
    {
        echo "screenshot: $_CUR_LABEL.png"
        echo "windows:"
        xdotool search --name '.*' 2>/dev/null | while read -r wid; do
            local wname
            wname="$(xdotool getwindowname "$wid" 2>/dev/null)"
            [ -n "$wname" ] && echo "  - [$wid] $wname"
        done
    } >>"$RUN_DIR/$_CUR_LABEL.txt"
    _log "  shot -> $_CUR_LABEL.png"
}

# Fail unless an X window whose name matches <regex> currently exists.
assert_window() {
    if xdotool search --name "$1" >/dev/null 2>&1; then
        note "assert_window OK: /$1/"
    else
        _fail "assert_window: no window matching /$1/"
    fi
}

# Fail unless the trailer window's title matches <regex>.
assert_title() {
    local title
    title="$(xdotool getwindowname "$_WIN_ID" 2>/dev/null)"
    if echo "$title" | grep -Eq "$1"; then
        note "assert_title OK: '$title' matches /$1/"
    else
        _fail "assert_title: window title '$title' does not match /$1/"
    fi
}

teardown() {
    if [ -n "$_APP_PID" ] && kill -0 "$_APP_PID" 2>/dev/null; then
        kill "$_APP_PID" 2>/dev/null || true
        _ms_sleep 400
        kill -9 "$_APP_PID" 2>/dev/null || true
    fi
    _APP_PID=""
}

trap teardown EXIT
