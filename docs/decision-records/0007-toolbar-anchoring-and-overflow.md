# 0007 — Toolbar anchoring & overflow: fixed primary row + trailing contextual bar with a pinned chevron

- **Status:** accepted
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-15
- **Date accepted / superseded:** 2026-07-15

## Context

Trailer runs a multi-toolbar top area: a slim always-visible **main toolbar**
(sidebar picker, zoom, rotate, markup/form toggles, and a trailing search
field), plus two mutually-exclusive **contextual** bars — the **markup**
toolbar and the **form** toolbar — that appear on demand. The design question
this record settles is *what anchors where* across those bars, and *what the
overflow ("show more") affordance is* so toggling it does not reflow the
layout. It is raised by the v0.3.0 real-Mac dogfood backlog item
`docs/backlog/2026-07-13-toolbar-anchoring.md` and its research theme
(`docs/research/2026-07-13-ux-research-agenda.md` → Theme 2).

**What ships today (so this record isn't misread as describing the target):**
the main toolbar is **not** anchored top-left. The top area is appended in the
order `markup, form, main`, with toolbar breaks placed *before* `markup` and
*before* `form` but **not** before `main`:

- markup toolbar constructed and docked — `src/ui/MainWindow.cpp:275-276`;
- form toolbar docked with its own-row break —
  `addToolBar(Qt::TopToolBarArea, m_formToolbar)` at `src/ui/MainWindow.cpp:336`,
  `insertToolBarBreak(m_formToolbar)` at `src/ui/MainWindow.cpp:341`;
- main toolbar built in `buildMainToolbar()` at `src/ui/MainWindow.cpp:710`,
  docked at `src/ui/MainWindow.cpp:732`, break
  `insertToolBarBreak(m_markupToolbar)` at `src/ui/MainWindow.cpp:733`;
- the main toolbar's trailing search field is pushed right by an expanding
  spacer at `src/ui/MainWindow.cpp:806-809`, followed by the collapse-to-icon
  search button (`src/ui/MainWindow.cpp:811-827`).

Walking that layout: row 1 = `markup`; row 2 = `form` then `main` (there is no
break before `main`, so it shares the form toolbar's row). With markup and form
hidden by default, `main` renders alone at top-left — but **showing the form
toolbar inserts `form` to the left of `main`, shoving the main toolbar
rightward.** That displacement is the reported bug. `main` was appended last and
never given its own row, so it is a tenant on the form toolbar's row; the markup
toolbar is already correct because it owns row 1 alone (via the `:733` break).

The overflow affordance is Qt's built-in `QToolBarExtension` chevron; the
extension chevron is referenced in a comment at
`src/ui/MarkupToolbar.cpp:26-27`, and its Qt objectName is
`qt_toolbar_ext_button`. Qt's documented behaviour: *"When a toolbar is
resized in such a way that it is too small to show all the items it contains, an
extension button will appear as the last item in the toolbar. Pressing the
extension button will pop up a menu containing the items that do not currently
fit in the toolbar."* (https://doc.qt.io/qt-6/qtoolbar.html). The chevron
carries **no** custom size policy or stylesheet in this codebase, so its size
hint tracks its arrow/checked state and the toolbar's `toolButtonStyle` /
`iconSize` — i.e. toggling it can reflow its neighbours. Pinning its size is not
addressed by the Qt class docs (they document that the button appears, not its
own dimensions), so the pin is an app-level responsibility this record commits.

**External grounding.** Apple's HIG *Toolbars* treats the primary toolbar as a
fixed frame element and reserves the **trailing** end for the search field and
an overflow menu: *"The trailing end of a toolbar contains important items that
need to remain available … an optional search field, and the Overflow menu that
reveals hidden items,"* and *"items in the trailing end remain visible at all
window sizes"*; when space is tight, *"the toolbar can display the Search button
in place of the search bar"* — which is exactly Trailer's collapse-to-icon
search (https://developer.apple.com/design/human-interface-guidelines/toolbars,
https://developer.apple.com/design/human-interface-guidelines/search-fields).
Preview.app keeps its primary toolbar in place and surfaces markup as a separate
bar via **View → Show Markup Toolbar**
(https://support.apple.com/guide/preview/annotate-an-image-prvw1501/mac); the
precise "primary stays pixel-fixed while the markup bar takes its own row"
behaviour is **(needs-live-verification)** on a real Mac. Adobe Acrobat's
narrow-window overflow is only anecdotally documented — users report toolbar
buttons dropping off when the window is snapped narrow
(https://community.adobe.com/questions-12/toolbar-too-large-after-update-1504975)
— so Acrobat's exact collapse mechanism is **(needs-live-verification)**; PDF
Expert's second-bar and overflow behaviour is likewise **(needs-live-verification)**
(no first-party layout doc located).

## Options

- **A. Fixed primary row + trailing contextual bar + pinned chevron (backlog
  direction).** Give `main` the top row to itself (build/insert it first, at the
  front of the top area) and place a break before *each* of markup and form so
  neither can share `main`'s row — replicate the `:733` markup wiring for the
  form toolbar. Add a leading expanding spacer at the front of the form toolbar
  (mirroring the main-toolbar spacer at `:806-809`) so the form buttons sit
  **right**, near the search field. Pin the `qt_toolbar_ext_button` chevron to a
  fixed size on all three toolbars (`setFixedSize(...)` or a fixed min/max-width
  stylesheet) so toggling it never reflows neighbours. Matches the HIG
  fixed-primary + trailing-search model and the app's own already-correct markup
  row.
- **B. Single reflowing toolbar.** Collapse everything onto one row that reflows:
  contextual (markup/form) buttons are inserted and removed inline in the primary
  toolbar, and Qt's extension chevron absorbs whatever does not fit. No dedicated
  second row; no fixed origin guarantee — the primary controls shift as
  contextual buttons come and go, and the chevron's size follows its state.
- **C. Unified primary toolbar, contextual tools in the trailing overflow
  (research-surfaced HIG-literal alternative).** Keep exactly one toolbar row;
  contextual markup/form tools are not a separate bar at all but live inline and
  spill into the **trailing Overflow menu** the HIG describes, alongside search.
  Closest to the pure macOS unified-toolbar reading, but abandons the
  second-row "contextual bar" pattern Preview/Acrobat use and the app already
  ships for markup.

## Personas debate

- **Office non-technical user:** Expects the primary controls to stay put; a
  toolbar whose zoom/search jumps sideways when a markup or form bar opens reads
  as the window rearranging itself. Favours **A** — a fixed top-left primary row
  is the "nothing moved" outcome. Under **B**/**C** the shifting primary controls
  are the exact surprise this lens dislikes.
- **Older careful user:** Wants the same control in the same place every time,
  document open or not, bar shown or not. A primary toolbar that keeps its origin
  across a contextual toggle is honest and predictable. Strongly favours **A**;
  **B** (origin drifts on every toggle) is the "it moved on me" failure this lens
  fears most.
- **Power migrator (ex-Preview / Acrobat):** Both reference apps keep a primary
  toolbar in place while a markup/annotation bar appears separately; a
  single-row app that reshuffles the primary controls or buries markup in an
  overflow menu reads as non-native. Favours **A**; treats **C** as
  unfamiliar and **B** as janky. (Reference-app pixel behaviour is
  **(needs-live-verification)**, but the "primary stays, contextual is its own
  bar" shape is the documented HIG convention.)
- **Occasional user:** Rarely toggles the contextual bars; needs the primary
  controls and the search field to be where they were last time and to survive a
  narrow window. Cares only that the trailing search and overflow stay reachable
  (HIG: trailing items *"remain visible at all window sizes"*). Neutral between
  A and C on the second-row question; favours A/C over B's reflow. Has no stake
  in *how* overflow is drawn so long as it does not move things.

## Admissible objections

- **Older careful / office user, Options B and C:** any layout where showing a
  contextual bar (or spilling contextual tools into overflow) moves the primary
  zoom/rotate/search controls fails at the concrete step "I clicked Markup and my
  zoom buttons jumped." This is the decisive argument for A's fixed primary row.
- **Any user, unpinned chevron (all options, today's state):** because
  `qt_toolbar_ext_button` has no fixed size, toggling overflow open/closed
  changes the chevron's own footprint and nudges adjacent widgets — concrete
  failure "the toolbar twitched when I opened the overflow." Admissible against
  every option that leaves the chevron unpinned; A is only admissible **with** the
  pin, which is why the pin is part of A rather than a follow-up.
- **Occasional / power user, narrow window (B):** if the primary controls and
  contextual buttons share one reflowing row, a narrow window can push *primary*
  controls (not just contextual ones) into the chevron, hiding search or zoom
  behind an overflow — violating the HIG rule that trailing items stay visible at
  all sizes. Names a concrete failure ("search disappeared when I narrowed the
  window") that A avoids by keeping the primary row intact and overflowing only
  the contextual row.

### Rejected as naked preference

- "One toolbar is cleaner / more modern." — rejected: states a taste, names no
  user, step, or failure. The admissible cost of one row (drifting primary
  controls, primary items hidden on narrow windows) is captured in the objections
  above.
- "The chevron should look nicer / be a custom widget." — rejected: an aesthetic
  ask with no user-step-failure. The admissible version is the *unpinned-chevron
  reflow*, which is a checkable layout defect, not an appearance preference.

## Checkable threshold this record would establish

Adopting **Option A** commits these four invariants, each declared per AGENTS.md
G1 and proven under `QT_QPA_PLATFORM=offscreen` by **widget-geometry
introspection** (invariants #2/#3/#4, where pixel diffing does not discriminate)
and by `QWidget::grab()` where pixel-identity is meaningful (invariant #1). This
stays within AGENTS.md G2 (offscreen harness introspection) — geometry rather
than pixel diffing where the pixels are identical across the states under test.
`[real-Mac]` confirmation is a bonus, not required, per the ux-evidence ruling
for non-native-chrome layout. The corrected implementation and the invariants:

**Wiring (R1, corrected fix mechanism).** A break "before each of markup and
form" alone does **not** lift `main` to the top row: the top-area append order is
`markup, form, main` (`main` is added **last** at `MainWindow.cpp:732`), so a
break before `main` would create a **third row below**, and `main` gets shoved
**down** when a contextual bar appears — re-breaking invariant #1 rotated 90°.
The correct fix is to move `main` to the **front** of the top-area order:
replace `addToolBar(Qt::TopToolBarArea, m_mainToolbar)` at
`MainWindow.cpp:732` with `insertToolBar(m_markupToolbar, m_mainToolbar)` (or
construct `main` first). Keep the break before markup (`:733`) and before form
(`:341`); place **no** break before `main` (first in area → row 1
automatically). Resulting rows: `main` = **row 1 always**; markup **or** form =
row 2 (mutually exclusive, both hidden by default).

**Chevron pin (R2, corrected mechanism).** In Qt6 the `qt_toolbar_ext_button`
extension is created **eagerly** in the `QToolBar`/`QToolBarLayout` ctor (hidden
until overflow), so `findChild` is non-null at construction — but `setFixedSize`
on the instance is re-imposed by `QToolBarLayout`'s per-relayout `setGeometry`
from `PM_ToolBarExtensionExtent`. Pin the chevron via a **class-targeted
stylesheet** on each toolbar instead:
`QToolBarExtension#qt_toolbar_ext_button { min-width: Npx; max-width: Npx; }`.
This pin is **defensive**: under the default style the chevron width is already
constant across popup open/close (the checked state does not change its rect),
and the real reflow risk exists only if `toolButtonStyle`/`iconSize` changed at
runtime, which the app does not do.

**Window minimum width (R3, new requirement).** There is **no** window minimum
width today (`MainWindow.cpp:106` only resizes; the `:1630` `setMinimumWidth` is
the sidebar). Qt overflows the **trailing-most** items first, and search is the
last widget on the main row — so on a narrow-enough window the **primary search**
would collapse into `main`'s own chevron, violating "trailing items remain
visible at all sizes." Add `setMinimumWidth(...)` on the **main window** ≥ the
primary row's `sizeHint().width()` (or the main toolbar's minimum-content width)
so the primary row never overflows and search is never hidden.

The four invariants in corrected form:

1. **Main-toolbar origin stable + on the top row.** With the **window size held
   constant** while the form (or markup) bar is toggled, the main toolbar's
   top-left origin is unchanged — `grab()`-provable here because the pixels are
   meaningfully identical (resizing between grabs would invalidate the
   comparison, so the size is fixed). Additionally assert `main` sits on the
   **top row** (minimal `y`). Toggling a contextual bar must not translate `main`
   horizontally or vertically.
2. **Form buttons right-aligned near search (geometry).** Assert
   `formToolbar->widgetForAction(firstRealAction)->geometry()` sits against the
   trailing edge, adjacent to the search field — via a leading expanding spacer
   mirroring `MainWindow.cpp:806-809`, not left-packed. Proven by widget
   geometry, not pixel grab.
3. **Widest contextual bar overflows at the window minimum width (geometry;
   R4 reframe).** The form bar (~6 buttons) is **narrower** than the main row, so
   no width overflows form before it overflows main — the old "form buttons
   collapse while search stays visible" invariant is **unsatisfiable** and is
   dropped. Reframed: at the **window's minimum width**, the **widest contextual
   bar** (markup, ~18 widgets) overflows into its `qt_toolbar_ext_button` (assert
   `extension->isVisible()` and a trailing markup action's widget hidden) while
   the primary row's trailing **search stays fully visible** (not in overflow).
4. **Chevron geometry pinned + neighbours stable across the overflow transition
   (geometry; R5 reframe).** The old popup-toggle grab tested a non-risk (opening
   the extension menu only makes the button `checked` with no rect change, and
   the popup is a separate top-level `QMenu` not captured by a window `grab()`).
   Reframed: assert `extension->geometry().size()` equals the **pinned constant**,
   **and** assert neighbour widget rects (the last visible toolbar button and the
   primary search rect) are **unchanged across the overflow appear/disappear
   transition** — resize just below vs just above the overflow threshold.

These ratify the backlog item's declared pass/fail lines
(`docs/backlog/2026-07-13-toolbar-anchoring.md` → *Threshold*) as G2-provable
invariants — via geometry introspection where pixels do not discriminate (R6),
with the added window-minimum-width requirement (R3), the front-insert wiring
(R1), and the stylesheet chevron pin (R2); the record does not fork or loosen
them, and no owner threshold is weakened. Options B and C would each establish a
*weaker* threshold (no fixed-origin guarantee, primary items overflow-eligible),
which is why they are laid out but not the direction this record carries.

## Arbiter verdict + rationale

**Accepted: Option A** (fixed primary top row + own-row contextual bar +
right-aligned form buttons + pinned overflow chevron), **with revisions.**

Option A's *shape* is correct and neither alternative survives the same
objections. Option B (a single reflowing row) drifts the primary controls on
every contextual toggle — the "I clicked Markup and my zoom buttons jumped"
failure — and lets a narrow window push primary search into overflow; Option C
(contextual tools in the trailing overflow) buries markup that the app already
ships as its own bar and still overflows the primary row. Both establish strictly
weaker thresholds (no fixed-origin guarantee, primary items overflow-eligible),
so A is the only direction that satisfies the admissible objections.

But the fix **mechanism** as originally worded is materially wrong, and two of
the four invariants are not provable as conceived. Acceptance is therefore
conditioned on the following revisions, which correct the mechanism and the proof
medium without weakening any owner threshold:

- **R1 (decisive — break wiring is wrong).** Placing a break "before each of
  markup and form" does **not** lift `main` to the top row. The top-area append
  order is `markup, form, main` — `main` is added **last** at
  `MainWindow.cpp:732` — so a break before `main` creates a **third row below**
  and `main` is shoved **down** when a contextual bar appears, re-breaking
  invariant #1 rotated 90°. The correct fix is to move `main` to the **front** of
  the top-area order: replace `addToolBar(Qt::TopToolBarArea, m_mainToolbar)` at
  `:732` with `insertToolBar(m_markupToolbar, m_mainToolbar)` (or construct
  `main` first), keep the breaks before markup (`:733`) and form (`:341`), and
  place **no** break before `main` (first in area → row 1). Any implication that
  breaks-before-markup-and-form suffice is struck.
- **R2 (chevron pin mechanism).** In Qt6 the `qt_toolbar_ext_button` is created
  **eagerly** in the toolbar-layout ctor, but `setFixedSize` on the instance is
  re-imposed by `QToolBarLayout`'s per-relayout `setGeometry` from
  `PM_ToolBarExtensionExtent`. Pin instead via a **class-targeted stylesheet** on
  each toolbar (`QToolBarExtension#qt_toolbar_ext_button { min-width: Npx;
  max-width: Npx; }`). The pin is **defensive** — under the default style the
  chevron width is already constant across popup open/close; the real reflow risk
  only exists if `toolButtonStyle`/`iconSize` changed at runtime, which the app
  does not do.
- **R3 (trailing-search protection).** There is **no** window minimum width
  today, and Qt overflows the trailing-most items first, so a narrow-enough
  window would collapse the **primary** search into `main`'s own chevron —
  violating "trailing items remain visible at all sizes." Add `setMinimumWidth`
  on the **main window** ≥ the primary row's `sizeHint().width()` so the primary
  row never overflows.
- **R4 (invariant #3 reframe).** The form bar (~6 buttons) is **narrower** than
  the main row, so nothing overflows form before main — the "form buttons
  collapse while search stays visible" invariant is **unsatisfiable**. Reframed:
  at the window's minimum width, the **widest** contextual bar (markup, ~18
  widgets) overflows into its `qt_toolbar_ext_button` while the primary row's
  trailing search stays fully visible. The "form buttons" specificity is dropped.
- **R5 (invariant #4 reframe).** The popup-toggle grab tests a non-risk: opening
  the menu only makes the button `checked` (no rect change), and the popup is a
  separate top-level `QMenu` not captured by a window `grab()` — the before/after
  grab is trivially identical and constrains nothing. Replaced by: assert
  `extension->geometry().size()` equals the pinned constant, **and** neighbour
  rects (last visible button + primary search) are unchanged across the overflow
  **appear/disappear** transition (resize just below vs just above the threshold).
- **R6 (proof medium).** Invariants #2/#3/#4 are **geometry-provable** via widget
  introspection (`widgetForAction(...)->geometry()` for right-alignment;
  `extension->isVisible()` + a trailing action's widget hidden for overflow;
  `extension->geometry()` for the pin), **not** reliably pixel-grab-provable (an
  icon-only chevron renders identically across widths; offscreen font jitter
  perturbs pixel hashes). Invariant #1 (main origin) stays grab-provable — but
  only with the **window size held constant** while toggling — and additionally
  asserts `main` is on the top row (minimal `y`). This remains within AGENTS.md
  G2 (offscreen harness introspection), just geometry rather than pixel diffing
  where the pixels do not discriminate.

## Evidence required to reopen

A reproducible case where the **fixed primary row + own-row contextual bar**
harms a real user at a real step — e.g. the vertical space cost of the dedicated
contextual row demonstrably regresses a documented workflow — together with owner
sign-off; or a superseding decision record. A bare taste for "one cleaner row" is
not such evidence; it is already rejected above as a naked preference, its
admissible cost (drifting primary controls, primary items hidden on narrow
windows) captured by the objections Option A answers.

## Addendum (2026-07-31) — persisted `windowState` could resurrect the pre-fix order

Dogfood report (owner, macOS nightly, 2026-07-31): activating the form toolbar
while the markup toolbar was visible still moved the main toolbar "up and to the
right," despite the R1 construction-time fix above already being on `main` since
2026-07-15 (`4ddabc2`). Investigation found the construction-time order was
correct, but **invariant #1 was not actually held**, because of a second channel
this record did not account for: `MainWindow::onCurrentDocumentChanged`'s
per-file and per-type view-state restore paths call
`QMainWindow::restoreState(entry.windowState)` / `restoreState(def.windowState)`
on a `QByteArray` captured by a previous `closeEvent()`'s `saveState()`.
`QMainWindow::saveState()`/`restoreState()` serialise the **toolbar area's order
and row-break placement**, matched back to toolbars by object name — a different
channel than the explicit `markupToolbarVisible` bool the same structs also
carry (which the restore code already re-applies *after* `restoreState()`, per
the comment at the call site — but only for visibility, not order). A blob
captured under an older arrangement — including, concretely, any blob saved by
a build that predates this record's R1 fix — silently overwrites the
construction-time canonical order the instant `restoreState()` runs, on every
subsequent build regardless of how correct that build's constructor is. Because
none of the three toolbars are user-movable/floatable (`setMovable(false)` /
`setFloatable(false)` on all three — placement is intentional, not
user-configurable), the blob never has a legitimate reason to carry a different
order, so there is no tradeoff in overriding it unconditionally.

Reproduced and fixed in the same change: `tests/uat/test_uat_search_and_markup.cpp`
`uat_xct_075_staleWindowStateBlobDoesNotResurrectOldToolbarOrder` plants a blob
saved under the pre-R1 arrangement (markup, form+break, main with no break) as a
document's persisted `RecentEntry::windowState` and shows the main toolbar
jumping ~184px right when the form toolbar is shown — the same magnitude as the
original bug, resurrected purely from disk. Fixed by
`MainWindow::reassertToolbarLayout()` (`src/ui/MainWindow.cpp`), called
immediately after both `restoreState()` call sites: it snapshots each
toolbar's current visibility, re-runs the exact same `addToolBar` /
`insertToolBar` / `insertToolBarBreak` sequence the constructor uses, then
reapplies the snapshotted visibility — so ORDER is always the canonical one
from this record regardless of what any blob (past, present, or future) encodes,
while VISIBILITY continues to come from whatever the caller set it to. A second,
paired case (`uat_xct_074_formActivationWhileMarkupVisibleKeepsMainAnchored`)
confirms the live, no-persisted-state transition the owner also described —
markup visible, then form activated manually — was already correct on `main`;
only the persisted-state channel was the residual gap. This addendum documents
the completion of invariant #1 as originally accepted; it does not reopen or
change the accepted verdict, and needs no separate decision record (AGENTS.md
G6) because it closes a gap in fulfilling an already-accepted threshold rather
than establishing a new one.
