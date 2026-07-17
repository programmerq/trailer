# Decision record: macOS Quit and Keep Windows

<!--
This record uses the date+slug naming scheme (docs/decision-records/YYYY-MM-DD-<slug>.md),
the same scheme as docs/backlog/, to avoid parallel-branch ADR-number collisions.
Refer to it by slug/date, not a number. It follows TEMPLATE.md and the process in
PHILOSOPHY.md → "How design decisions get adjudicated".
-->

- **Status:** proposed <!-- proposed | accepted | superseded-by <slug> -->
- **Arbiter:** platform-integration (agent role; the owner, programmerq, is the escalation-only override)
- **Date proposed:** 2026-07-16
- **Date accepted / superseded:** —

## Context

macOS applications offer a per-quit variant of Quit called **Quit and Keep
Windows**, reached by holding Option in the app menu so the Quit item's text and
shortcut swap in place (`Quit Trailer  ⌘Q` → `Quit and Keep Windows  ⌥⌘Q`) and by
the `⌥⌘Q` accelerator directly. The owner has **decided the shape** of bringing
this to Trailer; this record does not re-litigate that shape. It exists to
adjudicate the *implementation-design room* the shape leaves open — how the
persistence is stored, how the menu swap is realised on a Qt native menu, how the
behaviour composes with the OS-level default, whether a draft store bleeds into
crash-recovery scope, and whether any of this applies off macOS.

**Owner-decided shape (recorded as settled — not on the table below):**

- The feature exists **on macOS**. Accelerator `⌥⌘Q` (Option+Cmd+Q).
- The menu item is **not** a separate row next to Quit. With the app menu open,
  holding Option makes the Quit item text+shortcut swap **in place** — Quit label
  anchored left, shortcut right — the standard macOS `NSMenuItem` `alternate`
  pattern (`Quit Trailer  ⌘Q` ⇄ `Quit and Keep Windows  ⌥⌘Q`).
- Regular Quit (`⌘Q`) prompts to pick a name or discard for each unsaved document,
  **one at a time** when there are several — the sequential per-document form of
  the ADR-0004 (`docs/decision-records/0004-never-worry-save-invariant.md`)
  close-save flow, applied at quit.
- Quit and Keep Windows (`⌥⌘Q`) does **not** prompt: it persists the current
  window set **including unsaved/untitled documents with their content intact**,
  and on next launch those same untitled/unsaved documents return.

**What ships today (so this record is not misread as describing the present):**

- `⌘Q` today **does not** prompt per document. `Application::onAboutToQuit`
  (`src/app/Application.cpp:229-259`) walks live windows and snapshots the open
  **file paths only** into `Settings::setSessionOpenFiles`
  (`src/settings/Settings.cpp:213-216`, header `src/settings/Settings.h:116-117`),
  then quits. There is no per-document Save/Discard prompt at quit, and untitled
  or unsaved edits are **not** captured — only paths of on-disk files.
- On next launch `Application::restorePreviousSession`
  (`src/app/Application.cpp:207-227`) reopens those paths **iff** the
  `restorePreviousWindows` pref is on (`src/settings/Settings.h:109`, default
  `true` at `:186`) and no files were passed on the CLI. Paths that no longer
  exist are silently dropped.
- So today's persistence is a **file-path list**, not a document-content draft
  store. An untitled window, or a titled window with unsaved edits, does **not**
  come back with its content — only the saved file reopens.
- The app already keeps `settings.toml` and `recent.json` under
  `~/Library/Application Support/Trailer` (`src/settings/AppPaths.cpp:16-19`,
  `:54-60`), so a per-app writable data dir already exists.
- The Quit action is a Qt `QAction` with `QAction::QuitRole` on the native menu
  bar, in two places: `MainWindow`'s File menu
  (`src/ui/MainWindow.cpp:761-764`) and the no-window menu bar
  `Application::installNoWindowMenuBar` (`src/app/Application.cpp:305-308`).
  Neither exposes the Cocoa `NSMenuItem.alternate` flag that the in-place
  Option-swap requires.
- The only Obj-C++ platform shim in the repo today is
  `src/platform/Share.{h,mm}` with a non-macOS fallback `Share_stub.cpp`
  (`src/platform/`), which is the established pattern for reaching an AppKit API
  Qt does not surface.

