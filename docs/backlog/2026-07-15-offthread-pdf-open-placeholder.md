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
  (`PdfDocument::startBackgroundLoad()`, `src/document/PdfAdapter.cpp`,
  landed on `fix/startup-hang-large-pdf`). `annotations()` kicks the worker
  and returns the empty store immediately; a GUI-thread finished slot commits
  the result via a single batched `AnnotationStore::addBatch`. The GUI thread
  no longer blocks on the sweep at view-attach. See decision record 0006's
  Update note.
- **(PR #63 owner feedback) The qpdf editor parse + AcroForm detection are
  now ALSO off the GUI thread.** `supportsFormFilling()` used to force a
  synchronous ~0.55s qpdf `processFile` parse at view-attach; it now returns
  a provisional `false` until the SAME background worker parses a separate
  parse-only editor and reads AcroForm presence. On completion the GUI thread
  adopts that editor as `m_editor` and fires `capabilitiesChanged()`
  (`CapabilityNotifier`), which re-runs `MainWindow::refreshFormCapabilities()`
  so the forms toolbar enables a moment after open instead of blocking it.
  The adopted editor is parse-only (never annotation-swept), so steady-state
  RSS stays modest (Option B — DR 0006). The sync edit/save paths block on the
  in-flight load and adopt its editor if a mutation lands mid-window. The
  capabilities-refresh signal that this item's fix direction called for has
  therefore landed as part of this change.

### Still deferred (this item)

As of the P0 fix + those follow-ons, the `PdfDocument` constructor runs only
`QPdfDocument::load`; the qpdf parse + form detection AND the annotation sweep
are all on a worker. What remains synchronous on the GUI thread at open is the
single item this backlog now targets:

1. `QPdfDocument::load(path)` in the ctor — a bounded progressive read
   (~16ms typical; ~1.8s on a 195MB/2500-page file) that yields `pageCount()`
   + a page-0 render. This is the residual open-path IO this item targets.
   (The ~0.55s forms parse that used to sit here has moved to the worker —
   see "Done since" above.) Moving this last read off the GUI thread behind a
   placeholder first page is the remaining work.

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

- The P0 parent this splits from (`2026-07-13-startup-hang-large-pdf`, now
  deleted) — proxies #1 (off-thread annotation sweep) and #2 (off-thread qpdf
  parse + AcroForm detection) shipped and were verified-closed via PR #63
  (merge `852e9e3`); this item carries the surviving residual, proxy #3.
- `docs/decision-records/0006-defer-offthread-pdf-open.md` — the deferral
  rationale and the residual this item closes.
- `tests/test_perf_gui_thread_io.cpp` + `tests/perf_iodevice.h` — the
  `QSKIP` this item retires and the thread-identity proxy that gates it.
- `docs/performance-budgets.md:56-66` (B5/B6) — the binding invariant.

## Provenance

Split from the 2026-07-13 P0 startup-hang item when its lazy-loading fix
landed (2026-07-15). The residual GUI-thread read and the view-attach
lazy-load call sites are the remaining gap toward the off-thread invariant.
