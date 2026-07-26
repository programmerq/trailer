---
id: 2026-07-26-capture-origin-zoom-restore-override
title: Per-type / per-file zoom-restore in onCurrentDocumentChanged can override a capture-origin image's forced Actual-Size default
priority: TBD
status: open
source: Investigation into a macOS-only pendingCaptureDprConsumedOncePerBatch failure on PR #122-adjacent work (2026-07-26), found while tracing MainWindow::onCurrentDocumentChanged
created: 2026-07-26
---

## Threshold

TBD — declare before work begins. Candidate shape: open an ordinary image,
zoom it to a non-Actual factor (e.g. 50%, Custom mode), close the window
(so `MainWindow::closeEvent()` persists it as the Image
`DocumentTypeDefault` / `RecentEntry`), then take a screenshot / paste from
clipboard. The screenshot must open at Actual Size (1:1) regardless of the
prior image's persisted zoom — `ImageDocument::isCaptureOrigin()` documents
this as the intended contract ("Screenshot / clipboard-origin images open
at Actual Size... matching Preview's default for screen captures").

## Context

`MainWindow::onCurrentDocumentChanged()` (`src/ui/MainWindow.cpp:3873-3922`)
restores a newly-opened document's zoom from persisted state in priority
order: per-file `RecentEntry` (line 3882,
`doc->applyZoomState(entry.zoomMode, entry.zoomFactor)`), then per-type
`DocumentTypeDefault` (line 3908, same call). Both branches gate only on
`doc->filePath().isEmpty()` and `doc->documentType() != DocumentType::Unknown`
— **neither checks `ImageDocument::isCaptureOrigin()`** before calling
`applyZoomState()`, which sets `m_initialZoomApplied = true` and whatever
zoom mode/factor the persisted entry carries
(`src/document/ImageAdapter.cpp`, `applyZoomState`). Since this restore
runs synchronously in `onCurrentDocumentChanged` (called from
`DocumentView::addDocument`, before the async decode has necessarily
landed), it can win the race against `applyInitialFitZoom()`'s
capture-origin branch and permanently lock in a **non-Actual** zoom for a
capture-origin document — the "always Actual Size" screenshot contract
would silently not hold.

A capture-origin image gets a real backing file (a temp file for the
clipboard/screenshot import), so `doc->filePath().isEmpty()` is false and
it is NOT otherwise exempted from this restore path.

**Not confirmed as the cause of the macOS `pendingCaptureDprConsumedOncePerBatch`
CI failure this investigation started from** — in that unit test,
`DocumentTypeDefaults` cannot have any persisted state (the sandboxed test
`HOME` is fresh per run and nothing in `tests/test_image_scale.cpp` ever
calls `MainWindow::close()` to populate it), so this exact mechanism was
ruled out for that failure specifically. It is filed here because it is a
real, independently-discovered gap against the documented capture-origin
contract, worth its own repro and fix.

## Suggested fix shape (not yet implemented)

Add a capture-origin guard to both `applyZoomState()` call sites in
`onCurrentDocumentChanged()` — e.g. skip the restore (or defer it) when
`dynamic_cast<ImageDocument*>(doc)` is non-null and
`->isCaptureOrigin()` is true — mirroring how `applyInitialFitZoom()`
already special-cases capture-origin. Needs its own G1 threshold and repro
before implementation.