This record cites PHILOSOPHY → *Never worry about saving* and ADR-0004's
no-silent-data-loss floor (the quit-time prompt is that floor at quit), DESIGN
§2.5.2 personas, and gates G1/G2 in `AGENTS.md`.

## Options

The owner-decided shape is fixed. The genuinely open design choices, each stated
as options for the arbiter to pick:

**D1 — Persistence mechanism for the kept window set (incl. unsaved/untitled bytes).**
- **A. App-managed draft store.** Serialize the open-window set plus each
  unsaved/untitled document's **bytes** to a Trailer-owned store under
  `~/Library/Application Support/Trailer/` (e.g. a `restore/` subdir keyed by a
  window/session id, with a manifest). On launch, when restore is indicated,
  rehydrate untitled/unsaved documents from the stored bytes. Extends today's
  path-list session with a content tier that Trailer fully controls.
- **B. Native `NSWindowRestoration` / Cocoa state restoration.** Adopt the OS
  mechanism: mark windows restorable, implement
  `-restoreStateWithCoordinator:` / `NSApplicationDelegate` restoration hooks so
  macOS drives encode-on-quit and restore-on-launch, and let
  `NSQuitAlwaysKeepsWindows` compose for free.

**D2 — The in-place Option menu swap.**
- **A. Native `.mm` shim.** Add an Obj-C++ shim (following `Share.mm` +
  `Share_stub.cpp`) that reaches the concrete `NSMenuItem` behind the Quit
  `QAction` and installs an `alternate` item carrying the `NSEventModifierFlagOption`
  key-equivalent mask, so AppKit performs the in-place text+shortcut swap.
- **B. Pure Qt.** Approximate with two `QAction`s and shortcut juggling, or a
  second always-visible "Quit and Keep Windows" row.

