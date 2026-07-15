# 0007 — Toolbar anchoring & overflow: fixed primary row + trailing contextual bar with a pinned chevron

- **Status:** proposed
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-15
- **Date accepted / superseded:** —

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

Adopting **Option A** commits these four pixel invariants, each declared per
AGENTS.md G1 and proven by G2 offscreen `QWidget::grab()` under
`QT_QPA_PLATFORM=offscreen` (both toolbar states; `[real-Mac]` confirmation is a
bonus, not required, per the ux-evidence ruling for non-native-chrome layout):

1. **Main-toolbar origin pixel-stable across the form toggle.** `grab()` of the
   form-hidden state and the form-shown state show the main toolbar's top-left
   origin pixel at an *identical* coordinate. Toggling the form (or markup) bar
   must not translate the main toolbar.
2. **Form buttons right-aligned near search.** In the form-shown `grab()`, the
   form toolbar's buttons are right-aligned — their bounding rects sit against
   the trailing edge, adjacent to the search field — via a leading expanding
   spacer mirroring `MainWindow.cpp:806-809`, not left-packed.
3. **Narrow-window overflow into the chevron.** A `grab()` at a deliberately
   narrow window width shows contextual buttons collapsed into the
   `qt_toolbar_ext_button` extension chevron (Qt's documented "too small to show
   all items" path), while the primary row's trailing search stays visible.
4. **Chevron bounding rect invariant under toggle.** The
   `qt_toolbar_ext_button`'s bounding rect is byte-identical in `grab()`s taken
   before and after the chevron's popup is toggled, and adjacent widgets do not
   move — the pin (`setFixedSize` / fixed min-max width) holds on all three
   toolbars.

These ratify the backlog item's declared pass/fail lines
(`docs/backlog/2026-07-13-toolbar-anchoring.md` → *Threshold*) as G2-provable
invariants; the record does not fork or loosen them. Options B and C would each
establish a *weaker* threshold (no fixed-origin guarantee, primary items
overflow-eligible), which is why they are laid out but not the direction this
record carries into adjudication.

## Arbiter verdict + rationale

Empty while status is `proposed` — the implementing session runs the
persona/arbiter cycle.

## Evidence required to reopen

N/A until accepted.
