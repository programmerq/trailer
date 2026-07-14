# UX Research Agenda — v0.3.0 dogfood follow-ups (2026-07-13)

This is the **research set** for the UX findings raised in the v0.3.0 real-Mac
dogfood pass (2026-07-13). It is a companion to `docs/backlog/`, not a
replacement: each backlog item carries the *what* and a checkable G1 threshold;
each theme here carries the *why-decided-that-way* — the reference-app and
platform-convention research that a maintainer must do **before** committing to a
design, so the choice is grounded in observed convention rather than taste.

Every theme below terminates in a **decision or gate**, never in prose. The
terminus is one of: an accepted decision record in `docs/decision-records/`
(when the choice is a real design debate, per the G6 gate), or a concrete G1
threshold written back into the feeding backlog item (when the research just
picks a value or a convention). No theme is "done" until its decision/gate line
is satisfied — a merged write-up with the remediation still open is the
diagnosis-only anti-pattern this project has already been bitten by (PR #37).

Evidence conventions used throughout:
- **UX evidence / gate machinery** — user-visible states are proven per AGENTS.md
  G1 (threshold declared first) and G2 (offscreen `QWidget::grab()` under
  `QT_QPA_PLATFORM=offscreen`). Per the ux-evidence ruling, `grab()` suffices for
  most states, but **native-chrome / menu / icon / permission** items (macOS Dock
  icon, Services menu, TCC prompts) require a real-Mac pass.
- **Performance evidence** — the perf-measurement ruling: agent-measured locally
  on the reference corpus + reviewer check; CI enforces only the corpus-
  independent structural invariants in `docs/performance-budgets.md:56-66`, never
  a wall-clock assertion.

---

## Theme 1 — Search-result navigation conventions (position-aware seeding)

**Research question.** When a user invokes Find in a paginated document viewer,
which match should be selected first — the first match in document order, or the
first match at or after the current reading position — and how do reference apps
signal "N of M" while keeping whole-document coverage?

**Sources to consult.**
- Apple HIG — *Search fields* and *Searching* patterns; macOS text-finding
  behaviour (the standard Find bar's "next from here" semantics).
- Preview.app: open Find on a middle page of a long PDF and observe which match
  is highlighted first and how the sidebar results list orders/counts.
- Skim and PDF Expert: same probe — do they seed at the current page or at
  document start? How is the match counter framed?
- Adobe Acrobat: Find vs full-text Search panel — note the difference between
  "find next from cursor" and a global results list.
- Browser Ctrl+F (Safari/Chrome) as the most-ingrained baseline for
  "wrap-around from current position."

**Decision/gate to produce.** A G1 threshold written back into
`2026-07-13-search-current-page-seed`: the deterministic UAT assertion that the
initially-seeded match's page is `>= currentPage()` (first such), with
wrap-to-0 past the last match. If reference apps diverge enough that the choice
is a genuine debate (e.g. seed-at-page vs always-document-order), escalate to an
accepted decision record instead.

**Feeds backlog item(s).** `2026-07-13-search-current-page-seed`.

---

## Theme 2 — Toolbar anchoring & overflow norms (multi-toolbar layout)

**Research question.** In a multi-toolbar app where a primary toolbar coexists
with contextual toolbars (form, markup) and a search field, what anchors where —
and what is the convention for an overflow ("show more") affordance so it does
not itself reflow the layout when toggled?

**Sources to consult.**
- Apple HIG — *Toolbars* (macOS) and *The toolbar*; conventions for a fixed
  primary toolbar vs contextual/secondary bars and trailing search placement.
- Preview.app and PDF Expert: observe how the primary toolbar stays put while a
  markup/annotation bar appears on a second row, and where search anchors.
- Adobe Acrobat: primary toolbar + contextual tool toolbars; how overflow
  collapses on narrow windows.
- Qt's own `QToolBarExtension` behaviour (the `qt_toolbar_ext_button` chevron):
  document the default size-hint reflow and the norm of pinning it.
- The app's own markup toolbar second-row wiring as the internal reference
  (`src/ui/MainWindow.cpp:733`).

**Decision/gate to produce.** G1 thresholds written back into
`2026-07-13-toolbar-anchoring`, proven by G2 `grab()` of both toolbar states:
main-toolbar top-left origin pixel-stable across form-toolbar toggle; form
buttons right-aligned near search; narrow-window overflow into the chevron; the
chevron's bounding rect invariant under toggle.

**Feeds backlog item(s).** `2026-07-13-toolbar-anchoring`.

---

## Theme 3 — Background-work scheduling / QoS budgets on document open

**Research question.** What work is legitimate to run synchronously when a
document opens, what must move off the main thread, and how should a large-file
open be staged so the first page paints before the whole file is parsed —
without regressing edit/annotation correctness?

**Sources to consult.**
- Apple developer guidance — *Prioritize Work with Quality of Service Classes*
  (Energy Efficiency Guide); Dispatch QoS classes for user-initiated vs utility
  work; keeping the main thread free.
- Nielsen Norman Group response-time limits (0.1s / 1s / 10s), already the frame
  for `docs/performance-budgets.md`.
- Reference-app behaviour: Preview.app / Acrobat opening a large (100 MB+) PDF —
  does page 1 appear near-instantly with the rest streaming in? Observe the
  "first page before full parse" staging.
- Qt: `QtConcurrent` / `QThread` worker-open patterns; `QPdfDocument` vs qpdf
  lazy-load seams; the existing save-path worker (`MainWindow.cpp:2215`) as the
  in-repo pattern to mirror for open.
- The binding structural invariants and B4/B5/B6 rows in
  `docs/performance-budgets.md`.

**Decision/gate to produce.** The deterministic structural proxies in
`2026-07-13-startup-hang-large-pdf` (0 pages walked and editor-parse-count 0
before first-page paint; no GUI-thread read), retiring the `QSKIP` in
`tests/test_perf_gui_thread_io.cpp`. If the staging choice (defer qpdf editor
load vs worker-thread open with a placeholder page) is a real architecture
debate, capture it in an accepted decision record that cites the budget rows.

**Feeds backlog item(s).** `2026-07-13-startup-hang-large-pdf` (and, on the
shared open/search seam, `2026-07-13-search-current-page-seed`).

---

## Theme 4 — macOS adaptive app icons (Asset Catalog vs Icon Composer)

**Research question.** What is the supported mechanism for a light/dark (and
Tahoe tinted/clear) adaptive app icon on modern macOS, and how should it be wired
into a CMake-built bundle that currently ships a plain `.icns`?

**Sources to consult.**
- Apple HIG — *App icons* (macOS, incl. the macOS 26 Tahoe appearance variants:
  light, dark, tinted, clear).
- Apple developer docs — Asset Catalog `AppIcon` with `luminosity` appearance
  variants compiled to `Assets.car`; `CFBundleIconName` vs `CFBundleIconFile`.
- Icon Composer (`.icon`) — the Tahoe-era authoring path and when to prefer it.
- How the existing shipped-but-orphaned assets (`resources/icons/trailer-dark.*`)
  and the generators (`icon/make_iconset.py`, `icon/make_simplified.py`) can feed
  either path; how to compile an Asset Catalog outside a full Xcode project
  (actool) in a CMake build.

**Decision/gate to produce.** A decision on Asset Catalog vs Icon Composer,
recorded either as a G1 threshold in `2026-07-13-macos-dark-app-icon` or, if the
build-integration choice is contentious, an accepted decision record. Terminus
gate is the item's real-Mac threshold: dark-mode Dock shows the dark icon, light
shows light, appearance toggle swaps them (real-Mac tier — `grab()` insufficient
for Dock chrome).

**Feeds backlog item(s).** `2026-07-13-macos-dark-app-icon`.

---

## Theme 5 — Thumbnail sizing & layout norms (scale-to-width, aspect, labels)

**Research question.** How wide should a page-thumbnail sidebar render its
thumbnails relative to the sidebar width, how should row height track page
aspect for mixed-orientation decks, and where does the page number belong
(overlay badge vs label row)?

**Sources to consult.**
- Apple HIG — *Sidebars* and list/gallery layout conventions.
- Preview.app thumbnail sidebar: does the thumbnail scale to the sidebar width as
  the user widens it? How is a landscape page's row height handled vs a portrait
  one? Where is the page number?
- PDF Expert and Acrobat page-thumbnail panels: scale-to-width behaviour, aspect
  handling, label placement, and any user-adjustable thumbnail size.
- The in-repo diagnosis already written (`TODO.md:44-105`, PR #37) — its
  per-row `sizeHint` = fitted-height proposal is the starting point for the
  vertical axis; the research must additionally settle the **horizontal**
  scale-to-width axis it never addressed.

**Decision/gate to produce.** G1 thresholds in
`2026-07-13-thumbnail-sidebar-sizing`, proven by G2 `grab()` over a
mixed-orientation deck: portrait thumbnail width `>= viewport width - 2*padding`
(fills the column), and landscape `visualRect().height()` ≈ fitted thumbnail
height (no fixed-row slack).

**Feeds backlog item(s).** `2026-07-13-thumbnail-sidebar-sizing`.

---

## Theme 6 — Text-layer selection vs OCR affordance conventions

**Research question.** When should a PDF viewer offer to "recognize text," how
non-invasively, and how must that affordance distinguish a born-digital document
(has a text layer — selection should just work) from a scanned one (needs OCR)?

**Sources to consult.**
- Apple HIG — *Offering help* / status and notification patterns; the norm that
  passive document-status affordances are non-modal and benefit-worded.
- Preview.app and PDF Expert: on a born-digital PDF, selection works silently
  with no "recognize" prompt; on a scanned PDF, how (if at all) is OCR offered,
  and is it modal or an in-context affordance?
- Acrobat's "Recognize Text" flow: when it is surfaced, and how it avoids firing
  on documents that already have a text layer.
- The project's own ADR-0002 §3 "Missing model"
  (`docs/decision-records/0002-ml-background-removal-progress-cancel.md:126-129`,
  gates G5/G6) — the accepted spec the misfiring `m_largeDocOcrHint` violates,
  and the compliant `m_ocrModelMissingHint` (`MainWindow.cpp:517-546`) as the
  internal reference.

**Decision/gate to produce.** Reconcile the affordance with ADR-0002 §3 on its
four violated points (gate on `!hasTextLayer()`, route through consent, be
page-state-driven, be benefit-worded). Terminus is the G1 thresholds in
`2026-07-13-text-selection-and-recognize-notice` (mirroring ADR-0002 G5/G6). If
the reconciliation changes the accepted spec, it rides as an amendment/superseding
decision record, not ahead of the code.

**Feeds backlog item(s).** `2026-07-13-text-selection-and-recognize-notice`.
