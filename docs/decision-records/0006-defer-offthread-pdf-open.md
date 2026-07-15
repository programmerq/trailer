# 0006 — Defer off-GUI-thread PDF open; ship lazy editor/annotation loading first

- **Status:** accepted
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-15
- **Date accepted / superseded:** 2026-07-15

## Context

Opening a large PDF froze the whole app for minutes
(`docs/backlog/2026-07-13-startup-hang-large-pdf.md`, P0). The
`PdfDocument` constructor ran three synchronous whole-document passes on
the GUI thread before the view existed
(`src/document/PdfAdapter.cpp:146-166`):

1. `QPdfDocument::load(path)` — a bounded progressive read (~16ms on a
   115MB/8000-page file) that yields `pageCount()` + a page-0 render.
   Cheap and lazy inside pdfium. **Kept in the ctor.**
2. `PdfEditor::load(path)` — a full qpdf `QPDF::processFile` parse
   (~29ms) needed only for editing / forms / annotation round-trip.
3. `readAnnotations()` — an all-pages `/Annots` sweep (~137ms, ~75% of
   open cost) that forces whole-document object resolution in qpdf. This
   is the dominant cost and the multi-minute-hang root cause.

The backlog scoped two fix directions: (a) make the editor parse +
annotation sweep lazy, and (b) move `registry.open()` onto a worker
thread with a placeholder first page. It explicitly said the
worker+placeholder was **not** to be implemented "under this item alone."

This change ships (a): passes 2 and 3 are now deferred to first genuine
access via `PdfDocument::ensureEditorLoaded()` /
`ensureAnnotationsLoaded()` (`src/document/PdfAdapter.cpp`). The
constructor keeps only pass 1. What remains synchronous on the GUI thread
at open is that residual QPdfDocument bounded progressive read (~16ms).

This record adjudicates whether to also do (b) — moving that residual
read off the GUI thread behind a placeholder — now, or defer it.

## Options

- **A — Defer (b); ship lazy loading only.** Remove the two heavy passes
  from the open path; leave the ~16ms QPdfDocument read synchronous.
  Capture (b) as a separate P2 follow-up
  (`docs/backlog/2026-07-15-offthread-pdf-open-placeholder.md`).
- **B — Do (b) now.** Move `registry.open()` onto a `QtConcurrent`
  worker, render a placeholder first page, and swap in the real view
  when the read completes; flip the `test_perf_gui_thread_io` QSKIP to a
  hard off-thread assertion in the same change.
- **C — Do neither / minimal.** Leave open fully synchronous and only
  document the hang. (Rejected on its face: it ships the P0.)

## Personas debate

- **Office non-technical user:** Opened a big shared PDF and watched the
  app hang with a spinning cursor for minutes — reads it as a crash.
  Cares only that the window becomes usable quickly; does not distinguish
  16ms from instant. Option A already removes the perceptible freeze.
- **Older careful user:** Fears doing something wrong during the hang
  (force-quitting, opening twice). A first paint that arrives promptly
  reassures them; a residual 16ms is imperceptible. No stake in whether
  that last read is on a worker thread.
- **Power migrator:** Opens 200MB scanned/annotated PDFs from another
  tool and expects them to open as fast as that tool. Would eventually
  notice a sub-frame hitch on a truly huge file and want (b) — but not at
  the cost of a regression-prone threading change landing under time
  pressure. Fine with A now, B tracked.
- **Occasional user:** Opens a PDF a few times a month. Unaffected by a
  16ms read; was very affected by a multi-minute hang. A fully covers
  them.

## Admissible objections

- **The dominant cost must never be forced at open** — any user opening a
  large annotated PDF, at construction, hits the ~137ms all-pages sweep
  that triggers the multi-minute hang. Failure: unresponsive window.
  This objection is *binding* and drives keeping proxy #1 (the sweep)
  deferred unconditionally; it is satisfied by A.
- **Power migrator, huge-file open, residual read** — on a 200MB+ file
  the residual QPdfDocument read can still cost more than one frame on
  the GUI thread. Failure: a brief hitch. Real but bounded, and
  addressed by the tracked follow-up (b); does not justify blocking A.

### Rejected as naked preference

- "Open should always be fully threaded on principle." — rejected:
  names no user, step, or failure that A leaves unaddressed; the residual
  read is bounded and the follow-up is tracked.

## Checkable threshold this record would establish

At `DocumentRegistry::open()` on the reference corpus, with the
always-compiled `PdfEditor` instrumentation:

- `PdfEditor::annotationPageVisits() == 0` immediately after open
  (the all-pages sweep did not run at construction), and
- `PdfEditor::parseCount() == 0` at the moment `pageCount()` first
  returns > 0 (the qpdf processFile parse did not run at open).

Both are enforced by `tests/test_perf_lazy_open.cpp`. The off-thread
invariant (`test_perf_gui_thread_io`'s `readThreads()` contains no GUI
thread) stays a documented `QSKIP` until the follow-up lands — this
record does **not** commit to it.

## Arbiter verdict + rationale

**Option A.** The binding objection — the ~137ms all-pages sweep (proxy
#1) must never run at open — is fully satisfied by lazy loading, which
also defers the ~29ms qpdf parse (proxy #2) off the construction path.
That removes the entire perceptible freeze for every persona. The only
objection B additionally answers is the power migrator's bounded
sub-frame hitch on very large files from the residual ~16ms QPdfDocument
read — a real but bounded cost. B is a large, UI-touching, merge-conflict-prone
change (worker thread + placeholder first page + view swap) for that
small residual, and the backlog itself scoped the worker+placeholder out
of "this item alone." Shipping A now retires the P0; B is captured as a
P2 follow-up (`docs/backlog/2026-07-15-offthread-pdf-open-placeholder.md`)
with the `test_perf_gui_thread_io` QSKIP as its acceptance flip.

Residual accepted under A: `MainWindow::onCurrentDocumentChanged` queries
`supportsFormFilling()` (`src/ui/MainWindow.cpp:2875,2972`) and
`annotations()` (`:2985`) synchronously when a document becomes current,
so in the integrated app the qpdf parse and the sweep execute on the GUI
thread shortly *after* `open()` returns (at view-attach), not in the
ctor. The construction/registry-open path — what the structural tests and
the local wall-clock split measure — is clean. `supportsFormFilling()` is
the one capability probe that intentionally triggers the lazy editor
parse (there is no cheaper way to detect an AcroForm, and it is queried
at open); it never triggers the annotation sweep, which stays deferred.
Fully removing the parse + sweep from the GUI thread is exactly the
follow-up (b).

## Evidence required to reopen

A measured, reproducible GUI-thread stall at open attributable to the
residual QPdfDocument read (not the now-lazy passes) on a corpus fixture,
with a wall-clock split showing it exceeds one frame budget — plus owner
sign-off to spend the threading complexity. That evidence would promote
the follow-up (b) from P2 and flip the `test_perf_gui_thread_io` QSKIP to
a live assertion.
