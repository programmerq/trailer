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

### Done since this item was filed

- The all-pages `readAnnotations()` sweep (proxy #1, the dominant cost) is
  now run **off** the GUI thread on an isolated, throwaway qpdf instance
  (`PdfDocument::startAnnotationLoad()`, `src/document/PdfAdapter.cpp`,
  landed on `fix/startup-hang-large-pdf`). `annotations()` kicks the worker
  and returns the empty store immediately; a GUI-thread finished slot commits
  the result via a single batched `AnnotationStore::addBatch`. The GUI thread
  no longer blocks on the sweep at view-attach. See decision record 0006's
  Update note.

### Still deferred (this item)

As of the P0 fix + that follow-on, the `PdfDocument` constructor runs only
`QPdfDocument::load`; the qpdf parse is lazy and the sweep is on a worker.
What remains synchronous on the GUI thread at open is:

1. `QPdfDocument::load(path)` in the ctor — a bounded progressive read
   (~1.8s on a 195MB/2500-page file) that yields `pageCount()` + a page-0
   render. This is the residual open-path IO this item targets.
2. `MainWindow::onCurrentDocumentChanged` querying `supportsFormFilling()`
   (`src/ui/MainWindow.cpp:2875,2972`) when a document becomes current —
   this triggers the lazy qpdf `processFile` parse (~0.55s) on the GUI
   thread at *view-attach*. (`annotations()` no longer blocks the GUI
   thread — its sweep is now on a worker.) Moving open off-thread should
   also warm this parse on the worker (or attach the view against a
   placeholder and populate on the worker), so the GUI thread never blocks
   on it either — which needs a capabilities-refresh signal so the toolbar
   updates when the off-thread form detection completes.

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

## Minor tracked items (found while landing the B1 async-load fix)

Both are low-priority follow-ons surfaced on `fix/startup-hang-large-pdf`;
neither blocks this item.

- **(a) Save-As suffix omitted during the load window (cosmetic, no data
  loss).** If Save-As is invoked while the async annotation sweep is still
  in flight, the auto-suffix logic (`_marked` / `_signed`) can be skipped
  because the marked/signed state is derived before the sweep commits, so
  the suggested filename lacks the suffix (`src/ui/MainWindow.cpp:2256-2270`).
  Purely cosmetic — the sync-ensure on the write path still flushes the full
  annotation set into the saved file, so nothing is lost; only the *suggested
  name* is affected. Fix: force `ensureAnnotationsLoadedSync()` (or read the
  committed state) before computing the suffix.
- **(b) Chunked / yielding `addBatch` to smooth the one-shot GUI hitch.**
  `AnnotationStore::addBatch()` appends the whole loaded set and emits a
  single coalesced `changed()`. On a pathological 1M-annotation document the
  batched GUI commit is a one-shot ~125–200ms hitch. A chunked / yielding
  `addBatch` (commit in bounded slices across event-loop turns, coalescing
  refreshes) would spread that cost so the GUI never stalls for a visible
  frame on such documents.

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
