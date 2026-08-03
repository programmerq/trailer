# UAT — Cross-Cutting

Covers the features that don't belong to a single document type:
screenshot, theme, and a consolidated keyboard shortcut reference used
as a regression sweep.

Per-feature behaviour is in the phase docs; this file captures the
overall integrations.

---

## Theme

Theme is controlled by `general.theme` in `settings.toml`, one of
`"system"`, `"light"`, `"dark"`. There is no in-app preference dialog
yet (see UAT-FND-091); users must edit the file and restart.

### UAT-XCT-001 — `theme = "system"` follows OS

**Preconditions:** Set `theme = "system"` in `settings.toml`. OS is in
light mode.
**Steps:**
1. Launch the app.
**Expected:**
- Window chrome, menu bar, and viewport background use light-theme
  colours.
2. Switch the OS to dark mode (platform-specific — macOS: System
   Settings > Appearance; Windows: Settings > Personalisation; Linux:
   DE-specific).
**Expected:**
- The app tracks the OS theme either live or on next launch (document
  actual behaviour — live tracking is preferred but not guaranteed).

### UAT-XCT-002 — `theme = "dark"` overrides OS

**Preconditions:** Set `theme = "dark"`. OS in light mode.
**Steps:**
1. Launch.
**Expected:**
- App is in dark theme regardless of OS.

### UAT-XCT-003 — `theme = "light"` overrides OS

Symmetric to UAT-XCT-002.

### UAT-XCT-004 — Invalid theme value

**Preconditions:** Set `theme = "neon"` (or any unsupported value).
**Steps:**
1. Launch.
**Expected:**
- App launches without crash.
- Falls back to system or a documented default.
- Invalid value is either preserved in the file or replaced with the
  default on next save.

### UAT-XCT-005 — Document surround colour is one source of truth and follows a live theme change

The colour behind a page/image that doesn't fill the document area (the
"canvas" or "letterbox") must never look LIGHTER than the page/image it
surrounds — the PDF viewer's canvas was reported "too light" in dark
mode — and must re-derive on a **live** theme switch (Preferences →
Theme, or a System-mode OS flip), not only be correct at the moment the
document was opened. It is **not** required to be pixel-identical between
PDF and image documents in every theme: in light mode the PDF canvas is
deliberately a visible grey (matching Preview / Acrobat / Chrome's PDF
viewer convention — a page is typically white, so a white canvas would
make its boundary invisible), same as it always has been; only the
previously-broken dark-mode case is required to match the image viewer's
colour exactly.

**Preconditions:** A PDF and an image open (in the same or different
windows).
**Steps:**
1. Look at the canvas behind the PDF page in light mode. It should read as
   a visible grey, clearly darker than the (white) page — not the same
   white as the page itself.
2. Switch to Dark (Preferences → General → Theme, or flip the OS while on
   System).
3. Look at the canvas behind the PDF page again.
**Expected:**
- Light mode: PDF canvas is a visible grey, distinct from and darker than
  the page — unchanged from before this fix.
- Dark mode: PDF canvas is at least as dark as the image viewer's own
  canvas colour — never the washed-out light grey previously reported.
  Where Trailer's synthesized dark palette makes `QPalette::Dark` resolve
  lighter than `QPalette::Base` (the root cause — no hand-built dark
  `QPalette` of our own; see
  DR 2026-07-31-document-surround-colour-follows-base), the PDF canvas
  falls back to `QPalette::Base` and matches the image viewer's canvas
  colour exactly.
- Switching theme while the PDF is already open re-colours its canvas
  live, without reopening the document.

Automated: `uat_xct_005_documentSurroundColourFollowsPaletteLive` in
`tests/uat/test_uat_preferences.cpp` (constructs a dark palette with
`QPalette::Dark` deliberately inverted lighter than `QPalette::Base`, to
exercise the fallback deterministically rather than relying on the
offscreen QPA plugin's inability to derive a real dark palette on its
own).

---

## Screenshot (Tools > Take Screenshot)

The flow is the same on every platform: a Qt modal prompts for
`Whole screen` / `Single window` / `Region`, then the app hides itself
and performs the capture. The resulting PNG is opened as a new tab via
the same path as `File > Open`. Implementation lives at
[src/ui/MainWindow.cpp](../../src/ui/MainWindow.cpp) around line 730.

Platform backends:

- **macOS** — shells out to `/usr/sbin/screencapture` with `-iW`
  (window) or `-i -s` (region). The native macOS picker appears once
  the Trailer prompt is accepted.
- **Linux / Windows** — `QScreen::grabWindow(0)` captures the primary
  screen. The `Window` and `Region` radios are disabled and an
  explanatory note is shown in the prompt.

There is no `gnome-screenshot` fallback, nor any Windows-specific
capture API — both platforms go through Qt's generic screen grab.

### UAT-XCT-010 — Take Screenshot prompt opens

**Preconditions:** App running. Any or no document open.
**Steps:**
1. `Tools > Take Screenshot` (`Ctrl+Shift+3`).
**Expected:**
- A Trailer modal dialog appears with three radio buttons:
  `Whole screen`, `Single window (click to select)`,
  `Region (drag to select)`.
