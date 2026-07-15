---
id: 2026-07-15-offthread-pdf-open-placeholder
title: Move the residual PDF-open read off the GUI thread behind a placeholder first page
priority: P2
status: open
source: follow-up split from 2026-07-13-startup-hang-large-pdf (proxy #3)
created: 2026-07-15
---

## Threshold

`InstrumentedIODevice::readThreads()` (`tests/perf_iodevice.h`) contains
**no** entry equal to the GUI thread after a document open driven from the
GUI thread — i.e. the live `QSKIP` in
`tests/test_perf_gui_thread_io.cpp` is retired and replaced by a real
assertion. The whole UI must never block during a document open
(`docs/performance-budgets.md:56-66`, B5/B6).

## Context

This is the deferred third proxy (#3) split out of the P0 startup-hang
item (`2026-07-13-startup-hang-large-pdf.md`) when that item shipped lazy
editor/annotation loading. See decision record
`docs/decision-records/0006-defer-offthread-pdf-open.md` for why the
worker+placeholder work was deferred rather than done in the same change.

As of the P0 fix, the `PdfDocument` constructor no longer runs the two
heavy whole-document passes — the qpdf `processFile` parse and the
all-pages `readAnnotations()` sweep are lazy
(`PdfDocument::ensureEditorLoaded()` / `ensureAnnotationsLoaded()`,
`src/document/PdfAdapter.cpp`). What remains synchronous on the GUI thread
at open is:

1. `QPdfDocument::load(path)` in the ctor — a bounded progressive read
   (~16ms on a 115MB/8000-page file) that yields `pageCount()` + a page-0
   render. This is the residual open-path IO this item targets.
2. `MainWindow::onCurrentDocumentChanged` querying `supportsFormFilling()`
   (`src/ui/MainWindow.cpp:2875,2972`) and `annotations()` (`:2985`) when
   a document becomes current — these trigger the now-lazy qpdf parse and
   annotation sweep on the GUI thread at *view-attach*, shortly after
   `open()` returns. Moving open off-thread should also address where
   these lazy loads run (e.g. warm them on the worker before the view is
   attached, or attach the view against a placeholder and populate on the
   worker), so the GUI thread never blocks on them either.

## Fix direction

Move `Application::openFiles` → `DocumentRegistry::open()`
(`src/app/Application.cpp:140`) onto a `QtConcurrent::run` worker (mirror
the existing save path at `src/ui/MainWindow.cpp:2215`), render a
placeholder first page immediately, and swap in the real view + warm the
lazy editor/annotation loads on the worker when the read completes. Then
flip the `test_perf_gui_thread_io` `QSKIP` (lines ~88-108) to a hard
`readThreads()`-excludes-GUI-thread assertion. Decide QoS / cancellation
on close per the background-scheduling research theme
(`docs/research/2026-07-13-ux-research-agenda.md`).

## Cross-links

- `docs/backlog/2026-07-13-startup-hang-large-pdf.md` — the P0 parent this
  splits from (proxies #1 and #2 shipped there).
- `docs/decision-records/0006-defer-offthread-pdf-open.md` — the deferral
  rationale and the residual this item closes.
- `tests/test_perf_gui_thread_io.cpp` + `tests/perf_iodevice.h` — the
  `QSKIP` this item retires and the thread-identity proxy that gates it.
- `docs/performance-budgets.md:56-66` (B5/B6) — the binding invariant.

## Provenance

Split from the 2026-07-13 P0 startup-hang item when its lazy-loading fix
landed (2026-07-15). The residual GUI-thread read and the view-attach
lazy-load call sites are the remaining gap toward the off-thread invariant.
