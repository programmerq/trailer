---
id: 2026-07-20-screenshot-dialog-todo-leak
title: Take Screenshot dialog leaks a developer-facing "TODO.md" reference to end users
priority: TBD
status: open
source: UX-walkthrough driven-mode audit 2026-07-20 (Persona A worker A1-F5)
created: 2026-07-20
---

## Threshold

The Take Screenshot dialog's disabled-modes explanation contains no reference to
any internal repository file. Checkable: open Tools → Take Screenshot on a
platform where Window/Selected-Area capture is unavailable and read the note — it
explains that those modes are not available on this platform (or are coming)
without naming "TODO.md" (or any other internal artifact). Pass = the string
"TODO.md" does not appear in the dialog.

## Context

On Linux the dialog's disabled-modes note reads "Window and region capture are
tracked in **TODO.md**." A user has no TODO.md and no way to act on it; the note
exposes an internal development artifact in end-user UI. It should state the
capability is not yet available on this platform (G3-honest "why", and "where to
go" if there is a next step) without citing an internal file.

Small, self-contained string fix. The G3 non-lying-control behaviour (disabled
Window/Selected-Area modes with an explanation) is otherwise correct — only the
wording leaks. Persona B separately notes the whole capture feature could move to
disabled submenu items + tooltip (parity B2), which is a larger shape change
tracked as an observation; this item is just the string.

## Provenance

Driven against real `build/trailer` (main `6aab23f`), Xvfb+xdotool, dpr=1.
Evidence: `shipped/step-04-take-screenshot-dialog-G3.png`. Curated evidence to
commit under `docs/uat/images/2026-07-20-screenshot-dialog-todo-leak.png`.
