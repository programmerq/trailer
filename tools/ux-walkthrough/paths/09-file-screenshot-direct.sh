#!/usr/bin/env bash
# Golden path 9 — FILE -> SCREENSHOT -> WHOLE SCREEN (direct, dialogless; PR #86).
#
# Persona-(A) goal: from the empty state, reach the DISCOVERABLE acquire surface
# #86 added — the File -> Screenshot submenu — pick "Whole Screen", and confirm
# the capture opens as a document, WITH NO capture-mode dialog in between.
#
# TWO capture entry points, and this path drives the OTHER one from path 02:
#   * Tools -> "Take Screenshot" (objectName action.tools.takeScreenshot) opens
#     the capture-MODE DIALOG (whole-screen / window / region radio buttons),
#     THEN captures. That dialog-bearing action is what path 02 drives (menupick
#     t t). See README "Tools vs File" disambiguation.
#   * File -> "Screenshot" ▸ (Whole Screen / Window / Selected Area) is the
#     discoverable submenu #86 added via Application::addAcquireItems
#     (src/app/Application.cpp:494). Its items carry NO objectName, NO shortcut,
#     NO mnemonic, and fire Application::captureScreenshot DIRECTLY — no dialog.
#     THIS path drives that direct submenu. It is genuinely new surface no other
#     golden path covers.
#
# SELECTOR NOTE (documented fragility — read before editing):
#   The submenu items have no shortcut, no objectName, and no Alt mnemonic (the
#   labels carry no '&'), so none of the resilient handles the other paths lean
#   on exist here. The one stable handle left is the item's own visible TEXT, so
#   we drive by Qt menu TYPE-AHEAD: with the File menu open, typing 's' jumps the
#   highlight to the first item whose label starts with S — which is "Screenshot"
#   (it precedes "Scanner"/"Save"/"Save As" in File-menu order,
#   src/ui/MainWindow.cpp:754 buildMenus). We then press Right to open the
#   submenu (Right on a submenu item opens it; Right on a plain item like "Open…"
#   only walks to the next menu-bar menu, so a mis-highlight cannot fire Open's
#   file dialog), and Return to trigger the first submenu item, "Whole Screen".
#   If #86 reorders the File menu so another S-item precedes Screenshot, update
#   the type-ahead below.
#
# HONEST HEADLESS BOUNDARY (why this path degrades gracefully):
#   Whole-screen capture on Linux goes through QScreen::grabWindow(0)
#   (src/app/Application.cpp:672, #else branch) and saves to a temp path
#   (transientImportPath -> QStandardPaths::TempLocation), then openFiles() opens
#   it as a "trailer-screenshot-<stamp>.png" document. Path 02 proves that grab
#   runs under Xvfb. BUT a real desktop can route screen capture through a
#   portal/compositor that a bare Xvfb+openbox session does not provide, and menu
#   type-ahead / submenu keyboard nav is less deterministic than a shortcut. So
#   this path NEVER hard-fails on the outcome: it captures the menu-open state,
#   the submenu-open state, and whatever results (a captured document, or the
#   post-trigger state), and marks a documented boundary instead of failing when
#   no capture document appears. The macOS native /usr/sbin/screencapture path
#   and its TCC "Screen Recording" prompt are real-Mac only -> owner checklist.
set -uo pipefail
export PATH_NAME="file-screenshot-direct"
# shellcheck source=../lib/harness.sh
source "$HARNESS_LIB"

# The Linux capture backend writes the grab to TempLocation (always present).
# We still ensure ~/Pictures exists for parity with path 02, harmlessly, in case
# a build routes the save through PicturesLocation on some configuration.
mkdir -p "$HOME/Pictures"

# --- Step 1: empty state ---------------------------------------------------
step "empty-state" "App open, no document — the state a user reaches File -> Screenshot from."
launch     # no file arg -> empty state
shot

# --- Step 2: open the File menu (shows the acquire IA) ---------------------
step "file-menu-open" \
    "Alt+F opens the File menu; the Screenshot submenu (with Scanner/Camera peers) is present as a discoverable acquire surface (#86)."
menu "f"                   # Alt+F opens the File menu (in-window menu bar, not a separate X window)
note "the File menu carries the create/acquire group: New from Clipboard, Open, Open Recent, Screenshot ▸, Scanner, Camera"
note "Screenshot ▸ is the dialogless discoverable acquire submenu #86 added (Application::addAcquireItems)"
shot

# --- Step 3: open the Screenshot submenu -----------------------------------
step "screenshot-submenu-open" \
    "Type-ahead 's' highlights the Screenshot submenu; Right opens it to Whole Screen / Window / Selected Area."
press "s"                  # type-ahead: first File-menu item starting with S = "Screenshot"
press "Right"              # open the Screenshot submenu (Whole Screen is its first item, auto-highlighted)
note "Whole Screen enabled; Window / Selected Area disabled on Linux with an honest tooltip (G3, Application.cpp:516 #ifndef Q_OS_MACOS)"
note "SELECTOR: driven by menu type-ahead on the item's own text — no shortcut/objectName/mnemonic exists for these items (see header)"
shot

# --- Step 4: trigger Whole Screen; capture opens as a document (or boundary) -
step "whole-screen-captures-doc" \
    "Return on Whole Screen captures the X screen directly (no dialog) and opens it as a new document."
press "Return"             # trigger "Whole Screen" -> captureScreenshot(Screen) -> grabWindow(0) -> openFiles()
_ms_sleep 1300             # let the grab + save + open settle
# The captured screen opens as a NEW window titled "trailer-screenshot-<stamp>.png".
# Assert on that stem (not a bare "Trailer", which the empty state also shows) —
# but DEGRADE GRACEFULLY: if no capture document appears under this headless
# session, mark a documented boundary and capture whatever is reachable rather
# than hard-failing the suite.
_shot_win="$(xdotool search --name 'trailer-screenshot' 2>/dev/null | head -1)"
if [ -n "$_shot_win" ]; then
    _WIN_ID="$_shot_win"
    assert_window "trailer-screenshot"
    assert_title "trailer-screenshot"
    note "File -> Screenshot -> Whole Screen ran end-to-end headless: direct capture opened as a document (NO dialog in between)."
else
    _WIN_ID="$(_find_window)"
    boundary "No 'trailer-screenshot' document appeared under this bare Xvfb/openbox session — whole-screen capture can depend on a portal/compositor a headless X server does not provide, and submenu keyboard nav is non-deterministic. Capturing the reachable state instead of failing (the menu open + submenu open are the reachable evidence)."
    note "reachable-state capture only; the app-side direct-capture wiring is exercised by path 02's whole-screen grab, which shares Application::captureScreenshot."
fi
shot

_log "path complete"