**D3 — Composition with the OS setting `NSQuitAlwaysKeepsWindows`** ("Close
windows when quitting an app", System Settings → Desktop & Dock).
- **A. Honour the OS default; the menu item is always the *other* branch.** The
  setting decides which behaviour `⌘Q` performs by default and the Option-swap
  offers the opposite, exactly like a native app.
- **B. Trailer-fixed behaviour** independent of the OS setting (⌘Q always
  prompt-and-quit-clean; ⌥⌘Q always keep), ignoring the system default.

**D4 — Whether the draft store doubles as crash recovery.**
- **A. In-scope now** — design the store so an unclean termination also restores.
- **B. Out of scope (boundary only)** — record that the store *could* support it,
  design nothing for it here.

**D5 — Linux/Windows equivalent** ("or any equivalent if the other OSes have it").
- **A. macOS-only for now** — ship the feature on macOS; off macOS the existing
  `restorePreviousWindows` pref + path-list session remains the "reopen" story.
- **B. Cross-platform** — invent a "Quit and Keep Windows"-equivalent for
  Linux/Windows now.

## Personas debate

Each persona is an unranked adversarial lens (DESIGN §2.5.2). §2.5.3 names the
office non-technical user and the older careful user as the coverage floor; all
four are covered here.

- **Office non-technical user** (Windows-primary, PDFs daily, "Will this break my
  file? Where did my changes go?"): Rarely the one hitting `⌥⌘Q` on a Mac, but is
  the sharpest lens on **D1** and **D4**. If a kept untitled window came back
  *empty* or *stale*, this user's reaction is "it lost my work" — the exact
  never-worry failure. Wants restore to be all-or-nothing and truthful: an
  untitled doc either returns with its content byte-for-byte or does not claim to.
  On **D3**, would be confused if `⌘Q` "keeps windows" on one Mac and not another
  with no visible reason — but that is the OS's own inconsistency, and matching it
  is *less* surprising than diverging from it.
- **Older careful user** ("I need to know when it saves", prefers explicit Save,
  double-checks): The decisive lens on the **`⌘Q` sequential per-doc prompt**.
  This user *wants* to be asked, one document at a time, and to see each name-or-
  discard choice — a single bulk "discard all?" would violate their need to
  account for each document. They also need `⌥⌘Q` to be a **deliberate,
  discoverable** act, not a slip: the Option-swap (**D2**) is good precisely
  because it is modal and momentary — you only get the no-prompt keep behaviour
  while consciously holding Option. A silent no-prompt quit on the *plain* `⌘Q`
  would alarm them.
- **Power migrator** (from Preview/Acrobat, strong muscle memory, "Where is the
  equivalent?"): The reason the feature exists in the owner-decided form.
  Expects `⌥⌘Q` to mean exactly what it means system-wide and the label to read
  the exact standard string "Quit and Keep Windows" with the in-place swap
  (**D2-A**) — an extra always-visible row (D2-B) reads as "not a real Mac app."
  On **D1**, does not care *how* bytes are stored, only that an untitled scratch
  window survives a relaunch the way Preview's untitled windows do. On **D3**,
  has muscle memory tied to the OS setting and expects Trailer to ride it (D3-A).
- **Occasional user** (opens the app every few weeks, has forgotten everything):
  Will not know `⌥⌘Q` exists and will not miss it; low stake on **D2** discovery.
  But is the lens on **honest restore**: if they quit weeks ago with a kept
  untitled window, coming back to it intact is a delight, and coming back to a
  half-restored or errored window is a quiet betrayal. Favours **D1-A** only if
  the store is robust to app-version drift; otherwise favours *not* promising
  content restore at all over promising it and failing. Neutral on **D4/D5**.

## Admissible objections

Objections that name a user, a step in a real flow, and the failure hit there
(the PHILOSOPHY §"admissible-objection test" bar).

- **Office/occasional user, D1-B (native restoration), untitled-bytes step,
  content loss.** Qt does not surface Cocoa state restoration through
  `QWidget`/`QWindow`; `restoreStateWithCoordinator:` and the
  `NSApplicationDelegate` restoration hooks live on native objects Qt owns and
  drives, and Qt's own macOS guidance is that NSWindow-level configuration is not
  available through the cross-platform API. So on **D1-B** the user quits with an
  untitled window, relaunches, and the window either does not return or returns as
  an empty Qt shell because no Trailer code ran to re-inflate the *document model*
  behind it — a concrete "where did my changes go?" failure. This is the decisive
  objection against Option B for D1.
- **Older careful user, D2-B (pure-Qt swap), the quit step, wrong/ambiguous
  control.** A second always-visible "Quit and Keep Windows" row, or a Qt shortcut
  hack, produces a menu that does not match any Mac this user has used: two
  quit-like rows where they expect one, or an `⌥⌘Q` that silently does nothing
  because Qt swallowed the modified key-equivalent. They hesitate at the one step
  (quit) where hesitation means "I don't trust what this will do to my open
  documents." Names the concrete failure that drives D2 to the native shim.
- **Power migrator, D3-B (Trailer-fixed), the `⌘Q` step, broken muscle memory.**
  A migrator who has set "Close windows when quitting" to their preference expects
  every well-behaved app to honour it. Under D3-B, `⌘Q` in Trailer does the
  opposite of what the same keystroke does in Preview on the same machine — the
  migrator hits `⌘Q` expecting their configured default and gets Trailer's fixed
  one. Concrete cross-app inconsistency at the most-used shortcut.
- **Older careful user, `⌘Q` bulk-discard step, unaccounted document.** If the
  `⌘Q` quit-time prompt were a single "discard all unsaved?" instead of the
  owner-decided **sequential per-document** prompt, this user cannot account for
  each document and may discard one they meant to keep. This is why the record
  ratifies the sequential form (ADR-0004's close-save flow at quit), not a bulk
  prompt.

### Rejected as naked preference

- "A native app *should* just use the real macOS restoration API." — rejected:
  states a taste for native purity, names no user/step/failure. The admissible
  version is the office/occasional content-loss objection above, which is what
  actually decides D1 — and it decides *against* the native API here, because Qt
  cannot re-inflate the document model through it.
- "An app-managed draft folder feels heavy / clutters Application Support." —
  rejected: no user hits a failure at a step; the store sits beside the existing
  `settings.toml`/`recent.json` already there. A concrete version would have to
  name a disk-budget or a user-visible breakage, and does not.
- "Just show both Quit rows, it's simpler." — rejected as stated: bare simplicity
  preference. The admissible cousin (older-careful two-ambiguous-rows failure) is
  listed above and is what carries weight.

## Checkable threshold this record would establish

The feature is **Done** (UX-Done, gates G1/G2) when all of the following pass;
each is independently checkable by an agent or reviewer.

1. **Injectable quit-mode seam.** Quit is routed through an injectable mode
   (`QuitMode { PromptPerDoc, KeepWindows }`) so a headless test can invoke either
   path without a real menu event. (Unit-level; no display.)
2. **Draft round-trip (headless).** A test creates an **untitled** document,
   writes known bytes into it, invokes the KeepWindows path, tears down the app
   object, constructs a fresh app pointed at the same Application-Support dir, runs
   restore, and asserts the untitled document returns with **byte-identical**
   content and its untitled/dirty state intact. A titled-but-dirty document
   likewise returns with its unsaved edits. Runs under
   `QT_QPA_PLATFORM=offscreen`.
3. **`⌘Q` sequential per-doc prompt.** With ≥2 dirty documents open, the
   PromptPerDoc path raises the ADR-0004 Save/name-or-discard prompt **once per
   dirty document in sequence** (not a bulk prompt), and honours each choice;
   Cancel at any step aborts the quit with all documents still open and nothing
   written. Asserted via the injected seam + a scriptable prompt double.
4. **`⌥⌘Q` keeps without prompting.** The KeepWindows path raises **no** prompt
   even with dirty/untitled documents open, and produces a complete draft-store
   manifest covering every open window.
5. **Menu alternate swaps under Option (offscreen-observable slice).** The Quit
   `QAction`'s backing `NSMenuItem` has an `alternate` item whose
   key-equivalent-modifier mask includes Option and whose title is exactly
   `Quit and Keep Windows`; the base item's title is `Quit Trailer` /
   `⌘Q`. Verified by querying the native item through the shim on macOS. The
   **live visual swap** (holding Option actually re-rendering the row) is flagged
   for **owner MANUAL verification** — see step 7.
6. **OS-setting composition.** With `NSQuitAlwaysKeepsWindows` toggled, a test (or
   the shim) confirms which of `⌘Q`/`⌥⌘Q` maps to keep vs. prompt-and-close per
   the D3 verdict — i.e. the default branch follows the OS setting and the
   Option-swap offers its complement.
7. **Honest manual gate (real relaunch).** Because true state restoration and the
   live Option re-render cannot be observed offscreen (DESIGN §2.5.3 real-Mac
   tier), the owner performs, on a real Mac, exactly: (a) open one untitled window,
   type text; (b) hold Option in the Trailer app menu, confirm the Quit row text
   and shortcut swap in place; (c) press `⌥⌘Q`; (d) relaunch Trailer; (e) confirm
   the untitled window returns with the typed text; (f) repeat with plain `⌘Q` and
   confirm the sequential per-doc prompt appears and, on quit, the untitled window
   does **not** silently return. This checklist ships in the implementation PR's G2
   evidence, marked manual.

"Vibes" pass/fail is excluded: every clause is a seam call, a byte comparison, a
native-attribute query, or a scripted manual step.

## Arbiter verdict + rationale

Status is `proposed`; the owner ratifies proposed→accepted. The
platform-integration arbiter's recommendation:

**Accept the owner-decided shape and the following resolutions of the open room.**

- **D1 → Option A (app-managed draft store under Application Support).** Driven by
  the office/occasional content-loss objection against D1-B. Qt 6 uses the native
  `NSMenu`/AppKit chrome but does **not** expose Cocoa state restoration through
  `QWidget`/`QWindow`; the `restoreStateWithCoordinator:` /
  `NSApplicationDelegate` restoration hooks require a native `.mm` shim *and*, more
  fundamentally, macOS restoring an `NSWindow` does not reconstruct Trailer's
  `IDocument` model or the untitled bytes behind it — only Trailer code can do
  that. Riding `NSWindowRestoration` would restore window chrome while the document
  content stayed lost, which is exactly the failure the objection names. An
  app-managed store also composes cleanly with what already exists: today's
  `sessionOpenFiles` path-list (`src/app/Application.cpp:207-227`,
  `src/settings/Settings.cpp:213-216`) becomes the *titled-and-saved* tier, and the
  draft store adds the *untitled/unsaved bytes* tier beside the existing
  `settings.toml`/`recent.json` (`src/settings/AppPaths.cpp`). Web grounding: Qt's
  own macOS-issues guidance treats NSWindow-level configuration as outside the
  cross-platform API, and the `NSQuitAlwaysKeepsWindows`/state-restoration model is
  an OS convention Trailer can *mirror* without *delegating its document model to*.
- **D2 → Option A (native `.mm` shim).** Driven by the older-careful and
  power-migrator menu objections. `QAction::QuitRole` on the native menu bar
  (`src/ui/MainWindow.cpp:761-764`, `src/app/Application.cpp:305-308`) does not
  expose `NSMenuItem.alternate`; the in-place text+shortcut swap is an AppKit
  behaviour reachable only by setting `alternate` + the Option key-equivalent mask
  on the concrete `NSMenuItem`. Add a shim following the existing
  `src/platform/Share.mm` + `Share_stub.cpp` pattern (the repo's only Obj-C++
  shim), so non-macOS builds link the stub and macOS gets the real swap. Pure-Qt
  (D2-B) is rejected: it yields the two-ambiguous-rows / dead-`⌥⌘Q` failures.
- **D3 → Option A (honour the OS `NSQuitAlwaysKeepsWindows` default; the menu
  offers the complement).** Driven by the power-migrator cross-app-consistency
  objection. The system setting decides the **default** branch of `⌘Q`
  (keep-windows when `NSQuitAlwaysKeepsWindows` is set / "Close windows…"
  unchecked; prompt-and-close-clean otherwise), and the Option-swap always offers
  the *opposite* branch — the standard native composition where holding Option
  overrides the default for one quit. Trailer's ADR-0004 quit-time per-doc prompt
  is layered onto the *close-clean* branch (we ask before discarding), which is a
  strengthening of, not a divergence from, the OS convention. **Boundary noted:**
  when the OS default is "keep windows," plain `⌘Q` follows the keep path and does
  not run the per-doc prompt (there is nothing to discard) — the sequential prompt
  is specifically the close-clean branch's floor.
- **D4 → Option B (out of scope; boundary only).** The draft store *could* double
  as crash recovery (an unclean termination leaves the same serialized state a
  keep-quit would). This record **explicitly does not design that** — no
  write-cadence, no unclean-shutdown detection, no recovery-prompt UX is decided
  here. It is called out as deliberate **scope creep** and left for a separate
  record so this one stays about the quit feature.
- **D5 → Option A (macOS-only for now).** There is no strong Linux/Windows OS
  convention for a per-quit "keep windows" gesture, and inventing one now is
  unscoped. Off macOS, the **existing** `restorePreviousWindows` pref (default
  `true`, `src/settings/Settings.h:109,186`) plus the path-list session
  (`Application::restorePreviousSession`) already provides a partial, honest
  "reopen what I had" story for *saved* files — that stands as the cross-platform
  equivalent, and the untitled-bytes draft tier is macOS-only until a concrete
  cross-platform need is named. This is the acceptable honest answer, not a
  deferral of a promised feature.

Which objections drove it: the content-loss objection (office/occasional) decides
D1 *against* the superficially "more native" Option B; the menu-ambiguity
objection (older-careful/migrator) decides D2 for the shim; the cross-app-
consistency objection (migrator) decides D3 for OS-composition; the bulk-discard
objection ratifies the sequential per-doc prompt at `⌘Q`. Naked-preference items
(native-purity, folder-clutter, "just show both rows") carry no weight, as
recorded. The owner retains the escalation-only veto and the proposed→accepted
sign-off.

## Evidence required to reopen

Once accepted, reopening requires a concrete, checkable problem not on the table
here, plus owner sign-off:

- A demonstrated Qt 6 path that restores **untitled document content** (not just
  window chrome) through native `NSWindowRestoration` without a Trailer-owned
  byte store — which would reopen **D1**.
- A measured failure of the app-managed draft store: a round-trip that loses or
  corrupts untitled bytes across an app-version upgrade, or a disk-budget breach —
  which would reopen **D1/D4**.
- A macOS release that removes or changes the `NSMenuItem.alternate` in-place-swap
  convention, or a Qt release that surfaces `alternate` through `QAction` — either
  reopens **D2**.
- A change to how macOS composes `NSQuitAlwaysKeepsWindows` with per-app quit
  behaviour that makes D3-A user-visibly wrong — reopens **D3**.
- A named, concrete Linux/Windows user flow (with step and failure) that the
  path-list session does not serve — reopens **D5**.

Naked disagreement ("should have used the native API", "shouldn't keep a folder")
is not superseding evidence.
