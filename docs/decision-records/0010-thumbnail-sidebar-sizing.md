# 0010 — Thumbnail sidebar: scale-to-width, aspect-tracking rows, page number in a label row

- **Status:** proposed
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-15
- **Date accepted / superseded:** —

## Context

The page-thumbnail sidebar sizes each thumbnail from a **fixed logical box**,
not from the sidebar's live width. `QSize m_size{80, 100}` is hardcoded
(`src/ui/ThumbnailModel.h:47`); `setIconSize(m_model->thumbnailSize())` is set
**once** to that box and never updated (`src/ui/Sidebar.cpp:217`);
`ThumbnailDelegate::paint` scales the pixmap to the fixed 80 px width and
**centers** it in the full-width column (`src/ui/Sidebar.cpp:88-89`), so a
widened sidebar floats a small image in the middle at roughly one-eighth of the
column; and `resizeEvent` re-lays-out but **never recomputes iconSize**
(`src/ui/Sidebar.cpp:130-141`). On the vertical axis, `sizeHint` pins every row
to `icon.height() + 2*kThumbVerticalPadding` (= 108 px, from the 100 px icon
height, `src/ui/Sidebar.cpp:61-66`), so a landscape page's width-limited fit
leaves ~52 px of empty row beneath it. The page number is painted as an
**in-pixmap badge** in the lower-right corner (`src/ui/Sidebar.cpp:94-121`), not
a label row.

Two prior passes bear on this. The horizontal ~1/8-width waste and the vertical
fixed-row gap are both captured in the feeding backlog item
(`docs/backlog/2026-07-13-thumbnail-sidebar-sizing.md`). The **vertical** axis
was diagnosed — but only diagnosed — in PR #37, carried in `TODO.md:44-105`
("Thumbnail sidebar row height — fixed rows gap under non-portrait pages"): that
pass proved Qt honours the delegate's `sizeHint` exactly (row height ==
`sizeHint` height == 108 at both 1× and 2×), correctly re-attributed the gap to
"a fixed row height meeting a variable page aspect," and recommended a per-row
`sizeHint` of `fittedThumbnailHeight(index) + 2*kThumbVerticalPadding`. PR #37
shipped as **diagnosis-only** ("no code change"); the fix never landed, and it
never touched the **horizontal** scale-to-width axis at all. This record settles
both axes and the page-number placement question, so the paper-cut terminates in
a committed threshold rather than a second diagnosis.

