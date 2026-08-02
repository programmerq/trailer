# Trailer — Platform Conventions

> The concrete, checkable per-OS command-surface reference. This is the
> target gate **G4** (*Platform-native shape, no feature dropped*) in
> [`../AGENTS.md`](../AGENTS.md) checks a PR against: adapt a feature's
> *shape* to each OS's native command surface — macOS global menu bar,
> Windows in-window menu bar + accelerators, GNOME/Linux header bar — but
> never drop the feature on one OS because that OS shapes it differently.
> The shape adapts per platform; the feature set does **not**. See
> [`../PHILOSOPHY.md`](../PHILOSOPHY.md) → *Platform-native per OS; adapt
> the shape, never drop the feature* and DESIGN §2.1 goal 3 (cross-platform
> parity) and §5.4 (platform conventions).

Every rule below is phrased so an agent can check the built app pass/fail
against it. Where a rule restates a policy owned by another doc
(never-worry-save, Select-All semantics), it cross-links rather than
duplicating — the linked doc stays the source of truth. The keyboard table
is reconciled against the live `setShortcut` calls surfaced in DESIGN §7;
that section stays the shipped-vs-planned source of truth, this file is the
per-OS mapping and rationale.

---

## 1. Command-surface placement per OS

The command surface is *where* commands live. It differs by platform; the
requirement is that every command is reachable on all three, in that
platform's native place.

### macOS — global menu bar

| Rule (checkable) | Source |
|---|---|
| A **global menu bar** at the top of the screen is the primary command surface; every command Trailer offers is present there, even ones also reachable from a toolbar or context menu. | Apple HIG — The menu bar; Designing for macOS |
| The **set of top-level menus never changes** at runtime — menus and items are not added or removed dynamically. | Apple HIG — The menu bar |
| A command that cannot act in the current context is **dimmed (disabled), not removed**; if every item in a menu is unavailable, the menu itself stays open-able so the user can see what it offers. | Apple HIG — Menus; The menu bar |
| Because the menu bar is hidden in full-screen, **every command is also reachable through the app's own UI** (toolbar / context menu / shortcut). | Apple HIG — The menu bar |
| Menu-item labels use **title-style capitalization**, are **verb phrases** for actions, **omit articles**, and **append `…` only** when the item needs more input before it can complete. | Apple HIG — Menus |

This is Trailer's realisation of PHILOSOPHY → *A popup is a last resort*
(disable + tooltip) and gate **G3** (no lying controls) on macOS: the
disabled-not-removed rule is the macOS shape of G3.

### Windows — in-window menu bar + command bar + accelerators

| Rule (checkable) | Source |
|---|---|
| Commands live in an **in-window menu bar / command bar**; a document editor like Trailer offers a **full menu bar** (a command bar alone is for simpler apps). | Microsoft — Commanding basics |
| **Context (right-click) menus** carry contextual/space-saving commands on the canvas. | Microsoft — Commanding basics |
| **Keyboard accelerators are shown in the menu-item labels and in tooltips** so they are discoverable, and are **exposed to screen readers** (AcceleratorKey / equivalent). | Microsoft — Keyboard accelerators |
| A **disabled control disables its accelerator** — an unavailable command does not fire from its shortcut. | Microsoft — Keyboard accelerators |
| A control's **built-in accelerator is not overridden** (e.g. Ctrl+C inside a text box keeps its meaning). | Microsoft — Keyboard accelerators |

### GNOME / Linux — header bar + menu button

| Rule (checkable) | Source |
|---|---|
| Primary actions live in a **header bar**; there is **no classic global menu bar**. | GNOME HIG — Design principles |
| Secondary / overflow commands live behind a **menu button** in the header bar. | GNOME HIG — Design principles |
| *(Guidance, not a G4 gate row.)* Frequent actions are close at hand (header bar); rare actions are pushed further away (menu button / dialogs). | GNOME HIG — Design principles |

