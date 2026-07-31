# Zoom-% status-bar readout becomes transient (shows on change, fades, hidden otherwise)

- **Status:** accepted
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-31
- **Date accepted / superseded:** 2026-07-31 (accepted)

## Context

Owner directive (verbatim, while reviewing the zoom-on-open/window-size
fix in this same PR):

> "The zoom level is always displayed in the status bar. It should only
> display temporarily when changing the zoom level and then fade. I
> don't need to see `100%` constantly. The app should make the
> file/image/document be the focus, not arbitrary internals."

**What ships today on `main` before this change:** `m_zoomIndicator`
(`src/ui/MainWindow.cpp`, a permanent `QLabel` added via
`statusBar()->addPermanentWidget()`) is visible for the *entire* time a
zoomable document is open, updated by `MainWindow::updateZoomIndicator()`
from every zoom-action trigger, the doc-open/doc-switch path, and the
async initial-fit-landing path (`onDocumentCapabilitiesChanged`). This is
exactly the "permanent chrome for an ambient value" pattern
`docs/ux-guidelines.md` argues against (*"Prefer in-context affordances
over chrome"*, *"the document is the main focus; the UI recedes"*) —
the owner's directive above is a direct application of that existing
principle, not a new one.

## Options

- **A — Status quo (permanent).** Rejected: the reported friction, and
  contrary to `docs/ux-guidelines.md`'s standing "dialog/chrome →
  in-place, minimal, dismissable" direction.
- **B — Remove the status-bar readout entirely; percent is available
  only via the View menu's checkable zoom actions.** Rejected: this
  strands the information — a user who wants to *confirm* "am I at
  100% or did that pinch leave me at 97%" has no fast, low-friction way
  to check without opening a dialog. `docs/ux-guidelines.md`'s "quick
  self-check" explicitly asks "can this be a glyph/badge/tooltip ...
  instead of new chrome," implying *some* on-demand path must survive,
  not that the information disappears.
- **C — Transient: show on an explicit zoom change, hold briefly, fade
  out; stay hidden the rest of the time. On-demand answer via updated
  tooltips on the zoom toolbar/menu actions (Zoom In/Out/Fit
  Page/Actual Size/Fit to Width), which always state the current
  percent even while the status-bar label itself is hidden.** Matches
  the owner's literal request ("temporarily ... then fade") while
  keeping a genuine, always-reachable route back per the ux-guidelines
  self-check.

## Personas debate

- **Office non-technical user:** doesn't want a number sitting in the
  corner of every session: a HUD-style flash on an explicit zoom action
  (matching how macOS itself shows a transient volume/brightness
  overlay) reads as familiar, not surprising. (Favours C.)
- **Older careful user:** wants reassurance "did my zoom command take
  effect?" — the flash on trigger answers that immediately; the tooltip
  on every zoom control (available without triggering anything) answers
  "what am I at right now?" without requiring the user to remember a
  gesture. (Favours C, contingent on the tooltip path being real —
  addressed below.)
- **Power migrator:** Preview/Acrobat do not show a permanent zoom
  percentage in their status bar either; a transient HUD is closer to
  platform-native muscle memory than Trailer's prior permanent label.
  (Favours C.)
- **Occasional user:** may forget the flash happened by the time they
  look; needs the tooltip fallback to still answer the question days
  later with zero memory of "how do I check my zoom." (Favours C,
  same contingency.)

## Admissible objections

- **Occasional user, wants to check current zoom with no recent zoom
  action, status bar shows nothing — the core "stranded information"
  risk:** resolved by keeping the on-demand path real: every zoom
  QAction's tooltip (`Zoom In`, `Zoom Out`, `Actual Size`, `Fit Page`,
  `Fit to Width`) is kept current with the live percent
  (`refreshZoomActionTooltips()`, called from the same
  `updateZoomIndicator()` that maintains the status-bar text), and
  `viewMenu->setToolTipsVisible(true)` already means hovering any View
  menu zoom entry — or the corresponding main-toolbar button — shows it.
  No dialog, no extra chrome; this is the same control the user already
  reaches for to change the zoom.
- **Older/office user with the OS Reduce Motion setting on, expects no
  new animation to appear — accessibility no-regression floor
  (AGENTS.md, docs/accessibility-checklist.md row A6):** resolved by
  `trailer::platform::prefersReducedMotion()` gating the fade: when
  true, the readout hides instantly at the end of its visible hold
  instead of animating opacity. Checked on macOS via
  `NSWorkspace.accessibilityDisplayShouldReduceMotion` (the real
  system toggle); Windows/Linux fall back to a documented best-effort
  Qt animation-enabled proxy (see `src/platform/ReducedMotion_stub.cpp`
  for the stated limitation) rather than silently assuming motion is
  always fine off-Mac.
- **Any user, opens a file and the readout flashes immediately even
  though they took no zoom action — noise on every single open:**
  resolved by design: doc-open, doc-switch, and the async initial-fit
  landing all call `updateZoomIndicator(/*reveal=*/false)` — text/
  tooltips stay accurate, but the transient reveal-and-fade sequence
  fires only from the five explicit zoom-action call sites (Zoom
  In/Out/Fit Page/Actual Size/Fit to Width triggers). A resize-driven
  re-fit (live window drag in a fit mode) is classified the same as
  doc-open — silent — since DESIGN does not ask for a live-tracking HUD
  during a drag and the existing PDF-side gap
  (`docs/backlog/2026-07-25-pdf-zoom-readout-resize-staleness.md`)
  already treats resize-driven readout refresh as an open, separate
  question; this record does not change that.

