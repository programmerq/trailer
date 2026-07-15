---
id: 2026-07-15-staged-image-open
title: Staged (async) image document open per ADR 0008
priority: P2
status: open
source: ADR 0008 (staged document open scheduling) — Item D, deferred from the OCR images PR
created: 2026-07-15
---

## Threshold

Per **ADR 0008** (staged document open). An image document must open in stages
rather than blocking the GUI thread on a full-resolution decode before first
paint: derive an immediate `contentSizeHint` from a header-only read, paint an
honest placeholder, and swap in the decoded pixmap off-thread. Declared
pass/fail:

1. **No GUI-thread full decode.** Opening an image performs **no** full-pixel
   `QImageReader::read()` on the GUI thread — only a header-only
   `QImageReader::size()` for the immediate `contentSizeHint`. Satisfies the
   structural proxy in `tests/test_perf_gui_thread_io.cpp` (no GUI-thread
   full-pixel decode on open).
2. **Honest placeholder.** `createView` paints a "Loading image…" placeholder
   immediately, sized from the header-only size hint, before any pixels are
   decoded.
3. **Off-thread decode + swap.** The full-resolution decode runs off the GUI
   thread (`QtConcurrent::run` + `QFutureWatcher::finished`) and swaps the
   placeholder for the real pixmap when it completes.
4. **Reworked view/zoom tests.** The ~5 synchronous view/zoom unit tests
   (`imageDocumentZoomResizesPixmap`, `imageDocumentZoomFitPage`,
   `imageDocumentZoomFitWidth`, `imageDocumentZoomReapplyFitMode`,
   `imageDocumentZoomResizeEventTriggersRefit`) in `tests/test_adapters.cpp`
   deterministically await the placeholder→pixmap swap before reading
   `label->pixmap()`, instead of reading it immediately after `createView`.

## Context

`ImageDocument`'s constructor currently does a **synchronous** full-resolution
`QImageReader::read()` on the GUI thread before first paint
(`src/document/ImageAdapter.cpp:237-248`), and `createView` paints
synchronously. This blocks first paint on the full decode.

A full staged-open implementation was **attempted this cycle** — header-only
`QImageReader::size()` for an immediate `contentSizeHint`, off-GUI-thread decode
via `QtConcurrent::run` + `QFutureWatcher::finished`, and an honest
"Loading image…" placeholder — but **reverted** because the async `createView`
broke ~5 established synchronous view/zoom unit tests in
`tests/test_adapters.cpp` (`imageDocumentZoomResizesPixmap`,
`imageDocumentZoomFitPage`/`Width`/`ReapplyFitMode`/`ResizeEventTriggersRefit`)
that read `label->pixmap()` immediately after `createView`.

Scope for reviving it: implement staged open **and** rework those ~5 tests to
deterministically await the placeholder→pixmap swap; decide the blocking
semantics for a user-initiated zoom that arrives mid-decode; and satisfy the
structural proxy in `tests/test_perf_gui_thread_io.cpp` (no GUI-thread
full-pixel decode on open).

Grounded by `docs/decision-records/0008-staged-document-open-scheduling.md` and
`docs/decision-records/0013-image-ocr-pipeline-lazy-window-bounded-cache.md`.

File pointers:
- `src/document/ImageAdapter.cpp:237-248` — synchronous full-res
  `QImageReader::read()` in the `ImageDocument` constructor / `createView`.
- `tests/test_adapters.cpp` — the ~5 synchronous view/zoom tests to rework.
- `tests/test_perf_gui_thread_io.cpp` — structural proxy the staged open must
  satisfy.
