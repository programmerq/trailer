# Per-type view-state restore excludes content-relative fields (raw zoom factor, window geometry)

- **Status:** accepted
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-31
- **Date accepted / superseded:** 2026-07-31 (accepted)

## Context

Owner dogfooding report (nightly build 0.3.1-dev+768.gce56b4b8, macOS,
Retina): double-clicking a small JPEG (504×375 px, 17.6 KiB) opened it at
**Custom 80% zoom** in a **huge window**. The in-app Feedback Report
confirmed `Zoom: Custom (80%)` and 34 recent files.

**Corroborating second report:** a different JPEG
(`_page_159_Picture_2.jpeg`) opened in a *second* window (the first held
a PDF; `Open files in: new_window`) at **Custom (64%)** — a different
percentage than the first report's 80%. This rules out a frozen/stamped
constant and confirms the mechanism below: the per-type default *learns*
whatever `ZoomMode`/factor the last-closed document of that type
happened to be at, so the replayed value naturally drifts with whatever
the user was last doing to an unrelated image.

**What ships today on `main` before this change:** CONVENTIONS.md §11
documents the three-tier view-state restore (per-file → per-type →
hardcoded) implemented in `MainWindow::onCurrentDocumentChanged()`
(`src/ui/MainWindow.cpp`). For a document with no per-file `RecentEntry`
state, the per-type fallback applies `DocumentTypeDefaults::forType(doc
->documentType())` — the state captured when the user last closed *any*
document of that type — unconditionally:

```cpp
if (!isCaptureOriginImage)
    doc->applyZoomState(def.zoomMode, def.zoomFactor);
...
if (!def.windowGeometry.isEmpty()) {
    restoreGeometry(def.windowGeometry);
}
```

Root cause, confirmed by direct code trace (not just plausible — see
Evidence below): `ZoomMode::Custom` with an arbitrary `zoomFactor` (e.g.
0.8) is a raw percentage the user chose to fit *one specific document's*
pixel dimensions to *their* viewport at the time. Applying that same raw
factor to an unrelated, differently-sized document via the per-type
fallback produces a meaningless result — exactly what happened here: an
earlier, larger image closed at 80% custom zoom became the Image-type
default, and the *next* image opened — a tiny 504×375 file — inherited
80% instead of its own natural fit. The identical shape affects
`windowGeometry`: a window sized (or maximized) to comfortably hold one
document's content is not a size preference for an unrelated document of
the same type, yet `restoreGeometry(def.windowGeometry)` applies it
unconditionally, immediately after `applyInitialWindowSize(doc)` (called
earlier in the same function) had *already* computed a correct,
content-fit size for the new document — silently discarding it.

This is the same bug family PR #123 fixed for the *capture-origin*
(screenshot/clipboard) exception (`ImageDocument::isCaptureOrigin()`
guard) and the sentinel-application bug
(`applyZoomStateSentinelLeavesInitialFitUndecided`) — both scoped
narrowly to their triggering symptom. This record generalizes the
underlying principle those fixes already assumed but never stated: a
per-type default is a *convenience for typical documents*, not a promise
about *this* document, and some captured fields are inherently tied to
the document that produced them.

**DESIGN/PHILOSOPHY citations.** `src/document/IDocument.h`'s
`DocumentType` comment already frames per-type defaults qualitatively:
*"all PDFs open at fit-to-page, all images open at actual size"* — a
*mode*, not a raw percentage. DESIGN §6.1.3 lists the zoom feature as
Zoom In/Out/Actual Size/Fit to Window/Zoom to Selection — again modal,
not "restore an arbitrary prior percentage." Neither document states the
per-type-restore edge case explicitly, so this record settles it rather
than inferring silently (G6).

There is already a test asserting fit-to-content never *upscales* a
small image past 100% (`smallImageResizeDoesNotUpscale`,
`tests/test_image_scale.cpp`); this record's counterpart is that a small
image must also never be force-*shrunk* by an unrelated document's
leftover Custom zoom.

