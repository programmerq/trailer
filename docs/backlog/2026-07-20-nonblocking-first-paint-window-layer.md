---
id: 2026-07-20-nonblocking-first-paint-window-layer
title: Make first paint non-blocking by deferring MainWindow's synchronous document queries
priority: P2
status: open
source: PR #107 "Honest scope" caveat (off-thread PDF open) — coordinator follow-up 2026-07-20
created: 2026-07-20
---

## Threshold

`MainWindow::addDocument` performs **no synchronous document query** (no
`pageCount()` / capabilities drain) on the open path; the **"Loading…" first
paint precedes `pageCount()` availability**, asserted by a deterministic test
that the placeholder is painted and the event loop spins **before** adoption.

Concretely, the deterministic test establishes:

- `addDocument` (and the open path it drives) issues no call that forces
  `ensureDocLoaded()` — no `pageCount()`, no capability drain, no
  `waitForFinished` on the doc-open worker at add time; and
- the placeholder view (`objectName == "pdfLoadingPlaceholder"`, per
  `PdfDocument::createView`) is painted and at least one event-loop turn (a
  paint or a `QTimer` tick) is processed **before** the doc-open worker is
  adopted (before `pageCount()` returns > 0).

Structural / ordering oracle only (no-synchronous-query + paint-before-adopt
ordering), never wall-clock — consistent with `docs/performance-budgets.md`
invariants "First-page render must not block on a full-file read" / "The whole
UI never blocks during long work" (B5/B6) and the perf-measurement ruling.
Phrased so it cannot be satisfied by another structural half-step that still
drains the worker synchronously at add time.

## Context

PR #107 (`claude/offthread-pdf-open-placeholder`) moved the residual
`QPdfDocument::load` **read** off the GUI thread (worker in
`PdfDocument::startDocOpen`, adopted via `ensureDocLoaded()`) and retired the
`QSKIP` in `tests/test_perf_gui_thread_io.cpp` with a real
`readThreads()`-excludes-GUI-thread assertion. It also landed the honest
"Loading…" placeholder + doc-ready swap infrastructure in
`PdfDocument::createView` / `adoptDocOpenResult` / `buildRealView`.

What #107 deliberately did **not** do (kept out of the low-conflict seam per
that item's guidance): `MainWindow::addDocument`
(`src/ui/MainWindow.cpp:~3978`) still reads `document->pageCount()`
synchronously at add time (in the `uxrecord::recordEvent` "document_opened"
payload), and `onCurrentDocumentChanged` runs synchronous capability queries
(`supportsFormFilling` etc.) right after. Because `pageCount()` forces
`ensureDocLoaded()` (which `waitForFinished()`s the worker), the GUI thread
**waits** (blocked, but not reading) for the open in the interactive path —
so wall-clock first-paint is unchanged from before #107, and the placeholder
is not actually observed on the normal open flow (the doc is adopted before
`createView` runs).

Doing this item means making first paint genuinely non-blocking: defer
MainWindow's synchronous document queries at `addDocument` time (e.g. drop the
`page_count` from the open-time `uxrecord` event or emit it after adoption;
let the sidebar / capabilities refresh off the existing
`CapabilityNotifier` / a doc-ready signal) so the GUI thread does not
`waitForFinished` on the open worker, and the placeholder is shown while the
worker loads. The `PdfDocument` side (placeholder container, `buildRealView`
swap on `QFutureWatcher::finished`, doc-ready notification) already exists
from #107; this item is the **window-layer** half.

### Sequencing

Land **after PR #95 (empty-window reuse) merges** — both touch the
open / window-targeting flow (`Application::openFiles` → `addDocument`), so
sequencing them avoids a collision on that surface. Coordinate before
starting.

### Anchors

- `src/ui/MainWindow.cpp:~3978` — `addDocument`, the synchronous
  `document->pageCount()` read at open time.
- `src/ui/MainWindow.cpp` `onCurrentDocumentChanged` — synchronous
  capability queries (`retargetExternalChangeMonitor`, sidebar/inspector
  `setDocument`, `refreshFormCapabilities`).
- `src/document/PdfAdapter.cpp` — `ensureDocLoaded()` (the
  `waitForFinished` sync-fallback), `createView` placeholder branch,
  `adoptDocOpenResult` (the swap + `CapabilityNotifier::notifyChanged`).
- PR #107 — the read-off-thread half and the placeholder infrastructure this
  item builds on.

## Provenance

Split out of PR #107's "Honest scope" caveat at the coordinator's request
(2026-07-20): #107 met the backlog threshold "no open read on the GUI thread"
and retired the QSKIP, but explicitly deferred the window-layer change that
would remove the GUI-thread *wait*. This item tracks that deferred work.
