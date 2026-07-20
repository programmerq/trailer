#!/usr/bin/env bash
# Golden path 5 — FILE-MENU IA + NEW-FROM-CLIPBOARD (PR #86).
#
# Persona-(A) goals, all now reachable on Linux (the create/acquire group is no
# longer #ifdef Q_OS_MACOS — MainWindow::buildMenus calls
# Application::addNewFromClipboardAction + addAcquireItems on every platform):
#
#   1. The File menu carries the create/acquire group — New from Clipboard,
#      Screenshot ▸ (Whole Screen / Window / Selected Area), Scanner, Camera —
#      both with NO document open and WITH a document open (the "vanish once a
#      window is key" regression #86 fixes). Scanner/Camera are present but
#      disabled with an honest tooltip (G3); Window/Selected-Area capture are
#      disabled on Linux (QScreen fallback only does whole-screen).
#   2. ⌘N / Ctrl+N == New from Clipboard. With an IMAGE on the clipboard the
#      item is enabled and opens the image as a document.
#   3. NON-IMAGE (plain text, not a file path) on the clipboard: the item is
#      disabled and Ctrl+N is a SILENT no-op — NO dialog narrating the no-op
#      (PHILOSOPHY → "No popup that just says no"). This is the owner taste rule.
#
# Resilient selectors: the File menu is opened by its Alt+F mnemonic (menu "f");
# New from Clipboard is triggered by its QKeySequence::New shortcut (Ctrl+N),
# which survives the IA rename. We never target by pixel or label text.
set -uo pipefail
export PATH_NAME="menu-ia"
# shellcheck source=../lib/harness.sh
source "$HARNESS_LIB"

# A recognisable image so the judge can confirm THIS image opened.
CLIP_IMG="$RUN_DIR/clip-image.png"
convert -size 480x320 gradient:teal-navy \
    -gravity center -pointsize 34 -fill white -annotate 0 "CLIPBOARD\nIMAGE\n480x320" \
    "$CLIP_IMG" 2>/dev/null || _fail "could not generate clipboard image fixture (ImageMagick)"

# --- Step 1: File menu IA with NO document open ----------------------------
step "file-menu-no-doc" \
    "File menu (empty state) shows New from Clipboard (disabled — clipboard empty), Open, Open Recent, Screenshot submenu, Scanner (disabled), Camera (disabled)."
launch                      # no file arg -> empty state
menu "f"                    # Alt+F opens the File menu; aboutToShow re-checks the clipboard
note "clipboard not yet primed -> New from Clipboard should be DISABLED with the 'Copy an image or a file…' tooltip"
note "acquire group: Screenshot ▸ present; Scanner + Camera present-but-disabled (G3 honest tooltip)"
shot
press "Escape"             # close the menu

# --- Step 2: NON-IMAGE clipboard -> item stays disabled --------------------
# (One shot per step so nothing is overwritten — the harness keys the PNG off
# the step label.)
step "text-clipboard-menu-disabled" \
    "With PLAIN TEXT (not a file path) on the clipboard, the File menu shows New from Clipboard STILL disabled."
printf 'this is just some text, not an image and not a file path' \
    | xclip -selection clipboard -i 2>/dev/null
_ms_sleep 500              # let QClipboard::dataChanged reach refreshClipboardActions
menu "f"                   # open File menu so aboutToShow re-checks; item must be DISABLED
note "expect New from Clipboard STILL disabled — text is neither an image nor an existing file path (inspectClipboard)"
shot
press "Escape"

# --- Step 3: NON-IMAGE clipboard -> Ctrl+N is a SILENT no-op ---------------
step "text-clipboard-silent-noop" \
    "Ctrl+N with text on the clipboard does nothing and shows NO dialog — silent no-op (owner taste rule)."
press "ctrl+n"             # trigger via the shortcut; disabled action must NOT fire, and no popup may appear
_ms_sleep 700
note "expect: still the empty state, NO 'clipboard is empty' dialog, NO new window (PHILOSOPHY → No popup that just says no)"
shot

# --- Step 4: IMAGE clipboard -> menu item becomes enabled ------------------
step "image-clipboard-menu-enabled" \
    "With an IMAGE on the clipboard, the File menu shows New from Clipboard ENABLED."
xclip -selection clipboard -t image/png -i "$CLIP_IMG" 2>/dev/null
_ms_sleep 500
CLIP_BYTES="$(xclip -selection clipboard -t image/png -o 2>/dev/null | wc -c)"
[ "$CLIP_BYTES" -gt 0 ] || _fail "clipboard did not accept the image"
note "clipboard holds image/png: ${CLIP_BYTES} bytes"
menu "f"                   # aboutToShow -> New from Clipboard should now be ENABLED
note "expect New from Clipboard now ENABLED (clipboard has an image)"
shot
press "Escape"

# --- Step 5: Ctrl+N opens the clipboard image as a document ----------------
step "image-clipboard-opens-doc" \
    "Ctrl+N (New from Clipboard) opens the clipboard image as a document."
press "ctrl+n"             # New from Clipboard: clip image -> temp PNG -> openFiles()
_ms_sleep 900
_WIN_ID="$(_find_window)"
note "oracle: a document window now shows the teal/navy CLIPBOARD IMAGE fixture"
note "SEAM WATCH: default OpenFilesIn::NewWindow spawns a fresh window; note whether the empty launch window is left orphaned"
shot

# --- Step 6: create/acquire group STILL present WITH a document open -------
step "file-menu-with-doc" \
    "With a document open, the File menu STILL carries New from Clipboard + Screenshot ▸ + Scanner/Camera (the #86 anti-vanish fix)."
menu "f"
note "regression check for #86 / finding #4: the create/acquire group must NOT vanish now that a window is key"
shot
press "Escape"

_log "path complete"