**Does an automatic fit ever write a persisted preference?** Traced
directly in code, not assumed: `ImageDocument::applyInitialFitZoom()`
and `reapplyFitMode()` (`src/document/ImageAdapter.cpp`) only ever park
`m_zoomMode` at `FitInView`, `FitToWidth`, or `Actual` — an *automatic*
fit **never** produces `ZoomMode::Custom`. `Custom` is set only by an
explicit `zoomIn()`/`zoomOut()`/pinch, or by *restoring* an already-
persisted Custom value. `MainWindow::closeEvent()` captures
`doc->zoomMode()`/`zoomFactor()` **unconditionally** — for both the
per-file `RecentEntry` and the per-type `DocumentTypeDefault` — so
today's per-type snapshot picks up whatever the document's live zoom
happens to be at close, explicit or automatic, with no distinction. The
corroborating second report's drifting percentage is exactly this: a
real user naturally zooms different images to different manual
percentages in the course of ordinary use, and whichever one they
closed *last* silently became the template for the *next*, unrelated
image. The fix below (Option C) answers the owner's question directly
from this trace, not instinct: **per-type capture never writes
`Custom`/`windowGeometry` in the first place**, symmetric with per-type
restore never reading them — so the question "was this particular
Custom value reached automatically or explicitly" stops mattering,
because neither is captured into the per-type slot either way. Per-file
capture is intentionally left writing everything unconditionally
(explicit or automatic): "reopen this exact file where I left it" is
meaningful regardless of how that state was reached.

## Options

- **A — Status quo.** Per-type restore applies every captured field
  unconditionally, including raw `Custom` zoom and `windowGeometry`.
  Rejected: this is the reported bug.
- **B — Compare content size and skip when "materially different."**
  Persist the content size that produced each per-type default; on
  restore, compute a similarity threshold and skip the zoom/geometry
  restore only when the new document's content size differs "enough."
  Considered (the bug brief explicitly floats this) and rejected for
  now: it requires a new persisted field (`DocumentTypeDefault
  ::contentSize`), a hand-tuned similarity threshold with no natural
  unit (percent difference? aspect-ratio delta? both?), and doesn't
  address the *conceptual* problem — even a same-size unrelated document
  manually zoomed to 80% for THAT viewing session doesn't mean the next
  document of the same size should also open at 80%. Revisit if B's
  extra precision turns out to be worth the schema/threshold cost.
