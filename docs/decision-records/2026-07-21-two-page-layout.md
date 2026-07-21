# Decision record: Two-page (facing) layout — thresholds and architecture

<!--
This record uses the date+slug naming scheme (docs/decision-records/YYYY-MM-DD-<slug>.md),
the same scheme as docs/backlog/, to avoid parallel-branch ADR-number collisions.
Refer to it by slug/date, not a number. It follows TEMPLATE.md and the process in
PHILOSOPHY.md → "How design decisions get adjudicated".
-->

- **Status:** accepted <!-- proposed | accepted | superseded-by <YYYY-MM-DD-slug> -->
- **Arbiter:** viewer-rendering (agent role; the owner, programmerq, is the escalation-only override)
- **Date proposed:** 2026-07-21
- **Date accepted / superseded:** 2026-07-21 — the coordinator ratified rulings D1–D4 (below) ahead of implementation so PR0 could declare a checkable G1 threshold; the owner retains the escalation-only veto and may reopen any clause of this record at review.

## Context

Trailer today renders PDFs through `QPdfView` in Single-page and Continuous
modes. There is **no facing-page ("two-up") mode**: `QPdfView` has no two-up
`QPdfView::PageMode`, so a real side-by-side spread cannot come from a
`QPdfView` toggle — it needs a custom layout/paint layer. This is the
long-standing roadmap item tracked in
[`docs/backlog/2026-07-12-real-two-page-layout.md`](../backlog/2026-07-12-real-two-page-layout.md),
whose threshold was explicitly left "TBD — declare the concrete acceptance line
(which page pairings, cover-page handling, scroll behaviour) before work begins."
This record supplies that acceptance line so gate **G1** (threshold declared
before work begins) is satisfied for the whole feature; the view itself ships in
a later increment (PR1).

This is a user-visible behaviour: a new "Two Pages" mode that changes what the
user sees on screen. Per PHILOSOPHY → *Every work item carries a checkable
threshold* and gate G1, the pass/fail line is fixed here before the rendering
code is written. Per gate G3 (*No lying controls*) the enablement rule for the
new toggle is also pinned. Relevant references: DESIGN §5.4 (view modes),
PHILOSOPHY → *Platform-native per OS* and *No lying controls*, and gates
G1/G2/G3/G4 in `AGENTS.md`.

**What ships today (so this record is not misread as describing the present):**

- Only Single-page and Continuous view modes exist, both driven by `QPdfView`.
- No "Two Pages" command exists anywhere in the command surface.
- The zoom-% readout reflects the per-page zoom factor `QPdfView` applies.

## Options

The genuinely open choices, each stated as options for the arbiter to pick.

**D1 — Architecture of the two-up surface.**
- **A. AUGMENT.** Add a custom `TwoPageView` used **only** in Two-Pages mode;
  `QPdfView` keeps driving Single and Continuous. Two rendering surfaces
  coexist, selected by mode.
- **B. REPLACE.** Retire `QPdfView` and drive all three modes (Single,
  Continuous, Two-Pages) from one custom surface.

**D2 — Feature completeness of the first shipping two-up increment.**
- **A. HONEST DEGRADATION.** In the first shipping view increment,
  markup/annotation, text-selection, and search-highlight may be
  **disabled-with-tooltip** in two-up mode, with full parity committed as a
  **tracked follow-up (PR2)**, not optional.
- **B. FULL PARITY UP FRONT.** Two-up mode ships only once overlay, selection,
  and search all work there.

**D3 — Which two-up scroll model ships first.**
- **A. CONTINUOUS-first.** The first shippable two-up mode is continuous spread
  scroll (macOS Preview "Two Pages" shape) — spreads stacked vertically, scroll
  through them.
- **B. PAGED-first.** A paged spread flip (one spread at a time) ships first.

**D4 — The concrete threshold (layout, pairings, zoom, DPR, enablement).**
- Ratify the specific pass/fail clauses in *Checkable threshold* below as
  written, versus leaving any of them TBD.

## Personas debate

Each persona is an unranked adversarial lens (DESIGN §2.5.2). §2.5.3 names the
office non-technical user and the older careful user as the coverage floor; all
four are covered here.

