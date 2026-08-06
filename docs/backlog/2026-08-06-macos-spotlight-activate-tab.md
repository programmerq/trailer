---
id: 2026-08-06-macos-spotlight-activate-tab
title: macOS Spotlight offers Trailer's window as a "Tab" result whose Activate Tab action always fails
priority: TBD
status: open
source: owner HITL report 2026-08-06 (macOS), defect 2 of 2 — investigated while fixing defect 1 (already-open dedup)
created: 2026-08-06
---

## Threshold

On macOS, with `Electrical service manual.pdf` open in Trailer, typing its
name into Spotlight and choosing the **`… — Trailer / Window`** result
brings that document to the front. Concretely, checkable pass/fail on a
real Mac:

- The chosen result activates Trailer with that document visible and
  frontmost.
- **No** system notification appears. In particular not
  *The action "Activate Tab" could not run because an internal error
  occurred. (Shortcuts)*.

Closing this by making the window/tab result *not be offered at all* also
satisfies the threshold, provided the file result (below) still works —
what must not persist is a result that is offered and then fails.

## Context

Two defects came out of one owner report. Defect 1 — picking the **file**
from Spotlight opened a second window over the same file — is fixed
(already-open dedup in `Application::openFiles`, UAT-FND-053..058). This
item is defect 2, split out because it is not fixable at reasonable cost
from Qt.

**What happens.** Spotlight also surfaces the open *window* as a result
labelled `<file> — Trailer / Window`. Choosing it produces the macOS
notification *The action "Activate Tab" could not run because an internal
error occurred. (Shortcuts)* — the Shortcuts window-management action
macOS synthesises for that result cannot address the thing it is pointing
at.

**Why it can't be fixed cheaply.** Trailer's tabs are a `QTabWidget`
central widget (`src/ui/DocumentView.h`) — in-window tabs, one `NSWindow`
per `MainWindow`. macOS's "Activate Tab" addresses **native `NSWindow`
tabs** (a tab group formed by `-addTabbedWindow:ordered:`), which Trailer
has none of. Qt exposes no cross-platform API for `NSWindow` tabbing; Qt
in fact opts applications *out* of system tabbing by default
(`[NSWindow setAllowsAutomaticWindowTabbing:NO]`, qtbase change I292619).
Adopting native tabs would mean one `NSWindow` per open document plus
`tabbingIdentifier` / `addTabbedWindow:` / `newWindowForTab:` responder
wiring in Objective-C++ — i.e. replacing the shared, cross-platform
document/window model with a macOS-specific one. That is an architectural
change, not a fix, and it would put the tab model on a different footing
per OS than the one G4's platform-shape rule currently gets for free from
the shared `DocumentView`.

**Impact is already much reduced.** The defect-1 fix means the *file*
result — the one the owner reached for first, and the obvious result to
pick — now correctly surfaces the existing window instead of opening a
second copy. What remains is a secondary, redundant result that fails
noisily when chosen.

**Options when this is picked up**, cheapest first:

1. Verify on a real Mac whether the window result disappears, or starts
   working, once something else changes (OS version, the app's
   accessibility tree). This has never been reproduced off a real Mac —
   the state is unreachable from Linux/offscreen, which is why nothing was
   built speculatively here.
2. Investigate whether Qt's accessibility bridge is what advertises an
   `AXTabGroup` for the `QTabWidget`'s tab bar, and whether suppressing or
   correcting that removes the bogus result. Cheap if true; unverified.
3. Native `NSWindow` tabbing (the architectural option above). Only worth
   it if native tabs are wanted for their own sake, not to silence this.

## Provenance

Owner HITL report, 2026-08-06, macOS, alongside defect 1. Investigated
during the defect-1 fix on branch `claude/open-already-open-file`; filed
rather than built, per the instruction not to force a fix that isn't cheap
— and kept as one item rather than a second PR branch, per the
one-change-one-PR rule.
