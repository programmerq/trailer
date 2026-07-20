# File-menu information architecture: ⌘N = New from Clipboard, Acquire dissolved into direct entries

- **Status:** accepted <!-- proposed | accepted | superseded-by <YYYY-MM-DD-slug> -->
- **Arbiter:** the File-menu-IA arbiter role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-18
- **Date accepted / superseded:** 2026-07-18 (accepted)

## Context

The 2026-07-16 owner manual pass over a macOS dev build surfaced four File-menu
findings that the offscreen review machinery had missed (recorded in
DR 2026-07-16-ux-walkthrough-platform-parity-personas, §1): "New" IA confusion
(unclear what "New" does), new/acquire actions disappearing once a document
window is key, and ⌘N not being mapped to the owner's hottest path. This record
adjudicates the File-menu information architecture that answers them.

**What shipped before this branch (merge-base `233d63b`), so this record is not
misread as describing the present:**

- The create/acquire group lived **only** in the macOS no-window menu bar
  (`Application::installNoWindowMenuBar`, `#ifdef Q_OS_MACOS`). It carried a
  standalone **`&New`** bound to ⌘N that opened a **blank window**
  (`ensureFreshWindow()`), a shortcutless **New from Clipboard**, and a single
  **`&Acquire…`** item that ran `acquireFromScreenshot` (a whole-screen grab with
  no mode choice). The no-window Acquire/clipboard flow narrated failures via
  `QMessageBox::information` popups.
- The per-window `MainWindow` File menu (`MainWindow::buildMenus`) had **no**
  create/acquire group at all — it started at `&Open…`. So the moment a document
  window became key, New / New-from-Clipboard / Acquire **vanished** (the "vanish"
  bug). On non-macOS the group did not exist anywhere, because its only home was
  the macOS-gated no-window bar.
- `MainWindow` bound **Tools → Take Screenshot** to **⌘⇧3**
  (`Qt::CTRL | Qt::SHIFT | Qt::Key_3`) — a chord macOS intercepts globally for its
  own screenshot, so the app never received it.

These are **owner-decided** requirements, brought here to be recorded as settled
— not re-argued. The implementation is live on `feat/file-menu-ia`
(`Application::addNewFromClipboardAction`, `Application::addAcquireItems`,
`Application::newFromClipboard`, `Application::refreshClipboardActions`, wired
into both `MainWindow::buildMenus` and `installNoWindowMenuBar`) and asserted by
`tests/uat/test_uat_file_menu_ia.cpp`.

## Options

- **A. Ship the owner-decided IA (what ships).** ⌘N ≡ New from Clipboard;
  dissolve Acquire into direct File-menu entries (Screenshot submenu + Scanner /
  Camera placeholders); host the create/acquire group on every window's File menu
  on every platform; make clipboard/paste of non-image data an inert disabled
  item, never a popup; delete the dead ⌘⇧3 binding.
- **B. Keep standalone "New" on ⌘N, add New-from-Clipboard as a lesser item.**
  Preserve the blank-window shortcut and bolt the clipboard path on beside it.
- **C. Keep the single "Acquire…" item.** Leave a one-shot whole-screen Acquire
  rather than surfacing Screenshot modes and the Scanner / Camera roadmap.
- **D. Hide unimplemented acquire sources.** Drop Scanner / Camera (and the
  platform-gated Screenshot modes) until a backend exists, rather than showing
  them disabled with a tooltip.

## Personas debate

- **Office non-technical user:** Copies an image from a browser or chat and wants
  it in the app now. Under A, ⌘N does exactly that — the hottest path is the
  reflex shortcut. Under B, ⌘N opens an empty window and they must hunt for the
  clipboard item. Favours A.
- **Older careful user:** Wants a File menu whose choices are honest about what
  exists. Prefers Scanner / Camera shown **disabled with a plain tooltip**
  (Option A / against D) over silently absent capabilities they cannot tell are
  planned versus broken. A popup that just says the clipboard is empty (the old
  behaviour) reads as the app scolding them; the disabled item is calmer.
- **Power migrator (Preview / Acrobat muscle memory):** Expects the screenshot
  modes named (Whole Screen / Window / Selected Area) the way the native tools
  name them, not hidden behind an undiscoverable spacebar cycle. Expects
  create/acquire to stay put when a document is open — Preview never hides File-menu
  entries because a window is focused. Favours A; C keeps the discoverability gap.
- **Occasional user:** Rarely acquires; when they do, the menu is their only map.
  A dead ⌘⇧3 that silently does nothing (macOS ate it) teaches that the app's
  shortcuts are unreliable. Removing it (A) is strictly better than a shortcut
  that never fires.

## Admissible objections

- **Office user + power migrator, Option B — ⌘N step:** the owner's hottest path
  is new-from-clipboard; spending the muscle-memory ⌘N on a blank window forces
  the primary flow onto a worse shortcut or a mouse hunt. Decisive against B.
- **Power migrator, Option C — Screenshot step:** a single "Acquire…" whole-screen
  grab gives no way to choose Window or Selected Area; the OS picker's mode switch
  is an undiscoverable spacebar cycle. Decisive against C for the named-modes need.
- **Every persona, the vanish bug — "open a document" step:** create/acquire
  living only in the macOS no-window bar meant the actions disappeared the instant
  a window became key (and never existed on other platforms). A user mid-task
  cannot start a new acquire without first closing their document. Decisive for
  hosting the group on every window's File menu on every platform.