DESIGN §5.4 notes the Linux menu bar may sit inside the window or at the top
depending on the desktop environment; the header-bar shape above is the
GNOME-native default and the shape G4 checks for on Linux.

---

## 2. Standard menu structure (document viewer)

Checkable expectation for a document viewer on each platform. macOS follows
the system-defined menu set and ordering; Windows/Linux mirror the same
logical groups in whatever surface that platform uses (menu bar / header-bar
menu button).

| Menu | Expected contents (checkable) | Source |
|---|---|---|
| **App** (macOS only) | About Trailer first, then a separator; Settings… (⌘,); Hide Trailer; Quit Trailer (⌘Q). About uses a short app name (≤16 chars), no version number. | Apple HIG — The menu bar |
| **File** | New, Open (⌘O / Ctrl+O — takes `…` because it presents a chooser), Open Recent, Close (⌘W / Ctrl+W), Save (⌘S / Ctrl+S), Save As / Duplicate, Print… (⌘P / Ctrl+P). Prefer **Duplicate** over "Save As…" on macOS (Option reveals Save As). | Apple HIG — The menu bar; File management |
| **Edit** | Undo (⌘Z / Ctrl+Z), Redo (⇧⌘Z / Ctrl+Shift+Z), Cut/Copy/Paste, **Select All** (⌘A / Ctrl+A — what it selects defers to ADR 0001), Find items. Undo/Redo labels name their target ("Undo Rotate Page"). | Apple HIG — The menu bar; Menus |
| **View** | Present even for a subset of view functions; each show/hide item's title reflects current state ("Show Sidebar" vs "Hide Sidebar"). Carries zoom (in / out / fit / actual size). | Apple HIG — The menu bar |
| **Window** (macOS) | Present even with a single window; includes Minimize (⌘M) and Zoom so Full Keyboard Access users can reach them. | Apple HIG — The menu bar |
| **Help** | A Help menu is present; **⌘? opens it on macOS** and it exposes a **searchable** help field. Carries **Check for Updates…** (always enabled, independent of the Preferences → Updates auto-check toggle — G3) and **Feedback Report…**, same placement on all three platforms — see below. | Apple HIG — Keyboards; NN/g heuristic 10 |

**Check for Updates… placement (2026-07-30):** lives in the **Help** menu
identically on macOS, Windows, and Linux (`src/ui/MainWindow.cpp`'s
`buildMenus()`), not the macOS App menu. This is a deliberate uniform
choice, not a per-OS adaptation: Sparkle-integrated Mac apps conventionally
put "Check for Updates…" in the App menu because Sparkle injects it there
automatically, but Trailer's update checker (`src/update/`, see
`docs/decision-records/2026-07-30-nightly-auto-update-channel.md`) is
custom, not Sparkle — putting the action in Help keeps one code path and
one menu-construction site serving all three platforms rather than an
App-menu special case for macOS alone. Revisit only if user testing shows
Mac users don't find it there.

**Feedback Report… placement, and the ApplicationSpecificRole rule
(2026-08-02):** "Feedback Report…" likewise lives in the **Help** menu on
all three platforms. No per-window `QAction` may carry
`QAction::ApplicationSpecificRole`. On macOS that role moves the item out
of the window's own menu and into the single shared *application* menu;
Qt's Cocoa bridge merges the well-known roles (About / Preferences / Quit)
into fixed slots, but appends each `ApplicationSpecificRole` item
separately — it keys them on the per-`QAction` `QCocoaMenuItem` pointer, so
every window that builds its own menus contributes another copy, and
closing the window does not reliably remove it. Owner dogfooding on
2026-08-02 (build `0.3.1-dev+768.gce56b4b8`) found **four** identical
"Feedback Report…" items stacked between "Settings…" and "Services". The
checkable rule: **across every live window, the count of actions whose
`menuRole()` is `ApplicationSpecificRole` is zero**, and each window's Help
menu holds exactly one `action.help.feedbackReport`. Pinned by
`tests/test_menu_placement.cpp` and UAT-XCT-081. If a future feature really
does need one app-wide item, it must be a single `QAction` owned by
`Application` and installed once — not one per window.

