# 0006 — Sidebar thumbnails scale to column width with aspect-fit rows

- **Status:** accepted
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-15
- **Date accepted / superseded:** 2026-07-15 (accepted)

## Context

This record settles a **user-visible default** change (gate G6 in
[`../../AGENTS.md`](../../AGENTS.md)): the sidebar page-thumbnail rendering
moves from a fixed logical box to scale-to-width with per-row aspect-fit
heights.

The backlog item
[`../backlog/2026-07-13-thumbnail-sidebar-sizing.md`](../backlog/2026-07-13-thumbnail-sidebar-sizing.md)
(P1) tracks this paper-cut, re-confirmed in the owner's v0.3.0 real-Mac dogfood
(2026-07-13) and recurring across the May 2026 HITL passes and PR #37
(diagnosis-only, no code change). Before this change:

- `src/ui/ThumbnailModel.h` pinned a hardcoded `QSize m_size{80, 100}` logical
  box; the delegate rendered the pixmap into that box and **centred** it in the
  full-width column, so a wide sidebar floated a ~1/8-width image in its middle.
- `ThumbnailDelegate::sizeHint` returned a fixed `iconSize.height() +
  2*kThumbVerticalPadding` = 108 px for **every** row, leaving vertical slack
  under landscape/mixed pages.
- `resizeEvent` relayouted but never recomputed the render/icon size, so
  widening the sidebar re-laid-out at the same fixed 80 px.

## What ships now (so this record isn't misread as a target)