- **Platform: macOS** — all three radios are enabled.
- **Platform: Linux / Windows** — only `Whole screen` is enabled; an
  inline note explains the limitation and points to TODO.md.

### UAT-XCT-011 — Whole-screen capture opens a new tab (all platforms)

**Preconditions:** App running.
**Steps:**
1. `Tools > Take Screenshot`.
2. Accept with `Whole screen` selected.
**Expected:**
- The main window briefly hides, the primary screen is captured, and
  a new tab opens containing the resulting PNG.
- The PNG path is also appended to `File > Open Recent`.
- The image is editable (zoom, crop, annotate).

### UAT-XCT-012 — Window capture (Platform: macOS)

**Preconditions:** macOS. Multiple windows visible (at least one other
than Trailer's).
**Steps:**
1. `Tools > Take Screenshot`, pick `Single window`, accept.
**Expected:**
- Trailer hides; macOS's crosshair/window picker from `screencapture
  -iW` takes over.
- Clicking a window captures it; Trailer reappears and opens the PNG
  as a new tab.

### UAT-XCT-013 — Region capture (Platform: macOS)

**Preconditions:** macOS.
**Steps:**
1. `Tools > Take Screenshot`, pick `Region`, accept.
2. Drag-select a region.
**Expected:**
- Trailer hides; macOS's drag-region picker from `screencapture -i -s`
  takes over.
- Releasing the drag captures that region; Trailer reappears and opens
  the PNG as a new tab.

### UAT-XCT-014 — Cancel native picker (Platform: macOS)

**Preconditions:** macOS. Native `screencapture` picker active
(Window or Region mode).
**Steps:**
1. Press Escape.
**Expected:**
- Picker cancels; `screencapture` exits with no output file.
- Trailer reappears. No new tab is opened. No error dialog is shown —
  cancellation is not treated as a failure.

### UAT-XCT-015 — Cancel Trailer's prompt (all platforms)

**Preconditions:** `Take Screenshot` dialog open.
**Steps:**
1. Click `Cancel` (or press Escape on the Qt dialog).
**Expected:**
- Dialog closes. No capture runs. No tab opens. Window never hides.

### UAT-XCT-016 — Window/Region radios on non-macOS (Known gap)

**Preconditions:** Linux or Windows.
**Steps:**
1. Open the `Take Screenshot` dialog.
**Expected:**
- `Single window` and `Region` radio buttons are disabled.
- Forcing them (e.g. via a future config) and accepting shows an
  `Unsupported` information dialog rather than capturing. Cross-ref
  TODO.md.

---

## Keyboard shortcut matrix

Run through every shortcut in order on a fresh app launch with a
multi-page PDF and an image each open in separate tabs. Verify each
triggers the documented action. Platform column gives the canonical
form — `Cmd` on macOS, `Ctrl` on Windows / Linux.

### UAT-XCT-020 — File menu shortcuts

| Shortcut | Action |
|---|---|
| `Cmd/Ctrl+O` | `File > Open…` |
| `Cmd/Ctrl+S` | `File > Save` |
| `Cmd/Ctrl+Shift+S` | `File > Save As…` |
| `Cmd/Ctrl+P` | `File > Print…` |
| `Cmd/Ctrl+W` | `File > Close Window` |
| `Cmd/Ctrl+Q` | `File > Quit` |

### UAT-XCT-021 — Edit menu shortcuts

| Shortcut | Action |
|---|---|
| `Cmd/Ctrl+Z` | `Edit > Undo` |
| `Cmd+Shift+Z` (macOS) / `Ctrl+Y` | `Edit > Redo` |
| `Cmd/Ctrl+A` | `Edit > Select All` |
| `Cmd/Ctrl+F` | `Edit > Find…` |
| `Cmd/Ctrl+G` (macOS) / `F3` | `Edit > Find Next` |
| `Cmd+Shift+G` (macOS) / `Shift+F3` | `Edit > Find Previous` |

### UAT-XCT-022 — View menu shortcuts

| Shortcut | Action | Notes |
|---|---|---|
| `Ctrl+Shift+D` | `View > Toggle Sidebar` | Qt `Ctrl` → `Cmd` on macOS |
| `Ctrl+Shift+A` | `View > Toggle Markup Toolbar` | |
| `Ctrl+Shift+I` | `View > Toggle Inspector` | |
| `Page Up` | `View > Previous Page` | |
| `Page Down` | `View > Next Page` | |
| `Cmd/Ctrl++` **or** `Cmd/Ctrl+=` | `View > Zoom In` | Two shortcuts bound: `QKeySequence::ZoomIn` (the `+` form) and an explicit `Ctrl+=`/`Cmd+=` so users don't need Shift on US layouts |
| `Cmd/Ctrl+-` | `View > Zoom Out` | `QKeySequence::ZoomOut` |
| `Ctrl+0` / `Cmd+0` | `View > Actual Size` | |
| `Ctrl+1` / `Cmd+1` | `View > Fit to Width` | |
| `` ` `` (backtick) | `View > Magnifier` | Same key on all platforms |

### UAT-XCT-023 — Tools menu shortcuts

| Shortcut | Action | Notes |
|---|---|---|
| `Ctrl+L` | `Tools > Rotate Left` | Qt `Ctrl` → `Cmd` on macOS |
| `Ctrl+R` | `Tools > Rotate Right` | |
| `Ctrl+Shift+3` | `Tools > Take Screenshot` | **Platform: macOS** — Qt maps this to `Cmd+Shift+3`, which is also macOS's native full-screen capture shortcut. The OS typically intercepts it, so the Trailer action may not fire from the keyboard — use the menu item or change the shortcut. Tracked in TODO.md. |

### UAT-XCT-024 — Annotation keys

| Shortcut | Action |
|---|---|
| `Delete` / `Backspace` | Delete selected annotation (or selected page in Sidebar) |
| `Escape` | Cancel inline editor / dismiss search bar |
| `Ctrl+Return` / `Cmd+Return` | Commit inline text editor |

### UAT-XCT-025 — Shortcut disabled states match menu

**Preconditions:** No document open.
**Steps:**
1. Try every doc-specific shortcut above.
**Expected:**
- Shortcuts for disabled menu items do not trigger anything.
- App does not crash.

---

## Dock layout persistence

### UAT-XCT-030 — Dock geometry persists across restart (Known gap)

**Preconditions:** App launched.
**Steps:**
1. Move / resize the Sidebar and Inspector docks.
2. Quit.
3. Relaunch.
**Expected (future):** the dock layout reopens matching the saved
geometry.
**Current:** docks reopen in their default positions (no persistence
yet). This is a known gap — the case exists so we notice when it
lands.

---

## Multi-window behaviour

### UAT-XCT-040 — Open in new window

**Preconditions:** `open_files_in = "new_window"`. One window open with
a PDF.
**Steps:**
1. `File > Open…`, pick another file.
**Expected:**
- Two independent windows exist, each with its own menu bar, tabs,
  Sidebar, Inspector.
- Edits in one window do not affect the other.
- Closing one leaves the other running.

### UAT-XCT-041 — Quit with multiple windows (Known gap)

**Preconditions:** Two windows open, each with a dirty document.
**Steps:**
1. `File > Quit`.
**Expected (future):**
- User is prompted for each dirty document (save / discard / cancel).
- Cancelling any prompt aborts the quit.
- Accepting all saves and quits everything.

**Current:** all windows close unconditionally; edits are discarded
with no warning. Cross-ref UAT-FND-092.

---

## Layout robustness

### UAT-XCT-060 — Layout survives font scaling and RTL

**Preconditions:** A document is open.
**Steps:**
1. (Automated sweep) Realize the main window across a matrix of
   application font sizes and layout directions (LTR / RTL).
**Expected:**
- No visible interactive control collapses to zero size or renders
  narrower/shorter than the size it needs for its content — i.e.
  nothing clips or gets squashed under large system fonts or
  right-to-left mirroring. Driven by `tests/uat/test_uat_sweep.cpp`.

### UAT-XCT-061 — Icon buttons carry accessible names

**Preconditions:** A document is open.
**Steps:**
1. (Automated sweep) Walk every visible interactive button in the main
   window chrome (markup toolbar, main toolbar, side docks).
**Expected:**
- Each has a non-empty accessible name (its own, its action's text, or
  its visible text) — none reads as a bare "button" to a screen reader.
  Driven by `tests/uat/test_uat_sweep.cpp` (audit A-CRIT-1).

---

## Toolbar anchoring & overflow

Adjudicated in
[docs/decision-records/0007-toolbar-anchoring-and-overflow.md](../decision-records/0007-toolbar-anchoring-and-overflow.md)
(Option A, accepted). The main toolbar is a fixed primary row anchored
top-left; the markup and form contextual bars take their own second row;
the trailing search stays visible at every window size; and the built-in
overflow chevron is a fixed size so toggling it never reflows its
neighbours.

### UAT-XCT-070 — Main toolbar anchored top-left; form bar right-aligned; overflow chevron pinned

**Preconditions:** An editable document is open (so the markup and form
toolbars are meaningful). Contextual bars start hidden.
**Steps:**
1. (Automated, geometry-provable — `QT_QPA_PLATFORM=offscreen`) Hold the
   window at a fixed size, record the main toolbar's top-left origin
   with the form toolbar hidden, then show the form toolbar and record
   it again.
2. With the form toolbar shown, read the geometry of its first real tool
   button.
3. Shrink the window to its minimum width and show the markup toolbar
   (the widest contextual bar).
4. Widen the window until the markup bar fits, then narrow it again.

**Expected (the four invariants ADR 0007 establishes):**
- **#1 — origin stable + top row.** Showing the form toolbar does not
  move the main toolbar's top-left origin (same x and y), and the main
  toolbar sits on the top row (minimal y). Before the fix, opening the
  form bar shoved the main toolbar ~183px to the right because it was a
  tenant on the form bar's row. G2 grabs: `xct070_form_hidden.png`,
  `xct070_form_shown.png`.
- **#2 — form buttons right-aligned.** The first real form button sits
  in the trailing half of the form toolbar (a leading expanding spacer
  pushes the tool group against the search-field edge), not left-packed.
- **#3 — widest bar overflows, search stays visible.** At the window
  minimum width the markup toolbar overflows into its
  `qt_toolbar_ext_button` chevron, while the primary row's trailing
  search button stays fully visible inside the main toolbar (never
  collapsed into the main toolbar's own chevron). G2 grab:
  `xct070_narrow_overflow.png`.
- **#4 — chevron pinned + neighbours stable.** The overflow chevron's
  width is a fixed constant (pinned via a class-targeted stylesheet), so
  its neighbours — the leading markup button and the primary search — do
  not move across the overflow appear/disappear transition.

Driven by
`tests/uat/test_uat_search_and_markup.cpp::uat_xct_070_toolbarAnchoringAndOverflow`.

### UAT-XCT-074 — Activating the form toolbar while markup is visible keeps the main toolbar anchored

**Context.** Dogfood report (2026-07-31, macOS nightly): "Activating form
filling toolbar manually when the markup toolbar is visible makes the
markup toolbar go away. It also puts the form filling toolbar at the top
left. So now the main toolbar moves up and to the right instead of just
up and down." Two things reported as one bug are actually two: markup
auto-hiding when form is activated is **deliberate** (mutual exclusion —
different workflows, `MainWindow.cpp`'s `visibilityChanged` connections
either side of the markup/form construction), and is not itself a
defect. The defect is any accompanying position change.

**Preconditions:** An editable document is open. Window held at a fixed
size across the transition.
**Steps:**
1. Show the markup toolbar; record the main toolbar's on-screen origin.
2. Activate the form toolbar (View → Show Form Filling Toolbar, or its
   toolbar button) while markup is still visible.
**Expected:**
- The markup toolbar hides (confirms the deliberate mutual exclusion
  still fires) and the form toolbar becomes visible.
- The main toolbar's origin is **bit-identical** before and after —
  neither the "up" (row) nor the "right" (horizontal tenancy on the
  form bar's row) displacement the report describes occurs.

G2 grabs: `xct074_markup_visible.png`, `xct074_form_activated_markup_hidden.png`.
Driven by
`tests/uat/test_uat_search_and_markup.cpp::uat_xct_074_formActivationWhileMarkupVisibleKeepsMainAnchored`.

### UAT-XCT-075 — A stale persisted windowState blob does not resurrect the pre-ADR-0007 toolbar order

**Context.** `MainWindow::onCurrentDocumentChanged`'s per-file and
per-type view-state restore both call `QMainWindow::restoreState()` on a
`QByteArray` captured by a previous `closeEvent()`'s `saveState()`. That
blob encodes toolbar area **order and row-break placement**, matched
back to our toolbars by object name — a different channel than the
explicit `markupToolbarVisible` bool the same `RecentEntry` /
`DocumentTypeDefault` structs carry. Because none of the three toolbars
are user-movable (`setMovable(false)` / `setFloatable(false)` on all
three — placement is intentional, not user-configurable), there is never
a legitimate reason for a persisted blob to carry a *different* order
than the construction-time one ADR 0007 established — but nothing
stopped it from doing so. A blob saved by an older build (before ADR
0007's fix), or by any future rearrangement, silently overwrites the
canonical order the moment `restoreState()` runs, which resurrects the
"form toolbar shoves main toolbar right" bug from disk on a binary that
already has the construction-time fix — this is why the owner still saw
the bug on the latest nightly. Fixed by
`MainWindow::reassertToolbarLayout()`, called immediately after every
`restoreState()`.

**Preconditions:** A document has a persisted `RecentEntry` whose
`windowState` blob was captured under the pre-ADR-0007 arrangement
(markup toolbar, then form toolbar with a row break before it, then the
main toolbar appended last with **no** break — main a tenant on the form
toolbar's row).
**Steps:**
1. Open that document.
2. Show the form toolbar.
**Expected:** the main toolbar's origin is identical before and after
step 2 — the stale blob's order is overridden by
`reassertToolbarLayout()` regardless of what it encodes. Before the fix,
step 2 moved the main toolbar ~184px right (the same magnitude ADR 0007
describes for the original construction-time bug), reproduced here
purely from persisted state.

G2 grabs: `xct075_stale_blob_after_open.png`, `xct075_stale_blob_form_shown.png`.
Driven by
`tests/uat/test_uat_search_and_markup.cpp::uat_xct_075_staleWindowStateBlobDoesNotResurrectOldToolbarOrder`.

### UAT-XCT-076 — General invariant: toggling any one toolbar never moves another toolbar's actions

**Context.** The single durable regression guard for the owner's stated
principle ("toolbars should always have a reserved location so that when
visibility is toggled they never offset/move other toolbars"), phrased
as a geometry assertion rather than a screenshot diff so it holds
forever, not just for the specific transitions UAT-XCT-070/074/075 name.
Where those cases check the main toolbar's own top-left origin, this
case checks the geometry of the **individual action widgets inside it**
(zoom, rotate, the markup/form toggle buttons, search) — a stricter
check, since a toolbar could keep its own origin while an action inside
it silently reflowed.

**Preconditions:** An editable document is open. Window held at a fixed
size for the whole sweep.
**Steps (all offscreen, geometry-asserted after each):**
1. Record every main-toolbar action widget's on-screen rect with markup
   and form both hidden (baseline).
2. Show markup → assert unchanged. Hide markup → assert unchanged.
3. Show form → assert unchanged. Hide form → assert unchanged.
4. Show markup, then show form (triggers the mutual-exclusion hide of
   markup) → assert unchanged, and assert markup did hide.
5. Show markup again (triggers the mutual-exclusion hide of form) →
   assert unchanged, and assert form did hide.

**Expected:** every main-toolbar action's rect is bit-identical to the
baseline after every one of the five steps above.

Driven by
`tests/uat/test_uat_search_and_markup.cpp::uat_xct_076_toggleAnyToolbarNeverMovesAnotherToolbarsActions`.

### UAT-XCT-077 — Stale windowState blob is also reasserted via the per-type fallback path

**Context.** UAT-XCT-075 covers the per-**file** restore branch
(`RecentEntry`). `onCurrentDocumentChanged` has a second, structurally
parallel branch — the per-**type** fallback (`DocumentTypeDefault`) —
that fires whenever a document has no per-file state of its own but
Trailer has seen another document of the same type before (i.e. most
"first time opening this particular PDF" opens). Both branches call
`restoreState()` then `reassertToolbarLayout()`; this case exercises the
per-type branch directly rather than relying on code-reading to infer
it from UAT-XCT-075, since it is arguably the more common real-world
trigger.

**Preconditions:** No `RecentEntry` exists for the document being
opened; a `DocumentTypeDefault` for PDFs carries a `windowState` blob
captured under the pre-ADR-0007 arrangement.
**Steps:** Open the document; show the form toolbar.
**Expected:** the main toolbar's origin is identical before and after —
same invariant as UAT-XCT-075, proven through the other branch.

Driven by
`tests/uat/test_uat_search_and_markup.cpp::uat_xct_077_staleWindowStateBlobViaPerTypeDefaultAlsoReasserted`.

### UAT-XCT-078 — Status-bar permanent widgets never reflow each other (G10 / SC-CRIT-1)

**Context.** `docs/audit-2026-07-31-g10-deference.md` SC-CRIT-1:
`QStatusBar::addPermanentWidget()` packs every permanent widget into one
right-anchored box layout, so any member's width changing — including a
hide/show collapsing it to or from zero — shifts every OTHER member,
regardless of insertion order. Five widgets in Trailer's status bar toggle
independently (ML scheduler activity, OCR-hint dismissal, Two-Pages mode,
OCR batch progress, missing-model state); the audit's concrete repro is
switching to Two-Pages view while an OCR batch runs, which slid the ML
progress bar's Cancel button — a control the user may be mid-click on —
sideways. Fixed by wrapping each permanent widget in a fixed-width slot
(`reserveStatusBarSlot()`, `src/ui/MainWindow.cpp`) that stays in the
status bar's layout regardless of the widget's own visibility; the two
widest widgets (the large-doc and missing-model OCR hints) share ONE
reserved slot because they are provably mutually exclusive
(`OcrController::evaluateAutoOcrModel`'s `!isLargeDoc()` guard).

**Preconditions:** A document is open; a real, gated OCR batch has
revealed `m_mlProgress` mid-run (Cancel button visible).
**Steps (all offscreen, geometry-asserted after each):**
1. Record the Cancel button's absolute position.
2. Show, then hide, the Two-Pages read-only badge — assert unchanged
   after each.
3. Show, then hide, the ML indicator — assert unchanged after each.
4. Show, then hide, the large-doc OCR hint — assert unchanged after each.
5. Show, then hide, the missing-model hint — assert unchanged after each.
6. Reverse direction: let the OCR batch reach its terminal (completed)
   state, which changes `m_mlProgress`'s own width (the bar and Cancel
   button hide while the completion message shows) — assert the OTHER
   four widgets' positions are unchanged.

**Expected:** the Cancel button's position is bit-identical across every
step in 2–5, and the other four widgets' positions are bit-identical
across step 6. Before the fix, step 2 alone reproduced the audit's named
defect.

G2 grabs: `xct078_before_badge.png`, `xct078_after_badge.png`.
Driven by
`tests/uat/test_uat_ml_affordances.cpp::uat_xct_078_statusBarPermanentWidgetsNeverReflowEachOther`.

### UAT-XCT-079 — Markup toolbar tool actions never reflow on document-type switch (G10 / SC-CRIT-2)

**Context.** `docs/audit-2026-07-31-g10-deference.md` SC-CRIT-2:
`MarkupToolbar::setToolVisible()` hid individual tool `QAction`s
(Underline/Highlight/StrikeOut on `hasTextLayer()`; Instant Alpha/Smart
Lasso on image+SAM eligibility) from `onCurrentDocumentChanged()`. Hiding
an action inside a `QToolBar` collapses its slot, shifting every action
after it — Redact, the SAM separator, Instant Alpha/Smart Lasso, and the
trailing Stroke/Fill/Width/Dash controls all moved when switching between
an OCR'd PDF tab and a plain-image tab. Fixed by
`MarkupToolbar::setToolEnabled()` (disable-with-tooltip, G3, never hide);
see `docs/decision-records/2026-08-01-markup-toolbar-disable-not-hide.md`
for why this supersedes the prior hide-based design.

**Preconditions:** Two documents are open as tabs in one window — one PDF
with a native text layer, one plain image with none.
**Steps (all offscreen, geometry-asserted after each):**
1. On the text-layer PDF tab, record Redact / Stroke / Fill / Width /
   Dash's absolute positions.
2. Switch to the plain-image tab (text-aware trio becomes disabled) —
   assert every recorded position unchanged.
3. Switch back to the PDF tab (text-aware trio re-enabled) — assert
   unchanged again.

**Expected:** every recorded control's position is bit-identical across
both switches. Additionally, the text-aware trio stays `isVisible() ==
true` throughout (disabled, not hidden) and carries a non-empty tooltip
while disabled.

G2 grabs: `xct079_pdf_tab.png`, `xct079_image_tab.png`.
Driven by
`tests/uat/test_uat_search_and_markup.cpp::uat_xct_079_markupToolbarActionsStayPutAcrossDocumentTypeSwitch`.

### UAT-XCT-080 — Search bar's Prev/Next/Close never move as the match count crosses zero (G10 / SC-MOD-1)

**Context.** `docs/audit-2026-07-31-g10-deference.md` SC-MOD-1: `SearchBar`
lays out `[input, stretch=1][counter][prev][next][close]`. The counter
used to `hide()` with no query and `show()` once the query had ≥1 match,
which collapsed/restored its slot in the shared `QHBoxLayout` and shifted
Prev/Next/Close sideways by the counter's width the moment the match
count crossed zero — typing the first matching character, or clearing the
query, moved the buttons under the user's mouse. The audit flagged this
as a candidate finding but deliberately did not file it, pending a check
of whether `claude/mode-switch-and-search-nav` (PR #139) already touched
`SearchBar` layout. That branch's diff was read directly: it adds a
Shift+Enter event filter to `SearchBar::eventFilter()` only — no change to
`setMatchCounter()` or the layout order — so this case was fixed here,
not left to that branch.

**Preconditions:** The Find bar is open on a document.
**Steps (all offscreen, geometry-asserted after each):**
1. Record Prev/Next/Close's absolute positions with no query typed
   (`setMatchCounter(0, 0)`).
2. Set a query with ≥1 match (`setMatchCounter` reporting `total > 0`) —
   assert the three buttons' positions are unchanged.
3. Clear the query back to no matches (`setMatchCounter(0, 0)`) — assert
   unchanged again.

**Expected:** Prev/Next/Close's positions are bit-identical across every
step.

**Verification note.** The integrated UAT above, driven through the real
`MainWindow`-embedded `SearchBar` (which carries `setMaximumWidth(360)`),
does **not** reproduce the pre-fix defect at the app's default window
width: `m_input`'s stretch factor absorbs the counter's width change
within that fixed ceiling, so Prev/Next/Close happen not to move in this
specific embedding/size combination even against the un-fixed code. The
defect is real and was confirmed with a bare, unconstrained `SearchBar`
instance (mirroring `test_markup_toolbar.cpp`'s bare-widget pattern) —
`tests/test_search_bar.cpp`'s
`navButtonsStayPutAsMatchCountCrossesZero`, which lets the widget settle
to its own natural size after each change rather than pinning it to an
external width. That unit test fails against the pre-fix code (Prev
measured moving 66px) and passes with the fix; both tests are kept as
regression guards — the unit test is the one that actually catches a
regression in `SearchBar`'s own layout, the UAT test guards the
integrated embedding.

Driven by `tests/test_search_bar.cpp::navButtonsStayPutAsMatchCountCrossesZero`
(primary regression guard) and
`tests/uat/test_uat_search_and_markup.cpp::uat_xct_080_searchBarNavButtonsStayPutAsMatchCountCrossesZero`
(integrated coverage).

---

## Process lifecycle

### UAT-XCT-050 — Second launch with an arg attaches to running instance (Platform: macOS)

**Preconditions:** Trailer running on macOS.
**Steps:**
1. From another terminal, run `open -a Trailer file.pdf`.
**Expected:**
- The running Trailer opens `file.pdf` via `QFileOpenEvent` in
  [src/app/Application.cpp](../../src/app/Application.cpp) — no second
  `.app` instance spawns. This is provided for free by macOS's app
  bundle launcher.
- File added to Recent.

### UAT-XCT-052 — Second launch on Linux / Windows spawns a new process (Known gap)

**Preconditions:** Trailer running on Linux or Windows.
**Steps:**
1. From another terminal / shortcut, run `trailer file.pdf` (or
   `trailer.exe file.pdf`).
**Expected (future):** the running instance opens the file; no second
window/process spawns.
**Current:** a second Trailer process starts, independent from the
first. There is no IPC / single-instance guard — Trailer has no
equivalent of macOS's `QFileOpenEvent` on these platforms. Cross-ref
TODO.md.

### UAT-XCT-051 — Crash does not corrupt settings (smoke)

**Preconditions:** `settings.toml` contains valid content.
**Steps:**
1. Launch app.
2. Kill the process with `SIGKILL` (or equivalent).
3. Relaunch.
**Expected:**
- `settings.toml` is still valid; the app launches normally.
- Recent files list is intact up to the last persisted write.
- No partial / truncated file is left behind.

---

## Feedback / diagnostic report

`Help > Feedback Report…` — a local-only diagnostic report the owner can
generate while dogfooding and hand to a coding agent or paste into a
GitHub issue. Never transmits anything (PHILOSOPHY.md "No telemetry"):
the report is generated, shown to the user in full, and copied only on
an explicit click. Driven by `tests/uat/test_uat_feedback.cpp` (topical
prefix `uat_fbk_*`, plus two slots that mirror their spec IDs directly —
see docs/uat/README.md's two-naming-axis note).

### UAT-XCT-071 — Feedback Report menu item is always enabled

**Preconditions:** None — any app state, including zero windows/documents.
**Steps:**
1. Open the Help menu with no document open.
2. Open the Help menu with a document open.
**Expected:**
- `Feedback Report…` is present in the Help menu on both platforms'
  command surface (macOS global menu bar; Windows/Linux in-window menu
  bar) and is **enabled** in every case (G3 — there is no "unavailable"
  state to gate on; the report degrades to header + platform info
  instead of the control ever going grey).

Driven by `uat_xct_071_menuItemAlwaysEnabled`.

### UAT-XCT-072 — Report is fully visible and omits full paths by default

**Preconditions:** A document is open.
**Steps:**
1. `Help > Feedback Report…`.
**Expected:**
- The dialog shows the ENTIRE report as read-only text the user can
  scroll and read — not a "click to copy" black box.
- The report carries the stable header (app name, version, commit,
  generated timestamp, build type, platform, Qt version), an App state
  section (theme, auto-save, open-files-in, recent-files count), an ML
  section (scheduler flags + per-model downloaded/never-download state),
  and a Windows/documents section: per window, its outer geometry
  (maximized/fullscreen flag), the screen geometry + devicePixelRatio it
  sits on, and the current document's viewport size; per open document,
  type, page, zoom, view mode, text-layer flag, and natural geometry —
  pixel size + devicePixelRatio for an image (or "not yet decoded" /
  "unknown" while the staged-open decode is still in flight or failed),
  current page size in points for a PDF (2026-07-31, owner request after
  a 504x375-JPEG-at-80%-zoom bug needed a separate `mediainfo` run).
- The "Include full file paths" checkbox is present and **unchecked by
  default**; with it unchecked, no full on-disk path appears anywhere in
  the report — only bare file names — and the report says so explicitly
  ("Full file paths omitted…").

Driven by `uat_xct_072_reportIsFullyVisibleAndOmitsPathsByDefault`.
G2 evidence: `feedback-dialog-with-document.png` (paths off, a document
open) and `feedback-dialog-empty-state.png` (zero documents — the
Windows/Linux empty-state case).

### UAT-XCT-073 — Paths checkbox toggles live; Copy matches the visible text

**Preconditions:** A document is open; the Feedback Report dialog is open.
**Steps:**
1. Check "Include full file paths".
2. Click "Copy to Clipboard".
**Expected:**
- Checking the box immediately re-renders the report text to include the
  document's full on-disk path (no restart/reopen needed).
- The clipboard contents after Copy are byte-identical to what's
  currently shown in the text view — never a stale or different report
  than what the user reviewed.

Driven by `uat_xct_073_checkboxTogglesPathsAndCopyMatchesVisibleText`.
G2 evidence: `feedback-dialog-full-paths-checked.png`.

### UAT-XCT-081 — "Feedback Report…" does not accumulate in the command surface

**Context.** Owner dogfooding, 2026-08-02 (nightly `0.3.1-dev+768.gce56b4b8`,
macOS Retina): the Trailer **application** menu showed **four** identical
`Feedback Report…` items between `Settings…` and `Services`. The attached
diagnostic report listed 2 open windows; 4 MainWindows had been constructed
that session. The list grows over time and closing a window does not
reliably remove its entry.

Cause: the item was built with `QAction::ApplicationSpecificRole`. Every
MainWindow builds its own menu bar, and on macOS that role moves the item
out of the window's Help menu into the single **shared** application menu.
Qt's Cocoa bridge merges the well-known roles (About / Preferences / Quit)
into fixed application-menu slots — which is why those appear once — but
`ApplicationSpecificRole` items are keyed on the per-`QAction`
`QCocoaMenuItem` pointer and appended one per window.

**Preconditions:** None.
**Steps:**
1. Open four windows (`File > New Window` / open four files).
2. Close two of them.
3. Inspect the command surface: on macOS the application menu and the
   Help menu; on Windows/Linux the in-window Help menu.
**Expected:**
- Exactly **one** `Feedback Report…` item is reachable — it lives in the
  **Help** menu on all three platforms (`docs/platform-conventions.md` §2),
  and nothing is promoted into the shared macOS application menu.
- Checkable invariant: across every live window, the count of `QAction`s
  whose `menuRole()` is `ApplicationSpecificRole` is **0**, and each
  window's Help menu holds exactly one `action.help.feedbackReport`.
- The item stays enabled in every state (UAT-XCT-071 is unchanged).

Driven by `uat_xct_081_feedbackItemDoesNotAccumulateAcrossWindows`, plus
`tests/test_menu_placement.cpp` at unit level.
G2 evidence: `docs/uat/images/2026-08-02-help-menu-feedback-linux.png` —
byte-identical before and after the fix, which is the point: `menuRole()`
is macOS-only, so the Windows/Linux command surface is untouched.

**Known coverage limit (stated, not papered over).** `QAction::menuRole()`
does nothing off macOS, and the offscreen harness cannot render a native
Cocoa menu bar, so no test here observes the *merged application menu*
itself. What is asserted is the structural precondition the Cocoa bridge
reacts to: with zero `ApplicationSpecificRole` actions in the tree, the
bridge has nothing to append, so the duplication cannot occur. Confirming
the rendered macOS menu remains an owner/real-Mac check.

---

## UAT-UPD-001 — Help ▸ Check for Updates… is honest about whether this build can update

The update channel verifies a signed feed against a public key **embedded
in the binary**, supplied at configure time (`-DTRAILER_UPDATE_PUBKEY`)
and derived in CI from the signing secret — see
`docs/decision-records/2026-08-02-update-pubkey-from-signing-secret.md`.
A build configured without it (every ordinary local build, PR, and fork)
carries no key and can never verify an update.

**Preconditions:** None.
**Steps:**
1. Launch Trailer and open any document.
2. Open the **Help** menu.
**Expected:**
- `Check for Updates…` is **present in both cases** — never removed, so
  neighbouring items never shift (G10).
- It is **enabled** iff the build carries a key
  (`Update::kUpdateChannelProvisioned`).
- When disabled it carries a tooltip naming the reason ("no
  update-signing key") and where to go (the Releases page), and the
  hosting menu calls `setToolTipsVisible(true)` so that tooltip actually
  renders (G3 — a tooltip nobody can see is not an explanation).

Driven by `uat_helpMenuCheckForUpdatesMatchesProvisioning`; the
tooltips-visible half is swept for every menu by
`uat_fnd_043_everyMenuWithDisabledTooltipActionRendersTooltips`.

G2 evidence (same menu, same window, both states):
`docs/uat/images/2026-08-02-update-help-menu-with-key.png` and
`docs/uat/images/2026-08-02-update-help-menu-no-key.png`.

## UAT-UPD-002 — Preferences ▸ Updates explains a keyless build

**Preconditions:** None.
**Steps:**
1. Open **Preferences ▸ Updates**.
**Expected:**
- The action button reads `Check Now` and occupies the same position in
  both cases; only its enabled state and the status text differ.
- With a key: the ordinary status ladder (`Never checked.` → `Checking…`
  → …) and an enabled button.
- Without a key: the status states the build has no update-signing key
  and points at the Releases page; the button is disabled and carries a
  tooltip (G3).

Driven by `uat_preferencesUpdatesPaneMatchesProvisioning` and
`uat_pref_030_updatesTabReflectsManagerState`.

G2 evidence: `docs/uat/images/2026-08-02-update-prefs-with-key.png` and
`docs/uat/images/2026-08-02-update-prefs-no-key.png`.

## UAT-UPD-003 — the Updates button does not move as the status text changes

The Updates status line legitimately varies in height — one line for
`Never checked.`, two while a check runs (`Checking for updates…` plus the
disclosed URL), three for the no-signing-key message. Before the status
label reserved its tallest height, `Check Now` rode up and down with it,
sliding out from under the pointer at the moment the user clicked it.

**Preconditions:** None.
**Steps:**
1. Open **Preferences ▸ Updates**.
2. Observe the action button's position across each status message the
   pane can display.
**Expected:**
- `Check Now`'s position within the dialog is **identical** for every
  status text (G10, spatial constancy).

Driven by `uat_updatesButtonDoesNotMoveAsStatusTextChanges`, which
compares `mapTo(&dlg, {0,0})` across all four status strings — a geometry
assertion rather than a screenshot, per G10's evidence rule. Verified to
fail against the pre-fix layout.