Checkable roll-up: on macOS the App/File/Edit/View/Window/Help menus exist
in that order; on Windows/Linux the same File/Edit/View/Help groups exist in
the in-window menu bar / header-bar menu, and no group present on one OS is
absent on another (that absence is a G4 failure).

---

## 3. Unified shortcut table (1:1 modifier mapping)

One logical verb per row. The **only** difference between columns is the
primary modifier: **⌘ on macOS ↔ Ctrl on Windows/Linux** for the same verb.
A single logical binding drives all three by swapping the modifier. Rows are
reconciled against DESIGN §7 (the shipped-vs-planned source of truth);
where Trailer's live binding differs from the platform default it is called
out in Notes.

| Logical verb | macOS | Windows / Linux | Notes |
|---|---|---|---|
| Open | ⌘O | Ctrl+O | Presents the Open panel / file chooser. |
| Save | ⌘S | Ctrl+S | See §4 (save model). |
| Save As / Duplicate | ⇧⌘S | Ctrl+Shift+S | macOS prefers Duplicate; Option reveals Save As. |
| Export As | ⇧⌘E | Ctrl+Shift+E | Trailer binding (DESIGN §7). |
| Print | ⌘P | Ctrl+P | |
| Close window | ⌘W | Ctrl+W | |
| Quit | ⌘Q | Ctrl+Q | Windows/Linux: no exit-on-close (DESIGN §5.4). |
| Undo | ⌘Z | Ctrl+Z | Label names the target action. |
| Redo | ⇧⌘Z | Ctrl+Shift+Z | Windows also accepts Ctrl+Y by convention. |
| Cut / Copy / Paste | ⌘X / ⌘C / ⌘V | Ctrl+X / Ctrl+C / Ctrl+V | Handled by native widgets in text inputs; do not override. |
| Select All | ⌘A | Ctrl+A | **What it selects defers to [ADR 0001](decision-records/0001-select-all-semantics.md)** — a viewer's ⌘A must select *something* predictable, not do nothing. |
| Find | ⌘F | Ctrl+F | |
| Find Next / Previous | ⌘G / ⇧⌘G | Ctrl+G / Ctrl+Shift+G | Windows also F3 / Shift+F3 by convention. |
| Zoom in | ⌘= | Ctrl+= | Viewer-critical. `+` accepted where a keyboard produces it without Shift. |
| Zoom out | ⌘- | Ctrl+- | Viewer-critical. |
| Zoom reset (default view) | ⌘0 | Ctrl+0 | Viewer-critical. Trailer binds ⌘0/Ctrl+0 to **Actual size**; **Fit page** is ⌘9/Ctrl+9 — the digit row 1/2/3 is reserved for page mode (below), so zoom moved off it (DESIGN §7). |
| Page mode: Continuous / Single Page / Two Pages | ⌘1 / ⌘2 / ⌘3 | Ctrl+1 / Ctrl+2 / Ctrl+3 | Trailer binding, not a platform default (DESIGN §7). View-menu order top-to-bottom matches this Cmd-1/2/3 numbering and never reorders by which mode is active (gate G10 — spatial constancy). |
| Cancel current operation | ⌘. | Esc | ⌘. is the macOS cancel; Esc is the Windows/Linux cancel — this is the one row that is **not** a plain modifier swap. |
| Preferences / Settings | ⌘, | Edit/Tools → Preferences (Ctrl+, on Linux) | macOS app-menu Settings…; app-level settings only. |
| Minimize window | ⌘M | Ctrl+M | |
| First / Last page | ⌘Home / ⌘End | Ctrl+Home / Ctrl+End | |
| Previous / Next page (Go) | ⌘Left / ⌘Right | Ctrl+Left / Ctrl+Right | Viewer also uses PageUp / PageDown. |
| Go to page… | ⌥⌘G | Ctrl+Alt+G | |