**In-flight work this record governs (so it isn't misread as a fresh fork):** a
wave-2 sibling session is already implementing **thumbnails width-fill** against
the backlog item above — deriving icon width from
`viewport()->width() - 2*margin` in `resizeEvent` and letting height follow page
aspect via a cheap model role. **This record RATIFIES and refines that direction
(Option A below); it does not open a competing design.** The backlog's declared
G2 proxies (portrait width `>= viewport width - 2*padding`; landscape
`visualRect().height()` ≈ fitted thumbnail height) are adopted here verbatim, not
contradicted — this record adds the page-number-placement axis the backlog left
open and grounds the whole direction in reference-app convention.

**What ships today (so this record isn't read as describing the target):**
fixed 80×100 thumbnails centered in the column at ~1/8 width, a fixed 108 px row
that gaps under landscape pages, and the page number as an in-pixmap
lower-right badge.

External grounding — the reference-app norm is scale-to-width with a label:

- **Apple HIG → *Sidebars*.** A sidebar is a resizable navigation surface whose
  content is expected to adapt as its container and width change ("consider
  automatically hiding and revealing a sidebar when its container window
  resizes"; let people show/hide and resize it)
  (https://developer.apple.com/design/human-interface-guidelines/sidebars).
  A thumbnail that stays 80 px wide in a 640 px sidebar is the opposite of
  content adapting to width.
- **Preview.app.** Apple's own guide: *"Change the size of the thumbnails:
  Choose View > Thumbnails, then drag the sidebar's separator to the left or
  right to change the width of the sidebar."* — i.e. the thumbnail **scales to
  the sidebar width** as the separator is dragged
  (https://support.apple.com/guide/preview/view-pdfs-and-images-prvw11470/mac).
- **Adobe Acrobat.** The Page Thumbnails panel is width-adjustable and thumbnail
  size is user-adjustable: *"In the Page Thumbnails side panel, select Options,
  and then select Reduce Page Thumbnails or Enlarge Page Thumbnails"* (and the
  panel-separator drag exposes a size slider)
  (https://helpx.adobe.com/acrobat/using/page-thumbnails-bookmarks-pdfs.html).
- **PDF Expert (Mac).** The page number is a **label beneath** the thumbnail —
  Readdle's own guidance references clicking *"the page number under a preferred
  page"* in the thumbnails view
  (https://pdfexpert.com/how-to-rearrange-pages). Preview and Acrobat likewise
  appear to render the page number as text below/beside the thumbnail, not as
  an overlay drawn inside the page image **(needs-live-verification** — the
  page-number placement in these two apps was not confirmable from the
  reputable docs consulted).

These are templates for the options below, not a ruling; the arbiter section is
deliberately empty while this record is `proposed`.

## Options

- **A. Scale-to-width, aspect-tracking rows (fill the column) — RATIFY.** Derive
  the thumbnail width from the live viewport width
  (`viewport()->width() - 2*margin`, recomputed in `resizeEvent`), so a portrait
  thumbnail fills the column instead of floating at ~1/8 width; make the per-row
  `sizeHint` `fittedThumbnailHeight(index) + 2*kThumbVerticalPadding` so a
  landscape row hugs its thumbnail with no fixed-height slack (aspect read from a
  cheap model role — `QPdfDocument::pagePointSize`, already used in
  `PdfDocument::renderThumbnail`, `PdfAdapter.cpp:731`). This is the wave-2
  sibling's direction and the reference-app norm (Preview scale-to-width, Acrobat
  width-adjustable panel, HIG "content adapts to width"). Page number moves to a
  **label row below** the thumbnail, matching Preview/PDF Expert/Acrobat and
  keeping the now-full-bleed pixmap unoccluded.
- **B. Fixed-size thumbnails.** Keep a constant logical thumbnail box regardless
  of sidebar width (today's behaviour, or a larger constant). Simple and stable,
  but reproduces the ~1/8-width waste on a wide sidebar and the fixed-row gap
  under landscape pages, and diverges from every reference app.
- **C. User-adjustable thumbnail size.** Add an explicit size control (slider or
  Enlarge/Reduce menu, à la Acrobat) decoupled from sidebar width, so the user
  picks a thumbnail size independent of column width. Most flexible, but adds a
  control surface and a persisted preference, and still has to answer "what fills
  the column at the default size" — i.e. it sits on top of A rather than
  replacing it.

## Personas debate

- **Office non-technical user:** Widens the sidebar to see pages better and
  expects the thumbnails to get bigger, the way Preview does. A thumbnail that
  stays tiny in a wide column reads as broken. Favours A. Has no appetite for a
  separate size slider (Option C) they'd have to discover.
- **Older careful user:** Wants the sidebar predictable and legible — page images
  large enough to recognise, page numbers in a stable, readable spot. An in-pixmap
  badge overprinted on page content is harder to read than a plain number in its
  own row; favours A with the **label-row** page number. Would tolerate B only if
  the fixed size were already large. Wary of C as one more setting to get wrong.
- **Power migrator (ex-Preview/Acrobat/PDF Expert):** Every source app scales the
  thumbnail to the panel and labels the page number below it. Expects the same;
  reads fixed-1/8-width as non-native. Favours A. Would welcome C's explicit size
  control as a familiar Acrobat affordance, but not as a *substitute* for
  scale-to-width — in Acrobat the panel still widens.
- **Occasional user:** Opens the sidebar rarely; needs it self-explanatory. A
  column-filling thumbnail with a legible page number underneath serves this
  directly; a size slider is a control they won't find. Favours A, neutral-to-cool
  on C.

## Admissible objections

- **Office / power-migrator user, Option B:** on a widened sidebar the thumbnail
  stays ~1/8 of the column; the concrete failure is "I made the sidebar bigger and
  the pages didn't get bigger — it looks broken." This is the decisive argument
  against B and the reason the record ratifies A.
- **Older-careful user, Option B (vertical axis):** on a mixed-orientation deck
  the landscape rows carry ~52 px of empty space beneath each thumbnail
  (`TODO.md:72-82`), so the list looks ragged and wastes scroll distance; concrete
  failure at "the landscape pages have big gaps under them." Closed by A's per-row
  `sizeHint`.
- **Older-careful user, in-pixmap page badge under a full-bleed thumbnail:** once
  the thumbnail fills the column, a lower-right in-pixmap badge
  (`src/ui/Sidebar.cpp:94-121`) is overprinted on real page content and can land
  on dark or busy artwork; concrete failure at "I can't read the page number
  against the page." This is the argument for moving the number to a label row, as
  Preview/PDF Expert/Acrobat do.
- **Occasional user, Option C:** a size control the user must find and set adds a
  discovery step for a surface they open rarely; concrete failure at "I didn't know
  there was a slider, so the thumbnails stayed a size I didn't want." Admissible
  against making C the *primary* mechanism; it does not bar C as an addition on top
  of A.

### Rejected as naked preference

- "Fixed sizes are cleaner / more consistent." — rejected: asserts a taste, names
  no user, step, or failure. The admissible version (B's ~1/8-width waste on a wide
  sidebar) points the other way.
- "Overlay badges look more modern than a label row." — rejected: states an
  aesthetic, no user-step-failure. The admissible version is the readability
  objection above (badge overprinted on page content), which favours the label row.

## Checkable threshold this record would establish

The line this record commits to, proven by G2 offscreen `QWidget::grab()`
(`QT_QPA_PLATFORM=offscreen`, per AGENTS.md G2) over a **mixed-orientation deck**
(portrait + landscape pages in one document):

1. **Width fills the column (horizontal axis — the ~1/8 fix).** For a **portrait**
   page, the painted pixmap width in `ThumbnailDelegate::paint`
   (`src/ui/Sidebar.cpp:88`) is
   `>= viewport()->width() - 2*kThumbVerticalPadding - epsilon` — the thumbnail
   fills the sidebar column, not ~1/8 of it. This binds under a **widened**
   sidebar (recomputed in `resizeEvent`), not just at the default width.
2. **No fixed-height slack (vertical axis — the PR #37 gap).** For a **landscape**
   page, `QListView::visualRect(index).height()` ≈ the fitted thumbnail height,
   i.e. per-row `sizeHint` = `fittedThumbnailHeight(index) + 2*kThumbVerticalPadding`
   rather than the fixed 108 px row. The pre-fix ~52 px landscape gap
   (`TODO.md:72-82`) is the regression oracle.
3. **Page number lives in a label row below the thumbnail (placement axis).** The
   page-number text's bounding rect lies **below** the thumbnail pixmap's bounding
   rect within the row (a dedicated label row), not overpainted inside the pixmap
   as the current lower-right badge (`src/ui/Sidebar.cpp:94-121`). Checkable via
   `grab()`: the number's top edge `>= the pixmap's bottom edge` for the row.

These three are jointly the record's pass/fail line for **Option A**. Option B
would establish only "thumbnail size is a constant independent of viewport width"
(and fails 1 and 2); Option C would additionally require a persisted user size
setting and a control whose default still satisfies 1. The threshold above is the
one this record proposes to commit to.

## Arbiter verdict + rationale

Empty while status is `proposed` — the implementing session runs the
persona/arbiter cycle.

## Evidence required to reopen

N/A until accepted.
