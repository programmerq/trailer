---
id: 2026-07-13-thumbnail-sidebar-sizing
title: Sidebar thumbnails use ~1/8 of the sidebar width and leave vertical slack; must scale to sidebar width
priority: P1
status: open
source: v0.3.0 real-Mac dogfood report (2026-07-13)
created: 2026-07-13
---

## Threshold

Thumbnails scale to the sidebar width instead of a fixed logical box, and the
per-row height follows page aspect (no fixed-height slack).

Declared pass/fail proxies (G2 evidence captured by offscreen
`QWidget::grab()` under `QT_QPA_PLATFORM=offscreen`, per AGENTS.md G2):

1. **Width fills the column.** For a portrait page, the painted pixmap width in
   `ThumbnailDelegate::paint` (`src/ui/Sidebar.cpp:88`) is
   `>= viewport()->width() - 2*kThumbVerticalPadding - epsilon` — the thumbnail
   fills the sidebar column, not ~1/8 of it.
2. **No fixed-height gap.** For a **landscape** page,
   `QListView::visualRect(index).height()` ≈ the fitted thumbnail height (per-row
   `sizeHint` = `fittedThumbnailHeight(index) + 2*kThumbVerticalPadding`), not the
   fixed 108 px row.

Verified: `grab()` of the sidebar over a mixed-orientation deck shows portrait
thumbnails filling the column width and landscape rows with no bottom slack.

## Context

Owner dogfood report: sidebar thumbnails render at roughly one-eighth of the
sidebar width, with wasted vertical space under non-portrait pages. This is a
**recurring** paper-cut, not a fresh finding — see the recurrence note below.

Root cause: the thumbnail is a fixed logical box that never recomputes from the
viewport width.
- `src/ui/ThumbnailModel.h:47` — `QSize m_size{80, 100}` is the hardcoded
  logical thumbnail size (80 px wide, independent of sidebar width).
- `src/ui/Sidebar.cpp:217` — `setIconSize(m_model->thumbnailSize())` is set
  **once** to that fixed box and never updated on resize.
- `src/ui/Sidebar.cpp:61-66` — `ThumbnailDelegate::sizeHint` returns a full-width
  column but pins row height to the fixed icon box. The 108 px row derives from
  the **100 px icon height** (the height dimension of `QSize m_size{80, 100}`,
  `ThumbnailModel.h:47`) via `icon.height() + 2*kThumbVerticalPadding`
  (`Sidebar.cpp:64`) — not from the 80 px width.
- `src/ui/Sidebar.cpp:77-91` — `paint()` scales the pixmap to the fixed 80 px
  width and **centers** it in the full-width column (line 89), so a wide sidebar
  floats a small image in the middle. The page number is an in-pixmap badge
  (`:94-121`), not a label row.
- `src/ui/Sidebar.cpp:130-141` — `resizeEvent` calls
  `scheduleDelayedItemsLayout()` but **never recomputes iconSize**, so widening
  re-lays-out at the same fixed 80 px.

Fix direction: derive icon width from
`m_thumbnails->viewport()->width() - 2*margin` in `resizeEvent`
(`Sidebar.cpp:138`) — make `iconSize`/`thumbnailSize` viewport-driven instead of
the `ThumbnailModel.h:47` constant; height follows page aspect exposed via a
cheap model role (aspect from `QPdfDocument::pagePointSize`, already used in
`PdfDocument::renderThumbnail`, `PdfAdapter.cpp:731`). The thumbnail-sizing
research theme in `docs/research/2026-07-13-ux-research-agenda.md` feeds the
scale-to-width vs fixed-aspect decision. That decision is captured in
`docs/decision-records/0010-thumbnail-sidebar-sizing.md`, which **ratifies** this
item's width-fill direction (it does not fork it) and adds the page-number
placement axis.

### Recurrence — this complaint has been diagnosed but never fixed in code

- The vertical-gap half was written up in the original May 2026 HITL passes and
  carried in `TODO.md:44-105` ("Thumbnail sidebar row height — fixed rows gap
  under non-portrait pages").
- **PR #37** (June 2026, merged 2026-07-12) shipped a **diagnosis-only** doc:
  it changed only `TODO.md` (+54/−18) and two screenshot PNGs, states verbatim
  "Diagnosis only — no code change," and touched no `.cpp`/`.h`. Code proof the
  fix never landed: `src/ui/Sidebar.cpp:61-66` still returns the fixed
  `icon.height()+2*kThumbVerticalPadding` (= 108). No later PR (#38–#52)
  implements it.
- Even the PR #37 diagnosis only addressed the **vertical** gap and kept the
  thumbnail fixed at 80 px wide — it never addressed the owner's primary
  complaint, the **horizontal ~1/8 width** waste. So both axes remain unfixed.

This item exists so the paper-cut is tracked to a checkable threshold and does
not read as "handled" merely because a diagnosis was written down.

## Provenance

v0.3.0 real-Mac dogfood report, 2026-07-13. Recurrence trail: original May HITL
passes, `TODO.md:44-105`, PR #37 (diagnosis-only, no code fix). The `P1` rank is
justified by owner re-confirmation in the v0.3.0 dogfood + multi-cycle recurrence
(May HITL, PR #37), not a rank invented here.