Checkable rules for the table itself:
- **A standard verb is wired to its standard shortcut**; a standard combo is
  never repurposed for a non-standard action.
- **Every command is keyboard-operable** — the app is fully usable with the
  keyboard alone. (Apple HIG — Accessibility; NN/g heuristic 7.)
- Modifier meaning is **stable** across the app — the same combo means the
  same verb everywhere. (asktog — Consistency.)

---

## 4. Save-model conventions

Trailer's document model is *never-worry-save* — cross-linked, not restated,
to [`../PHILOSOPHY.md`](../PHILOSOPHY.md) → *Never worry about saving* and
decision record
[`0004-never-worry-save-invariant`](decision-records/0004-never-worry-save-invariant.md)
(status: proposed — the invariant-vs-opt-out question is to be decided in
ADR 0004, not here). The per-OS *presentation* conventions are:

| Rule (checkable) | Platform | Source |
|---|---|---|
| Autosave by default — **avoid making the user take an explicit action to save**; save periodically while editing, on close, and on app switch. | macOS (and Trailer's default everywhere) | Apple HIG — File management |
| When autosave is **on**, do **not** show an unsaved-changes dot — it falsely implies the user must act. | macOS | Apple HIG — File management |
| When autosave is **off** (user disabled it — DESIGN §6.10.1), show unsaved state: a **dot on the window close button** and next to the name in the Window menu, and present a save dialog on close/quit. | macOS | Apple HIG — File management |
| The **"Edited"** title suffix may be shown regardless of autosave state, but must be **removed as soon as an autosave or save occurs**. | macOS | Apple HIG — File management |
| **Prefer Undo over a confirmation dialog** for reversible actions; **confirm (modal) only for irreversible, high-consequence loss** (overwrite, permanent delete, close-without-saving when unrecoverable). | Windows, GNOME/Linux | Microsoft — Commanding basics; GNOME HIG; Apple HIG — Feedback |
| **Do not overuse confirmation dialogs** — they help on mistakes but hinder intentional actions. | all | Microsoft — Commanding basics; NN/g |

Converged checkable rule (all platforms): *never silently lose work; block
with a modal only for irreversible, high-consequence actions; everything
else is Undo-able.* This is the save-model half of Trailer's
forgiveness-and-reversibility stance (DESIGN §2.3).

---

## 5. Citations

Every source URL used above, so the citation set is committed and
survivable.

**Apple Human Interface Guidelines**
- Menus — https://developer.apple.com/design/human-interface-guidelines/menus
- The menu bar — https://developer.apple.com/design/human-interface-guidelines/the-menu-bar
- Keyboards (standard keyboard shortcuts) — https://developer.apple.com/design/human-interface-guidelines/keyboards
- Windows — https://developer.apple.com/design/human-interface-guidelines/windows
- File management (the "never worry about saving" model) — https://developer.apple.com/design/human-interface-guidelines/file-management
- Feedback — https://developer.apple.com/design/human-interface-guidelines/feedback
- Accessibility — https://developer.apple.com/design/human-interface-guidelines/accessibility
- Designing for macOS — https://developer.apple.com/design/human-interface-guidelines/designing-for-macos

**Microsoft (Windows / Fluent)**
- Commanding basics — https://learn.microsoft.com/en-us/windows/apps/design/basics/commanding-basics
- Keyboard accelerators — https://learn.microsoft.com/en-us/windows/apps/design/input/keyboard-accelerators

**GNOME**
- Human Interface Guidelines — Design principles — https://developer.gnome.org/hig/principles.html

**Nielsen Norman Group**
- 10 Usability Heuristics — https://www.nngroup.com/articles/ten-usability-heuristics/

**Bruce Tognazzini**
- First Principles of Interaction Design — https://asktog.com/atc/principles-of-interaction-design/
</content>
</invoke>