- **Office non-technical user** (Windows-primary, PDFs daily, "Will this break
  my file? Where did my changes go?"): Does not care how many rendering surfaces
  exist (**D1**), only that the mode they picked shows two pages and nothing they
  did to the file changed. The sharp lens on **D2**: if markup is quietly
  unavailable in two-up mode with *no explanation*, they will click the markup
  tool, see nothing happen, and conclude "it's broken." A disabled control with a
  tooltip that says where to go ("switch to Single or Continuous to mark up") is
  the honest form. On **D4 zoom**, expects "Actual Size" to mean the page is its
  real size, not "the spread got shrunk to fit my window" — a spread that
  silently shrinks reads as "it zoomed out on its own."
- **Older careful user** ("I need to know when it saves", prefers explicit,
  double-checks): The decisive lens on **D2 honest degradation** and **G3**. This
  user needs the app to *tell* them when something is off, never to silently swap
  behaviour. A markup tool that is present-but-inert with a clear reason is
  acceptable to them precisely because it is honest; a markup tool that appears to
  work but drops their annotation is a betrayal. Also the lens on **D4
  enablement**: a "Two Pages" toggle that is enabled on a single-page document and
  then does nothing visible is exactly the lying control they distrust.
- **Power migrator** (from Preview/Acrobat, strong muscle memory, "Where is the
  equivalent?"): The reason **D3-A (continuous-first)** is chosen — Preview's
  "Two Pages" is a continuous facing-page scroll, and this user expects the same
  shape. Cares about **D4 pairings**: in Preview a book opens with the cover
  alone and then facing pairs (2,3),(4,5); getting the parity wrong (cover paired
  with page 2) is immediately jarring to anyone who reads books or magazines.
  Expects the zoom-% readout to keep meaning the true per-page zoom across modes
  (**D4 zoom**), not to silently redefine itself as a spread-fit percentage when
  they switch to two-up.
- **Occasional user** (opens the app every few weeks, has forgotten everything):
  Low stake on **D1/D3**. The lens on **D4 enablement** for images: they may hit
  "Two Pages" on a single photo; a disabled toggle with "images don't have pages
  to face" teaches them why in one line, where a no-op or a popup-that-says-no
  would just confuse. Neutral on the overlay-parity timing so long as nothing
  claims to work and then doesn't.

## Admissible objections

Objections that name a user, a step in a real flow, and the failure hit there
(the PHILOSOPHY §"admissible-objection test" bar).

- **Office/older user, D2-A degradation, the markup step, silent inert control.**
  If two-up mode ships with markup unavailable but the markup tools stay
  *enabled-looking*, the user selects a tool, drags on the page, and nothing
  happens — the classic "popup that just says no" / silent-inert failure
  PHILOSOPHY forbids. This drives the degradation to be **disabled-with-tooltip**
  (G3), not merely "markup doesn't work in two-up." The tooltip names where to go:
  "switch to Single or Continuous to mark up."
- **Power migrator, D4 pairings, opening a book, wrong facing parity.** A
  migrator opens a scanned book in two-up expecting the cover alone then (2,3),
  (4,5). If the app paired (1,2),(3,4) by default, every spread is off by one and
  the recto/verso reading order is broken — a concrete, immediately visible
  failure for anyone reading paginated content. This drives **cover-alone ON as
  the default** for book-like documents, with an OFF path for documents that
  genuinely start facing at page 1.
- **Office/migrator, D4 zoom, hitting "Actual Size" in two-up, silent redefinition.**
  If "Actual Size" in two-up meant "fit the whole spread in the window," a user
  who hits Actual Size (or reads the zoom-% readout) sees a number that no longer
  means what it means in Single mode — the readout lies across modes. This drives
  **"Actual Size" = 1 PDF point → 1 logical pixel per page**, and the zoom-%
  readout staying the *true per-page zoom factor* in all three modes.
- **Older user, D4 enablement, single-page doc, lying toggle.** A "Two Pages"
  toggle enabled on a one-page PDF or on an image, that then shows nothing to
  face, is a lying control (G3). This drives the enablement rule: enabled **only**
  for multi-page PDFs; disabled-with-tooltip for images ("images don't have pages
  to face") and single-page documents ("this document has only one page").

### Rejected as naked preference

- "REPLACE `QPdfView` outright — one surface is cleaner." — rejected as the *now*
  decision: states an architectural taste, names no user/step/failure that AUGMENT
  fails to serve. Replacing the proven `QPdfView` Single/Continuous paths wholesale
  before the custom two-up surface is even shipped risks regressing two working
  modes to gain nothing a user can see today. Recorded as a **possible future
  unification** (see verdict), not a naked-preference reason to do it now.
- "Ship paged two-up first, it's simpler to render." — rejected as stated: an
  implementer-convenience preference, not a user failure. The migrator lens names
  the concrete expectation (Preview's continuous shape), which decides D3 the
  other way.
- "Just make markup work in two-up before shipping anything." — rejected as a
  blocking preference: it names no failure of the degraded-but-honest increment,
  and the parity work is *committed* (D2-A + PR2 backlog item), not dropped. Gate
  G4 (no feature dropped per-OS) is about platforms, not increments; shipping an
  honest, tooltip-gated subset with a tracked parity follow-up satisfies the
  no-lying-controls floor.

## Checkable threshold this record establishes

The two-page-layout feature is **Done** (UX-Done, gates G1/G2/G3) when all of
the following pass; each is independently checkable by an agent or reviewer.
PR0 (this record + the pure pairing function) declares these; the view
increment (PR1) meets the layout/zoom/DPR/enablement clauses; overlay/search/
selection parity is the committed PR2 follow-up.

1. **Layout.** A real two-page (facing) layout renders **two pages side by
   side**. The first shippable mode is **CONTINUOUS spread scroll** — facing
   spreads stacked and scrolled, matching macOS Preview's "Two Pages" shape
   (**D3-A**). (Paged spread-flip is explicitly out of scope for the first
   increment.)
2. **Pairings.** With **cover-alone ON** (the default for book-like documents),
   page 1 (the cover) renders **ALONE**, then facing pairs **(2,3),(4,5),…**.
   With **cover-alone OFF**, pairs are **(1,2),(3,4),…**. In either mode a
   **trailing unpaired page renders alone**. This pairing rule is implemented by
   the pure `trailer::spreadsFor(int pageCount, bool coverAlone)` function in
   [`src/document/SpreadLayout.h:35`](../../src/document/SpreadLayout.h) /
   [`src/document/SpreadLayout.cpp:5`](../../src/document/SpreadLayout.cpp)
   (single-page / trailing-page slot encoded as `Spread{left, 0}`), and is pinned
   exhaustively for page counts 0–7 × cover-alone {on, off} in
   [`tests/test_spread_layout.cpp`](../../tests/test_spread_layout.cpp).
3. **Zoom truthfulness.** **"Actual Size" means each page is rendered at 1 PDF
   point → 1 logical pixel** (NOT "the spread fits the window"). The zoom-%
   readout stays the **true per-page zoom factor** across Single, Continuous, and
   Two-Pages modes — the same 100% means the same physical page size in every
   mode.
4. **DPR.** The layout holds correctly at **devicePixelRatio 1, 1.5, and 2**;
   pages are rendered at **pts × zoom × dpr** device pixels and laid out at
   pts × zoom logical units, so a HiDPI display shows a crisp spread with the
   same logical geometry as a 1× display.
5. **Enablement (G3, no lying controls).** The **"Two Pages" toggle is enabled
   only for multi-page PDFs**. It is **disabled-with-tooltip** for images
   (tooltip: "images don't have pages to face") and for single-page documents
   (tooltip: "this document has only one page"). No enabled-but-inert toggle, no
   popup-that-says-no.
6. **Degradation + committed parity (D2-A).** The first shipping two-up
   increment **may** ship with markup/annotation, text-selection, and
   search-highlight **disabled-with-tooltip** in two-up mode (tooltip: "switch to
   Single or Continuous to mark up"). Full overlay/search/selection **parity in
   two-up mode is a COMMITTED follow-up**, tracked as PR2 in
   [`docs/backlog/2026-07-21-two-page-overlay-search-parity.md`](../backlog/2026-07-21-two-page-overlay-search-parity.md)
   — not optional, not "maybe later."

"Vibes" pass/fail is excluded: clause 1 is an observable side-by-side render,
clause 2 is a pure-function enumeration already unit-tested, clause 3 is a
pixel-per-point measurement plus a readout comparison across modes, clause 4 is
a device-pixel measurement at three DPRs, clause 5 is an enabled/disabled +
tooltip check, and clause 6 is a disabled-with-tooltip check plus the existence
of the tracked PR2 item.

## Arbiter verdict + rationale

Status is `accepted` (coordinator-ratified D1–D4; owner escalation-only veto
retained). The viewer-rendering arbiter's resolutions:

- **D1 → Option A (AUGMENT).** Add a custom `TwoPageView` used **only** in
  Two-Pages mode; `QPdfView` keeps driving Single and Continuous. `QPdfView`
  has no two-up `PageMode`, so two-up genuinely needs a custom surface — but the
  Single/Continuous paths already work and regressing them buys nothing a user
  can see. **Non-goal / possible future unification (explicit):** once the custom
  two-up surface is proven, Trailer *may* later unify all three modes onto one
  custom surface and retire `QPdfView` (Option B). That is a **deliberate
  non-goal for now**, recorded so it is a future decision with its own record and
  threshold, not something this record forecloses or greenlights.
- **D2 → Option A (HONEST DEGRADATION with committed parity).** Driven by the
  office/older silent-inert-control objection. The first increment may ship with
  markup, text-selection, and search-highlight **disabled-with-tooltip** in two-up
  mode, satisfying G3 (the controls are honest about being unavailable and say
  where to go). Full parity is **committed**, not optional — tracked as PR2 in
  `docs/backlog/2026-07-21-two-page-overlay-search-parity.md`. Option B
  (full-parity-up-front) is rejected as a blocking preference: it names no failure
  of the honest-subset increment and would stall a shippable, truthful two-up mode
  behind overlay-reprojection work.
- **D3 → Option A (CONTINUOUS-first).** Driven by the power-migrator
  Preview-parity objection. Preview's "Two Pages" is a continuous facing-page
  scroll; matching that shape first meets the strongest muscle-memory expectation.
  Paged-first (Option B) is rejected as implementer convenience with no user
  failure named.
- **D4 → ratify the threshold as written.** Driven by the pairings, zoom, and
  enablement objections: cover-alone-ON default (migrator book-parity), Actual
  Size = 1pt→1px with a mode-stable zoom readout (office/migrator zoom-honesty),
  and enabled-only-for-multi-page-PDF with disabled-with-tooltip otherwise
  (older-user lying-control). Each clause is phrased as an observable pass/fail
  above.

Which objections drove it: the silent-inert-control objection (office/older)
decides D2 for honest-degradation-plus-committed-parity; the book-parity
objection (migrator) fixes cover-alone-ON as the default; the zoom-honesty
objection fixes Actual Size and the mode-stable readout; the lying-toggle
objection fixes enablement; the Preview-shape objection (migrator) decides D3 for
continuous-first. Naked-preference items (REPLACE-now, paged-first,
parity-blocks-ship) carry no weight, as recorded. This record **satisfies the G1
threshold for** `docs/backlog/2026-07-12-real-two-page-layout.md`; that backlog
item is **not** closed here — its threshold is only fully met when the view
ships (PR1), so it is deleted in the PR that meets it, not in PR0.

## Evidence required to reopen

Once accepted, reopening requires a concrete, checkable problem not on the table
here, plus owner sign-off:

- A demonstrated `QPdfView` two-up `PageMode` (or a Qt release that adds one)
  that renders facing pages without a custom surface — would reopen **D1**.
- A measured regression where AUGMENT's two coexisting surfaces produce a
  user-visible inconsistency (e.g. zoom-% diverging between modes on the same
  document) that a single unified surface would not — would reopen **D1** toward
  the future unification.
- A named, concrete user flow where the disabled-with-tooltip degradation
  (markup/selection/search unavailable in two-up) causes a data-loss or
  can't-complete-the-task failure the tooltip does not honestly cover — would
  reopen **D2** and/or reprioritise PR2.
- A Preview/Acrobat behaviour change, or a named user flow, showing continuous
  two-up is the wrong first shape — would reopen **D3**.
- A concrete document class where the cover-alone-ON default or the 1pt→1px
  Actual-Size definition is user-visibly wrong — would reopen the relevant **D4**
  clause.

Naked disagreement ("REPLACE would be cleaner", "should have shipped paged
first", "parity should have blocked ship") is not superseding evidence.
