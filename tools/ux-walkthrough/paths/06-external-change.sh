#!/usr/bin/env bash
# Golden path 6 — EXTERNAL FILE-CHANGE FLOWS (PR #89).
#
# Persona-(A) goal: when the backing file changes underneath an open document,
# Trailer must never silently lose either side. Three scenarios, each a fresh
# app + fresh backing file (teardown between phases, like path 01):
#
#   A. CLEAN auto-reload — a CLEAN (unmodified) doc whose file is overwritten on
#      disk reloads silently, Preview-style (no dialog), showing the new bytes.
#   B. DIRTY conflict banner — a DIRTY doc whose file is overwritten mid-session
#      raises the non-modal in-window amber banner:
#         "This file was changed by another program while you had unsaved edits."
#      with Reload (discard my edits) / Keep mine / Compare (disabled, G3).
#      It must NOT auto-decide (never clobber the user's edits).
#   C. DELETE underneath — deleting the file on disk keeps the buffer and shows:
#         "This file was deleted on disk. Your edits are still open — Save to recreate it."
#      with a Save button.
#
# The monitor watches the PARENT DIRECTORY (survives atomic rename) and debounces
# filesystem events by 250ms (ExternalChangeMonitor kDebounceMs), so every "poke
# the file" step is followed by a >1s settle before the screenshot.
#
# NOTE ON SELECTORS: the banner is an in-window QFrame (FileChangeBanner,
# objectName "fileChangeBanner"), NOT a separate X window — so assert_window
# cannot see it. We verify it by screenshot + note; its exact text and button
# set are fixed in src/ui/FileChangeBanner.cpp. Dirtying uses Ctrl+R (rotate),
# same resilient selector as path 04.
set -uo pipefail
export PATH_NAME="external-change"
# shellcheck source=../lib/harness.sh
source "$HARNESS_LIB"

# Two visibly different images so a reload / clobber is obvious in the pixels.
IMG_A="$RUN_DIR/backing-A.png"   # original content, opened in Trailer
IMG_B="$RUN_DIR/backing-B.png"   # the "another program wrote this" content
convert -size 560x380 gradient:green-yellow \
    -gravity center -pointsize 40 -fill black -annotate 0 "VERSION A\n(opened)" \
    "$IMG_A" 2>/dev/null || _fail "could not generate IMG_A (ImageMagick)"
# Different size + content so both mtime AND size differ (classifyExternalChange
# keys on either) and the reload is unambiguous on screen.
convert -size 620x300 gradient:red-blue \
    -gravity center -pointsize 40 -fill white -annotate 0 "VERSION B\n(on disk)" \
    "$IMG_B" 2>/dev/null || _fail "could not generate IMG_B (ImageMagick)"

# The single path Trailer watches; each phase re-seeds it from A.
BACKING="$RUN_DIR/document-under-edit.png"

# ===========================================================================
# Phase A — CLEAN doc + external change -> silent reload (Preview-style)
# ===========================================================================
cp "$IMG_A" "$BACKING"

step "clean-open-versionA" "A clean document opens showing VERSION A; window title has no unsaved marker."
launch "$BACKING"
assert_title "Trailer"
note "clean doc: no in-app edits, so an external change should reload silently with NO banner"
shot

step "clean-external-change-autoreloads" \
    "Overwriting the file on disk with VERSION B auto-reloads the view to VERSION B silently — no dialog, no banner."
cp "$IMG_B" "$BACKING"      # simulate 'another program' rewriting the file in place
_ms_sleep 1300             # > debounce (250ms) + reload
note "expect: view now shows the red/blue VERSION B; a 'Reloaded — the file changed on disk.' status flash; NO banner"
shot
teardown

# ===========================================================================
# Phase B — DIRTY doc + external change -> conflict banner (never auto-decide)
# ===========================================================================
cp "$IMG_A" "$BACKING"      # re-seed VERSION A

step "dirty-open-and-edit" \
    "Reopen VERSION A, then rotate (Ctrl+R) to make an in-app edit; title gains the '•' unsaved marker."
launch "$BACKING"
press "ctrl+r"             # action.tools.rotateRight -> pixel mutation -> dirty
_ms_sleep 400
assert_title "•"           # dirty
note "doc is now DIRTY — an external change must NOT be silently reloaded (would lose the edit)"
shot

step "dirty-external-change-shows-banner" \
    "Overwriting the file on disk now raises the amber conflict banner (Reload / Keep mine / Compare-disabled), not a silent reload."
cp "$IMG_B" "$BACKING"     # 'another program' writes while we hold unsaved edits
_ms_sleep 1300
note "expect banner text: 'This file was changed by another program while you had unsaved edits.'"
note "expect buttons: Reload (discard my edits) | Keep mine | Compare (DISABLED w/ tooltip, G3). Edit must be intact — no clobber."
shot

# Integration seam: does opening the File menu on top of the banner misbehave?
step "seam-menu-over-banner" \
    "SEAM CHECK: with the conflict banner showing, open the File menu — menu and banner must coexist cleanly."
menu "f"
note "adversarial #86-vs-#89 seam: banner (in #89) + rebuilt File menu (in #86) sharing the window at once"
shot
press "Escape"
teardown

# ===========================================================================
# Phase C — file DELETED underneath -> 'deleted on disk' banner, Save recreates
# ===========================================================================
cp "$IMG_A" "$BACKING"      # re-seed VERSION A

step "deleted-open" "Reopen VERSION A (clean) ahead of deleting its backing file."
launch "$BACKING"
assert_title "Trailer"
shot

step "deleted-underneath-shows-banner" \
    "Deleting the file on disk keeps the buffer open and shows the 'deleted on disk' banner with a Save button."
rm -f "$BACKING"           # yank the file from under the open document
_ms_sleep 1300
note "expect banner text: 'This file was deleted on disk. Your edits are still open — Save to recreate it.' with a Save button"
note "expect: the document is STILL open (buffer kept), not closed or blanked"
shot

_log "path complete"