- **C — Per-type default carries only content-independent fields, on
  both the read and write side.** `Custom` zoom mode and
  `windowGeometry` are dropped from the per-type *restore* path
  entirely (their *semantic* siblings — `FitInView`, `FitToWidth`,
  `Actual` — are kept, since those recompute against whatever document
  they're applied to and carry real cross-document meaning) — **and**,
  symmetrically, dropped from the per-type *capture* path in
  `closeEvent()`: a `Custom`-mode zoom or a window geometry is never
  written into `DocumentTypeDefault` in the first place, regardless of
  whether that state was reached by an explicit zoom action or an
  automatic fit (see the write-side trace above). Sidebar mode,
  markup-toolbar visibility, and `windowState` (toolbar/dock layout, not
  size) are unaffected on either side — they are genuine UI-preference
  state, not content-relative. Per-file capture/restore (reopening the
  *same* document) is untouched in every field on both sides: that is
  deliberate, single-document state, not a cross-document guess.

## Personas debate

- **Office non-technical user:** never manually sets "80% zoom" as a
  concept they'd expect to persist across unrelated files — they just
  want each file to look "right" when it opens. Option C matches that
  expectation exactly; Option A is the reported confusion. (Favours C.)
- **Older careful user:** would be alarmed by a small file opening in a
  window sized for something else, or oddly zoomed with no visible
  cause — "did I do something wrong?" Predictable per-document sizing
  (C) is reassuring; A is the opposite. (Favours C.)
- **Power migrator:** coming from Preview/Acrobat, expects each document
  to open at a sensible size/zoom for *that* document, not inherit a
  sibling's manual adjustment. Preview does not carry a "last custom
  zoom %" across unrelated images. (Favours C.)
- **Occasional user:** opens one file every few weeks; has no memory of
  what a *previous, different* file's zoom was, so a value that traces
  back to that unrelated file reads as arbitrary and unexplainable.
  (Favours C.)

## Admissible objections

- **Office user, reopening the SAME large scan repeatedly, per-file
  zoom lost — hypothetical, checked and not applicable:** Option C only
  touches the per-type *fallback* branch; per-file restore (the branch
  that fires when `RecentEntry::hasViewState()` is true for that exact
  path) is untouched, so a document the user has actually opened before
  keeps its own remembered Custom zoom and window geometry exactly as
  today. No user hits a regression here.
- **Power migrator, batch-opens many same-size images from one shoot,
  expects the second image to open at the zoom they just set for the
  first — real, but addressed differently:** per-type Custom restore
  helping this workflow was never reliable in the first place (it also
  wrongly fires for *differently*-sized images, which is the reported
  bug), and Option B's "materially different" carve-out would need to
  special-case exactly this same-size-batch case to preserve it while
  fixing the reported one — added complexity for a workflow with no
  existing test or user report backing it. Not addressed by this record;
  worth its own follow-up (content-aware batch zoom) if a real workflow
  surfaces it.

### Rejected as naked preference

- "Per-type window geometry memory is a nice touch, don't remove it." —
  rejected: names no concrete user/step/failure Option C causes; the
  concrete failure (huge window on a tiny image) is exactly what Option
  A causes and what this record fixes. `applyInitialWindowSize()`
  already computes a sane content-fit size once per-type geometry stops
  clobbering it (verified by the paired test below).

## Checkable threshold this record would establish

1. Opening a document of a type whose only prior per-type default has
   `zoomMode == ZoomMode::Custom` must **not** apply that factor; the
   document falls through to its own natural fit computation
   (`ImageDocument::applyInitialFitZoom()` / `PdfDocument`'s
   constructor-time `FitInView` default).
2. Opening a document of a type whose only prior per-type default
   carries a non-empty `windowGeometry` must **not** restore that
   geometry; the window is sized by `applyInitialWindowSize()`'s
   content-fit computation (1100×750 floor, 90%-of-screen ceiling).
3. Per-type `FitInView` / `FitToWidth` / `Actual` zoom modes, sidebar
   mode, markup-toolbar visibility, and `windowState` (dock/toolbar
   layout) continue to apply exactly as before — unaffected by this
   record.
4. Per-file restore (a document with `RecentEntry::hasViewState() ==
   true` for its own path) is unaffected in every field, including
   `Custom` zoom and `windowGeometry`.
5. Closing a document at a `Custom` zoom, or with any window geometry,
   never writes those two fields into that document type's
   `DocumentTypeDefault` — `closeEvent()`'s per-type snapshot leaves
   them at their "not captured" sentinel (`ZoomMode::Custom` +
   `zoomFactor <= 0.0`; empty `windowGeometry`) regardless of whether
   the live zoom at close time was reached explicitly or automatically.
   Per-file capture (`RecentEntry`) is unaffected — both fields are
   still captured unconditionally there.

Covered by `TestImageScale::ordinaryOpenIgnoresPersistedCustomTypeDefaultZoom`
and `TestImageScale::ordinaryOpenIgnoresPersistedTypeDefaultWindowGeometry`
in `tests/test_image_scale.cpp`, and UAT cases `uat_zoom_ind_040_*` /
`uat_vwr_0NN_*` (see the paired PR).

## Arbiter verdict + rationale

**Option C is adopted.** It directly matches the qualitative framing
`IDocument.h` already documents for per-type defaults ("all images open
at actual size" — a mode, not a percentage), fixes the reported bug with
no new persisted schema or hand-tuned similarity threshold (Option B),
and every admissible objection is either not applicable (per-file
restore untouched) or names a workflow with no current backing that can
be revisited separately. `DocumentTypeDefault`'s struct and persistence
layer (`src/settings/DocumentTypeDefaults.{h,cpp}`) are **not** changed
— `windowGeometry` (and a `Custom` `zoomMode`/`zoomFactor`) stay valid
persisted fields (existing settings on disk round-trip unaffected,
`tests/test_document_type_defaults.cpp` unaffected); only
`MainWindow::onCurrentDocumentChanged()`'s per-type *restore* branch and
`closeEvent()`'s per-type *capture* branch stop reading and writing
those two fields, symmetrically, for the per-type slot specifically.

## Evidence required to reopen

A concrete, checkable problem this exclusion causes — e.g. real user
reports that per-type Custom zoom or window-geometry memory across
same-type documents was a workflow they relied on and its removal is a
regression — plus a proposal for how to distinguish that case from the
reported bug (Option B's content-size-comparison approach, properly
threshold-justified, is the most likely shape) and owner sign-off.