### Rejected as naked preference

- "A permanent readout is more reassuring, don't touch it." — rejected:
  the owner's directive is the dispositive, explicit request driving
  this record, and the personas above independently converge on the
  same direction docs/ux-guidelines.md already establishes.

## Checkable threshold this record would establish

1. Triggering any of the five zoom actions (Zoom In, Zoom Out, Fit
   Page, Actual Size, Fit to Width) makes `m_zoomIndicator` visible at
   full opacity immediately, showing the correct `qRound(zoomFactor *
   100)` percent.
2. With Reduce Motion **off**, the label fades (animated opacity 1→0)
   starting `kZoomIndicatorVisibleMs` after the triggering action (no
   further zoom action in between), finishing hidden after the fade
   duration.
3. With Reduce Motion **on** (`prefersReducedMotion() == true`), the
   label hides instantly at the same `kZoomIndicatorVisibleMs` mark —
   no opacity animation runs.
4. Opening a document, switching tabs, or the async initial-fit landing
   never makes the label visible if it was already hidden (silent
   update only).
5. At all times — visible, fading, or hidden — `Zoom In`/`Zoom Out`/
   `Actual Size`/`Fit Page`/`Fit to Width`'s `QAction::toolTip()`
   contains the current `qRound(zoomFactor * 100)` percent.
6. (G10) No other status-bar (or any other) control's `pos()` changes
   as a side effect of the readout revealing or fading.

Covered by `uat_zoom_ind_040_explicitZoomActionRevealsAndFades`,
`uat_zoom_ind_050_docOpenAndSwitchStaySilent`,
`uat_zoom_ind_060_reduceMotionSkipsFade`, and
`uat_zoom_ind_070_togglingReadoutDoesNotShiftSiblingWidgets` in
`tests/uat/test_uat_zoom_indicator.cpp` (fake/short timing via the
test-only `MainWindow::setZoomIndicatorTimingForTesting()` seam, not
real sleeps).

## Arbiter verdict + rationale

**Option C is adopted.** It is the direct, literal implementation of the
owner's directive, resolves the one admissible "stranded information"
objection with an existing, zero-new-chrome mechanism (tooltips on
controls the user already uses to change zoom), and honours Reduce
Motion via a real platform query on macOS (the reporting owner's own
platform) with an honestly-limited, documented fallback elsewhere. The
`kZoomIndicatorVisibleMs` / `kZoomIndicatorFadeMs` constants are
hand-tuned (PHILOSOPHY → *Hand-tuned values stay hand-tuned*); their
in-code rationale comment lives at the constant definitions in
`MainWindow.cpp`.

### G10 addendum (2026-07-31, same day): the readout is a floating overlay, not a status-bar widget

Gate G10 (AGENTS.md, "Deference and spatial constancy") landed the same
day as this record, naming this exact readout as one of the concrete
violations that motivated it. The **first** implementation of Option C
added `m_zoomIndicator` as a `QStatusBar::addPermanentWidget()` entry,
placed last (rightmost) among the permanent status-bar widgets on the
theory that nothing positioned after it means nothing shifts when it
reveals/hides.

That theory was **empirically wrong**: Qt's `QStatusBar` keeps its whole
permanent-widget row right-anchored as a single block. When any member's
width changes — including the *last* one growing from 0 (hidden) to its
text width (revealed) — the block's left edge moves to accommodate the
new total width, which shifts **every** member, including ones
positioned *before* the resized one. A regression test written against
this design (`uat_zoom_ind_070_togglingReadoutDoesNotShiftSiblingWidgets`)
caught it directly: a sibling badge measured moving ~41px when the
readout revealed.

**Fix:** `m_zoomIndicator` is a widget parented directly to `MainWindow`
(not to `statusBar()`), positioned by explicit `move()` calls in
`repositionZoomIndicator()` (bottom-right of the document area, called
on construction and on every `resizeEvent()`), and never inserted into
any shared box layout. It therefore cannot participate in any layout
reflow with another control — its own visibility change is structurally
incapable of moving a sibling, not merely arranged not to by insertion
order. This also settles G10's "reserved-but-blank slot" trap explicitly:
this is not a permanently-reserved status-bar slot that goes blank;
hidden means zero footprint anywhere in the window, exactly as if it
were never constructed.

This addendum does not change the Options/Personas/Objections analysis
above — Option C stands — only HOW it is implemented in the widget tree.

**Evidence for the G10 fix:**
`uat_zoom_ind_070_togglingReadoutDoesNotShiftSiblingWidgets`
(`tests/uat/test_uat_zoom_indicator.cpp`, UAT-VWR-106) asserts a sibling
status-bar widget's `pos()` is bit-identical before a zoom action
reveals the readout, while it's visible, and after it fades back out.
Confirmed failing against the status-bar-widget design (measured
`QPoint(282,3)` vs expected `QPoint(323,3)`) and passing against the
floating-overlay design.

## Evidence required to reopen

A concrete, checkable problem — e.g. usability evidence that real users
cannot find the on-demand tooltip path, or that the visible/fade timing
is wrong for real dogfooding sessions (readout vanishing before it can
be read, or lingering as unwanted chrome) — plus owner sign-off.
