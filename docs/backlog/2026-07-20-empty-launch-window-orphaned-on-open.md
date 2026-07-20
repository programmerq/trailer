---
id: 2026-07-20-empty-launch-window-orphaned-on-open
title: New-from-Clipboard (and any Open) leaves the empty launch window orphaned beside the new document window
priority: TBD
status: open
source: UX-walkthrough driven-mode audit 2026-07-20 (Persona A worker A2-F2)
created: 2026-07-20
---

## Threshold

On Windows/Linux, when the app is showing the empty "Open a file" launch window
and the user opens the first document (New from Clipboard, File → Open, or drop),
that first document adopts/reuses the empty launch window rather than spawning a
sibling. Checkable: launch with no file, trigger New from Clipboard, then
enumerate top-level windows — there is exactly **one** window (the document), not
a document window plus a leftover empty-state window. (Opening a *second*
document while one is already open may still use a new window per the existing
new-window-on-open policy; this item is specifically about the first-open case
adopting the pre-existing empty launch window.)

## Context

The empty launch window is not reused: `Application::openFiles` →
`ensureFreshWindow` honors the default `OpenFilesIn::NewWindow` and opens the
image in a new window, never adopting the existing empty-state window, so the user
is left with a document window **plus** an orphaned empty "Trailer" window (the
window inventory in `menu-ext/06-file-menu-with-doc.txt` lists both). This is
app-wide (File → Open does the same), but #86 makes it the first thing a
paste-user hits on the advertised hottest path.

On macOS this does not arise (empty state = no window, G5). The fix is
Win/Linux-specific: the persistent empty launch window should adopt the first
opened document instead of spawning a sibling. The catalog notes multi-window /
new-window-on-open "appears already implemented; no open backlog item" (TODO.md
~L172, L589) — the empty-window-adopt-first-doc case is uncovered.

## Provenance

Driven against real `build/trailer` (main `6aab23f`), Xvfb+xdotool, dpr=1.
Evidence: `menu-ext/06-file-menu-with-doc.png` and its `.txt` window inventory.
Curated evidence to commit under
`docs/uat/images/2026-07-20-empty-window-orphaned.png`.