The thumbnail is scaled to fill the column width (`viewport()->width() −
2×kThumbHorizontalMargin`), left-aligned at the margin, with a subtle 1 px
Mid-palette border. Each row's height is `round(availW / pageAspect) +
2×kThumbVerticalPadding`, where `pageAspect` comes from a new cheap, no-render
`IDocument::pageSizeHint(pageIndex)` surfaced to the delegate through
`ThumbnailModel::AspectRole`. Portrait rows are tall, landscape rows short, with
no fixed-height gap. On resize the pixmap is re-rendered at the new column width
(debounced) so scaling up stays crisp, while layout responds immediately.

## Constants this record establishes

Each carries the in-code rationale comment required by PHILOSOPHY → *Hand-tuned
values stay hand-tuned*; per G6 this record cites `file:line` rather than
duplicating the comment.

### (1) `kThumbHorizontalMargin = 6` px

- **Where:** `src/ui/Sidebar.cpp` (anonymous namespace, next to
  `kThumbVerticalPadding`).
- **What it is:** the gutter between the thumbnail and each edge of the sidebar
  column. The thumbnail fills `viewport width − 2×this` and is left-aligned at
  this inset.
- **Range tried / symptom to change:** 0 px made the thumbnail touch the
  scrollbar/edge (cramped); 12 px left an obviously wasteful margin on a narrow
  sidebar. 6 px is the balance. Change if thumbnails crowd the edge (raise) or
  leave an obvious empty margin (lower).

### (2) Render-width clamp `[48, 600]` px

- **Where:** `src/ui/Sidebar.cpp`, `ThumbnailListView::resizeEvent`
  (`std::clamp(viewport()->width() − 2*kThumbHorizontalMargin, 48, 600)`).
- **What it is:** the logical width the thumbnail pixmap is re-rendered at on
  resize.
- **Range tried / symptom to change:** below 48 px a thumbnail is illegible;
  above 600 px the render cost / pixmap memory outgrows any sidebar a user would
  actually widen to. Raise the cap if thumbnails blur on a very wide sidebar;
  lower it if the app renders needlessly large pixmaps.

### (3) Resize re-render debounce = 120 ms

- **Where:** `src/ui/Sidebar.cpp`, `ThumbnailListView` `m_renderTimer`
  (single-shot, 120 ms).
- **What it is:** the delay before re-rendering at a new column width, so
  dragging the splitter doesn't thrash the render cache. Layout itself is
  immediate (`scheduleDelayedItemsLayout` + per-row `sizeHint`), so width fills
  instantly via `scaledToWidth`; only the crisp re-render waits.
- **Range tried / symptom to change:** 0 ms re-rendered at every intermediate
  drag width (thrash); 400 ms made crispness visibly late. 120 ms matches the
  sidebar's existing `m_pageSyncTimer` debounce. Change if a slow drag still
  thrashes (raise) or crispness lags noticeably after a resize settles (lower).

### (4) Aspect fallback = 0.8

- **Where:** `src/ui/ThumbnailModel.cpp` (`AspectRole` branch) and mirrored in
  `ThumbnailDelegate::sizeHint`.
- **What it is:** the width/height ratio used when the document can't supply a
  page size (null size / unsupported adapter). 0.8 is the legacy `80/100`
  logical-box ratio, so absent a real page size the row height matches the old
  behaviour rather than collapsing.

### (5) Thumbnail pixmap-cache budget = 256 MB

- **Where:** `src/ui/ThumbnailModel.cpp` (`kThumbCacheBudgetKB`, applied to the
  `QCache<int, QPixmap> m_cache` via `setMaxCost` in the constructor).
- **What it is:** the total cost budget of the rendered-thumbnail LRU cache, in
  kilobytes (each entry's cost is its byte size / 1024). The cache was
  previously an **unbounded** `QHash<int, QPixmap>`, only ever `clear()`ed
  wholesale. With the viewport-driven render size (`m_size = {w, w*2}`, `w`
  clamped `[48,600]`) a single 600-px-wide page on a 2× display is ~8–11 MB, so
  a 200–500 page deck scrolled at a wide sidebar could hold gigabytes resident
  — a real memory regression this change closes. `QCache` gives cost-based LRU
  eviction; 256 MB caps residency while keeping a few hundred small thumbnails
  hot.
- **Range tried / symptom to change:** raise if thumbnails re-render visibly on
  scroll-back at a wide sidebar; lower if resident memory balloons on very
  large decks.

## Personas debate

- **Office non-technical user:** Benefits directly — thumbnails become large
  enough to recognise a page at a glance instead of squinting at a 1/8-width
  stamp. Provided the resize stays smooth (debounce), no downside.
- **Older careful user:** Distrusts the UI reshaping itself. The change keeps
  row identity and selection across resizes (the re-render is a `dataChanged`,
  not a model reset), so nothing jumps or loses place — the same pages stay
  selected and scrolled where they were.
- **Power migrator (ex-Preview/Acrobat):** Preview/Acrobat thumbnails fill the
  strip and vary height by orientation; the old fixed centred box read as
  non-native. Scale-to-width + aspect rows matches the expected behaviour.
- **Occasional user:** Sees larger, orientation-correct thumbnails; no learning
  cost.

## Admissible objections

- **Older careful user, selection loss on resize:** if the resize re-render used
  the existing `setThumbnailSize` (a full `beginResetModel`), every splitter
  settle would drop the selection and scroll position — a concrete "it jumped
  and forgot where I was" failure. This is why a separate `setRenderWidth`
  re-renders in place (cache clear + `dataChanged(DecorationRole)`) instead of
  resetting the model.
- **Any user, resize thrash:** re-rendering on every intermediate drag width
  would stutter the drag. Closed by the 120 ms debounce and the 8 px render-width
  hysteresis in `ThumbnailModel::setRenderWidth`.

### Rejected as naked preference

- "A fixed 80 px box looks tidier / more uniform." — rejected: names no user,
  step, or failure. The owner's dogfood names the concrete failure (thumbnails
  waste ~7/8 of the column and leave slack under landscape pages), which the
  uniform box causes.

## Checkable threshold this record establishes

Over a mixed portrait/landscape deck, at ≥ 2 distinct sidebar widths: (a) the
painted thumbnail paper fills the column width — horizontal extent `≥ availW − 6`
where `availW = viewport width − 2×kThumbHorizontalMargin`, left-aligned at the
margin; and (b) each row's `visualRect(index).height()` ≈ `round(availW /
pageAspect) + 2×kThumbVerticalPadding` (±3 px), with portrait rows taller than
landscape rows. Proven by the UAT
`uat_thumb_010_scaleToWidthAndAspectRows`
(`tests/uat/test_uat_thumbnail_sidebar.cpp`), which authors a 6-page deck of A4
portrait pages interleaved with **extreme ~2.5:1 panoramic landscape** pages
(800×320 pt — deliberately far from A4-landscape's ~1.4:1 so the fitted
landscape height is unambiguously clear of the legacy fixed 108 px row at both
widths), reads ground-truth aspect from `QPdfDocument::pagePointSize`, and
asserts, at the two dock widths 200 and 360 (**viewport ~180 and ~340**, availW
~168 and ~328): the per-row height oracle (±3 px); a hard, non-skippable pixel
width-scan on a portrait row scrolled into view; an explicit
`|landscapeH − 108| > 3` anti-regression guard; that the debounced re-render
produces a **crisper** (wider device-pixel) pixmap at the wider width; and —
via a `modelAboutToBeReset` spy across both resizes — that the resize
re-render never resets the model (the in-place `dataChanged` mechanism that
preserves selection/row identity). A companion case,
`uat_thumb_020_imageDocAspectRow`, opens a wide 1600×400 PNG and asserts the
single image-doc thumbnail row tracks the image's 4:1 pixel aspect (a short
row), exercising the new `ImageDocument::pageSizeHint`. G2 evidence PNGs are
emitted via `QWidget::grab()` under `QT_QPA_PLATFORM=offscreen`.

## Arbiter verdict + rationale

Accepted. Scale-to-width with aspect-fit rows is the intended sidebar shape: it
resolves the owner's re-confirmed P1 dogfood complaint (both the horizontal
~1/8-width waste and the vertical landscape slack), matches Preview/Acrobat
behaviour the power-migrator expects, and is achieved without the selection-loss
regression the older-careful persona fears (in-place re-render, not model
reset). The four hand-tuned constants above are ratified with in-code rationale
comments and remain subject to the reopen clause; the owner retains
escalation-only veto and sign-off on the magnitudes.

## Evidence required to reopen

A documented user-flow failure caused specifically by the new behaviour (e.g. a
resize that stutters or loses selection despite the debounce/in-place re-render,
or a page whose aspect renders wrong), or owner sign-off changing one of the
ratified constants.

## Addendum — 2026-07-16 Retina/devicePixelRatio follow-up

A separate HiDPI regression surfaced in the scale-to-width delegate:
`ThumbnailDelegate::paint` scaled a dpr-stamped pixmap with
`QPixmap::scaledToWidth(availW)`, which works in raw device pixels and preserves
the source dpr — so on a Retina display (dpr=2) it painted at half the column
width (small thumbnail, tall empty row, low page-number badge). Fixed by scaling
to logical width via `src/ui/ThumbnailPaint.h::scaleToLogicalWidth` (a no-op at
dpr=1), pinned by `tests/test_thumbnail_paint.cpp`.
