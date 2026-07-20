---
id: 2026-07-16-zoom-polish
title: Zoom polish — zoom-% indicator, larger steps, deferred smooth animation
priority: P3
status: partially-implemented
source: owner request (zoom polish) — feasibility & perf assessment 2026-07-16
created: 2026-07-16
---

# Zoom polish — feasibility & perf assessment

## Context

Owner request, verbatim: he wants (1) a visible zoom-% indicator ("there is
none today"), (2) larger zoom steps for large images ("current steps feel too
small"), and (3) a smooth zoom animation where the % counts up/down then fades
after a beat. He added: "I don't know if this is asking too much of Qt/qpdf, so
push back if this would cost optimization/performance."

Assessment verdict: implement (1) and (2) now — both are tiny and carry no perf
cost — and **defer (3)** because animating a multi-megapixel image per frame
blows the 16 ms/60 fps budget. Items 1 and 2 are **IMPLEMENTED in this change**
(`feat/zoom-polish`); item 3 is **DEFERRED**, with item 3a (a render-decoupled
HUD counter/fade) split out as the only cheap, salvageable piece.

### How zoom works today (two separate paths)

- **Image path** — `src/document/ImageAdapter.cpp`. `ImageDocument::applyScale`
  (`ImageAdapter.cpp:436-453`) rebuilds the displayed pixmap from scratch on
  every zoom change (`buildDisplayPixmap` → `QLabel::setPixmap`). The view host
  is a `QLabel` inside a `QScrollArea`; overlays (`AnnotationOverlay`,
  `SelectableTextLayer`) are children of the `QLabel`, kept in sync via
  `setGeometry(label->rect())`.
- **PDF path** — `src/document/PdfAdapter.cpp`. Zoom is delegated to Qt's
  `QPdfView` via `applyZoomFactor` (`PdfAdapter.cpp:791-800`), which sets
  `QPdfView::ZoomMode::Custom` and `setZoomFactor`. Qt re-rasterises the page
  internally at the new factor; we don't own the frame loop.
- Both paths step by the **same geometric ratio** `kZoomStep`
  (`ImageAdapter.cpp:46`, `PdfAdapter.cpp:48`) in `zoomIn` / `zoomOut`. Bounds
  differ: images `0.05..32`, PDF `0.10..16`.
- **Important constraint:** `IDocument` is **not a QObject** (`IDocument.h`, no
  `Q_OBJECT`) — there is no `zoomFactorChanged` signal to subscribe to. Any
  indicator must be pushed from the zoom call sites (or poll
  `doc->zoomFactor()`).

---

## Item 1 — Zoom-% indicator — **IMPLEMENTED in this change**

**Current state (before):** none. The factor was queryable via
`doc->zoomFactor()` (`IDocument.h`; `ImageAdapter.h:53`, `PdfAdapter.cpp:891`);
the value simply wasn't shown.

**Recommendation (as implemented):** a small permanent `QLabel`
(`m_zoomIndicator`) added to the status bar next to the existing ML/OCR
indicators (mirrors the `statusBar()->addPermanentWidget(...)` permanent-widget
pattern at `MainWindow.cpp` ~509). A private `updateZoomIndicator()` helper
reads `doc->zoomFactor()`, formats `qRound(factor*100)` as e.g. `"120%"`, and
hides the label when `!doc || !doc->supportsZoom()`. It is called from all five
zoom action lambdas (zoom in, out, actual, fit-page, fit-width) and from the
document-changed path (`onCurrentDocumentChanged`, right after the `hasZoom`
action-enable block), so tab switches refresh it too.

**Complexity:** small — one member label, one ~12-line helper, six one-line call
sites. No new files or classes.

**Perf verdict:** free. A text label updated only on discrete zoom events — no
per-frame or per-paint cost.

**Known limitation (accepted, noted in code):** resize-driven refits update the
scale *inside the document* (`reapplyFitMode`, `ImageAdapter.cpp:507-532`; Qt's
internal fit for PDF) without notifying `MainWindow`, so a live window-drag in a
fit mode won't tick the number until the next explicit zoom action. Making it
live would require promoting `IDocument` to a `QObject` with a
`zoomFactorChanged` signal (or a `QPdfView` bridge) — a broader change tracked
as a possible follow-up, not a blocker. Noted in a comment on the
`updateZoomIndicator()` declaration in `MainWindow.h`.

---

## Item 2 — Zoom step sizing — **IMPLEMENTED in this change**

**Current state (before):** geometric `1.1×` (10%) per tap, shared by both
paths (`ImageAdapter.cpp:46`, `PdfAdapter.cpp:48`).

**Diagnosis:** the step is already *geometric*, so it is size-independent in
ratio terms — a 4000 px and a 400 px image both take the same ~7.3 taps to
double at 1.1×. The "feels too small, especially for large images" complaint is
that a 10% ratio is just fine-grained; on a large image there's a lot of pixel
travel per tap so the fine step is more noticeable to click through. The fix is
to raise the ratio, not to key the step off image dimensions (users reason in
%, and a dimension-keyed step would make the same tap mean different things on
different files).

**Recommendation (as implemented):** bump `kZoomStep` to `1.25` (double in ~3.1
taps, a Preview/Acrobat-like default) in **both** `ImageAdapter.cpp:46` and
`PdfAdapter.cpp:48`, kept equal so the two view types feel identical.
`kZoomMin`/`kZoomMax` are unchanged; the existing `std::clamp` already handles
the coarser overshoot.

**Complexity:** trivial — two one-line constant edits, no logic change.

**Perf verdict:** none. Pure constant change.

**Optional deferred alternative:** a fixed "step ladder" that snaps to familiar
stops (25/50/75/100/150/200/400%) instead of a pure ratio. Nicer UX but a real
behaviour change (nearest-stop search on both paths, interacts with fit-mode
entry) — defer to its own item if the owner wants it after trying the coarser
ratio.

**Tests:** `imageDocumentZoomInStepsByQuarter` and
`pdfDocumentZoomInStepsByQuarter` in `tests/test_adapters.cpp` assert one
`zoomIn` from Actual Size (1.0) lands at ~1.25 for both adapters.

---

## Item 3 — Smooth zoom animation with % counter fade — **DEFERRED**

This is the perf-sensitive ask, and the honest answer is **push back on the
image-scaling half of it.**

**Current state:** every zoom change funnels through `applyScale`
(`ImageAdapter.cpp:436-453`), which for each call does a full
`QImage`→scaled-`QPixmap` rebuild (bilinear resample + fresh allocation +
`setPixmap`). There is no intermediate/cheap transform stage — the pixmap is
always produced at final resolution.

**Why a `QVariantAnimation` driving `applyScale` per frame is not viable for
large images:** a 2880×1800 source is ~5.2 MP; at 2× the target buffer is ~20
MP, reallocated *every frame*. A smooth-scale of a multi-megapixel image to a
multi-megapixel target is single-digit-to-tens of milliseconds of CPU on its
own; a 250–300 ms animation is ~15–18 such frames, each allocating a growing
buffer and re-uploading via `QPixmap::fromImage`. That blows the ~16 ms/60 fps
budget, thrashes the allocator, and will visibly jank — exactly the
"optimization/performance" cost the owner asked us to flag.

**Why a cheap `QTransform`/`QGraphicsView` scale of an already-rendered pixmap
(re-render sharp only at the end) is correct but not small:** it requires
replacing the `QLabel` host with a `QGraphicsView`/`QGraphicsPixmapItem` so
intermediate frames are a GPU/transform scale of one existing pixmap rather than
N resamples. But the overlays and text layer are `QLabel` children driven by
`setGeometry(label->rect())` and a `p*m_scale` doc→view map; all that
coordinate/geometry plumbing would have to be ported to the graphics scene, and
it only helps the image path. The **PDF path is worse**: `QPdfView` has no cheap
intermediate-scale hook — `setZoomFactor` re-rasterises the page, so animating it
re-renders every frame with no way to defer. So a "smooth zoom" covering both
document types is a large, cross-cutting refactor with real regression surface on
annotations, selectable text, search-highlight geometry, and scroll anchoring.

**Perf verdict:** animating the *image* per frame = **rejected** for large
images. Animating only the *HUD number and opacity* = **free**.

### Item 3a (salvageable, future) — render-decoupled HUD %-counter + fade

The one genuinely cheap piece: a small overlay `QLabel` on the viewport with a
`QVariantAnimation` counting the number up/down and a `QGraphicsOpacityEffect` +
`QPropertyAnimation` fading it out after a beat costs nothing (one tiny widget,
no image work). The catch is that it must be **decoupled from the image
resample**: snap the zoom to its final factor immediately (today's instant
`applyScale`) while the HUD number animates and fades. That delivers most of the
perceived "polish" without touching the render hot path.

- **3a — HUD %-counter + fade with instant zoom snap:** cheap, can be picked up
  soon. This is the only salvageable piece of the smooth-animation ask.
- **3b — true smooth image scaling:** costly graphics-view refactor with no good
  PDF story. Do **not** implement per-frame `applyScale` animation. Only take
  3b on with eyes open.