- **Older careful user, Option D — Scanner / Camera step:** hiding an
  unimplemented-but-planned source is indistinguishable from the app lacking it;
  a disabled item with an honest tooltip communicates "planned, not yet" without a
  dead-end click. Decisive against D (per the owner policy: unavailable
  capabilities are disabled + explained, never dropped).
- **Any user, non-image clipboard — "press ⌘N with text copied" step:** an
  informational popup that just says there is nothing to paste is narration the
  user did not ask for. The item must instead be **disabled with a tooltip**, and a
  programmatic trigger a silent no-op. Decisive for the no-narration path.

### Rejected as naked preference

- "Keep standalone New for consistency with other apps." — rejected: names no
  concrete user, step, or failure; the owner's measured hottest path is
  new-from-clipboard, and B degrades it (see the admissible objection above).
- "⌘⇧3 is the standard screenshot chord, keep it." — rejected: states a
  convention, not a checkable outcome; on macOS the OS consumes the chord globally
  so the binding never fires — it is dead, not merely unfashionable.

## Checkable threshold this record establishes

Independently checkable, proven by `tests/uat/test_uat_file_menu_ia.cpp`:

- **⌘N binding.** `New from &Clipboard` carries `QKeySequence::New`, and **no**
  other action anywhere on the menu bar owns that sequence; there is no `New` /
  `&New` item on the File menu. (`uat_fmia_003`)
- **Acquire dissolved into direct entries.** The File menu carries a `Screenshot`
  submenu with `Whole Screen`, `Window`, `Selected Area` sub-items, plus `Scanner`
  and `Camera` items — no single `Acquire…` item. (`uat_fmia_001`)
- **Placeholders visible-but-disabled + honest tooltip.** `Scanner` and `Camera`
  exist, are `!isEnabled()`, and carry a non-empty tooltip; on non-macOS `Window`
  and `Selected Area` are likewise disabled + tooltipped rather than hidden.
  (`uat_fmia_004`)
- **No vanish.** With one or more document windows open, `New from Clipboard`, the
  `Screenshot` submenu, and its sub-items are all still present, and (clipboard
  primed with an image) `New from Clipboard` and `Whole Screen` are enabled.
  (`uat_fmia_002`)
- **Non-image clipboard is inert, never a popup.** With non-image, non-path text on
  the clipboard, `clipboardHasOpenableContent()` is false, `New from Clipboard` is
  disabled + tooltipped, and invoking `newFromClipboard()` opens no document and
  spawns no modal (`documentCount` and `windowCount` unchanged). (`uat_fmia_005`)
- **Dead ⌘⇧3 removed.** No action anywhere on the menu bar owns
  `Ctrl/Cmd+Shift+3`. (`uat_fmia_006`)

## Arbiter verdict + rationale

**Accepted 2026-07-18 — Option A**, as the owner-decided IA. The admissible
objections are decisive against every alternative: B degrades the measured hottest
path (⌘N → clipboard), C leaves the Screenshot-mode discoverability gap, D hides
planned capabilities behind indistinguishable absence, and the vanish bug is a
concrete mid-task failure that hosting the group on every window's File menu fixes.
The non-image-clipboard path is disabled-plus-tooltip rather than a popup, applying
the ratified no-narration-dialogs principle (PHILOSOPHY → *No popup that just says
"no"*); the same rule made the old `QMessageBox::information` acquire narration
inadmissible. The dead ⌘⇧3 chord is removed because it never fired — macOS consumes
it globally — so it is a broken affordance, not a style choice.

Screenshot modes are surfaced as named items (Whole Screen / Window / Selected
Area) because the OS picker hides its mode switch behind an undiscoverable spacebar
cycle; on non-macOS only whole-screen capture is wired through the QScreen
fallback, so Window / Selected Area ship visible-but-disabled with an honest
tooltip (the same G3/G4 disabled-not-hidden rule the Scanner / Camera placeholders
follow). The implementing seams are `Application::addNewFromClipboardAction` and
`Application::addAcquireItems`, invoked from both `MainWindow::buildMenus` and
`Application::installNoWindowMenuBar` so the create/acquire group is identical on
the per-window and no-window surfaces.

## Consequences

- **Positive.** The owner's hottest path is one reflex keystroke; create/acquire is
  reachable from any window on any platform; screenshot modes and the Scanner /
  Camera roadmap are discoverable; no shortcut on the menu bar is dead; no acquire
  path narrates a "nothing to do" popup.
- **Costs / follow-ups.** Scanner and Camera are inert until their backends land —
  tracked separately, not in this record. On non-macOS, Window / Selected-Area
  screenshot capture and a Retina-accurate clipboard-paste dpr remain open (the
  paste-dpr heuristic in `newFromClipboard` is conservative pending owner
  confirmation on Retina hardware). These are enhancements to an accepted IA, not
  reopenings of it.

## Evidence required to reopen

A measured case where the accepted IA costs a real user a concrete step — e.g.
telemetry or an owner pass showing ⌘N ≡ New-from-Clipboard misfires for a common
workflow, or that a disabled Scanner / Camera placeholder is read as breakage
rather than roadmap — plus owner sign-off. A wired Scanner / Camera / Window /
Selected-Area backend simply enables an existing item and does **not** require
reopening this record.
