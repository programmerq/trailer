---
id: 2026-07-13-startup-hang-large-pdf
title: Opening a large PDF freezes the whole app for minutes (synchronous GUI-thread parse ×2 + all-pages annotation sweep)
priority: P0
status: open
source: v0.3.0 real-Mac dogfood report (2026-07-13)
created: 2026-07-13
---

## Threshold

Opening a large PDF must never block the GUI thread on a full-file parse. The
binding structural invariants in `docs/performance-budgets.md:56-66` (B5/B6
region) are the gate: **first-page render must not block on a full-file read**
and **the whole UI never blocks during long work**.

Declared, deterministic pass/fail proxies (per the perf-measurement ruling —
agent-measured locally + reviewer check, CI enforces only structural proxies,
never a wall-clock assertion):

1. **No all-pages walk before first paint.** A page-visit counter added to
   `PdfEditor::readAnnotations()` (`src/document/PdfEditor.cpp:1082-1089`)
   reads **0 pages** during `DocumentRegistry::open()` — annotations are read
   lazily/off-thread, not eagerly across every page at construction.
2. **No second full-file parse before viewer-ready.** An editor-parse counter
   (incremented in `PdfEditor::load`, `src/document/PdfEditor.cpp:33-38`) is
   still **0** at the moment `pageCount()` first returns > 0 — the qpdf
   `processFile` load is deferred until the first edit/annotation access.
3. **No synchronous file IO on the GUI thread.** Flip the live `QSKIP` in
   `tests/test_perf_gui_thread_io.cpp:93-98` to a real assertion once open moves
   off-thread: `InstrumentedIODevice::readThreads()` (`tests/perf_iodevice.h`)
   contains **no** entry equal to the GUI thread.

Verified: on a large text-layer PDF, 0 pages walked and editor-parse-count 0
before first-page paint, and no read thread equals the GUI thread. Local
wall-clock split (`QPdfDocument::load` vs `PdfEditor::load`+`readAnnotations`)
reported in the PR per the perf-measurement ruling.

## Context

Owner ran v0.3.0 on macOS against a 142 MB text-layer PDF and hit a multi-minute
unresponsive hang pinning 100% of one core — the worst item in the dogfood pass.

Root cause: document open is fully synchronous on the GUI thread and does the
heavy work three times before the view exists.
`Application::openFiles` calls `m_registry.open(path)` synchronously
(`src/app/Application.cpp:140`); the only `QtConcurrent::run` in `MainWindow` is
the *save* path (`src/ui/MainWindow.cpp:2215`), not open. That flows through
`DocumentRegistry::open()` (`src/document/DocumentRegistry.cpp:14-18`) →
`PdfAdapter::open()` (`src/document/PdfAdapter.cpp:1524-1540`), which constructs
`PdfDocument` inline. The `PdfDocument` constructor
(`src/document/PdfAdapter.cpp:146-166`) then:

- parses the whole file with `QPdfDocument` (`PdfAdapter.cpp:149`, parse #1),
- parses the *same* file again with qpdf `processFile` via
  `m_editor->load(m_path)` (`PdfAdapter.cpp:157` → `PdfEditor.cpp:33-38`,
  parse #2 — only needed for editing/round-trip), and
- eagerly sweeps **every page** via `readAnnotations()`
  (`PdfAdapter.cpp:158-160` → `PdfEditor.cpp:1082-1089`), where
  `getAllPages()` + per-page `getMediaBox`/`getKey("/Annots")` force
  whole-document object resolution in qpdf.

This is *not* thumbnails (lazy per-visible-item,
`src/ui/ThumbnailModel.cpp:143-164`), *not* OCR/ML (skipped on text-layer docs,
`src/ui/OcrController.cpp:74-75`), and *not* search pre-indexing (lazy on first
query, `PdfAdapter.cpp:777-779`). It is the qpdf editor load + all-pages
annotation read in the constructor.

Already a documented gap: `tests/test_perf_gui_thread_io.cpp:9-15` states open is
"fully SYNCHRONOUS on the calling thread … there is no worker-thread open seam
yet" and `QSKIP`s the target invariant.

Fix direction (not to implement under this item alone): make
`m_editor->load` + `readAnnotations()` lazy (first edit / first annotation
access) and/or move `registry.open()` onto a worker with a placeholder first
page, satisfying `docs/performance-budgets.md:56-66`. The background-scheduling
research theme in `docs/research/2026-07-13-ux-research-agenda.md` feeds this
item's decision on QoS/off-main-thread budgets.

Cross-links:
- `docs/performance-budgets.md:56-66` (binding structural invariants), B5/B6
  rows (`:123-132`).
- `tests/test_perf_gui_thread_io.cpp` (live `QSKIP` this item retires) and
  `tests/perf_iodevice.h` (`InstrumentedIODevice` thread-identity proxy).
- `2026-07-12-gate-reference-rig-and-corpus` — the reference rig + `docs/perf/
  corpus/` needed for the local wall-clock measurement (today's corpus tops out
  at `text_20page.pdf`; a large fixture is required to reproduce).
- `2026-07-13-search-current-page-seed` — shares the same synchronous,
  page-0-anchored `PdfAdapter.cpp` open/search seam.

## Provenance

v0.3.0 real-Mac dogfood report, 2026-07-13. Root-cause file:line refs from the
grounded investigation pass against `a4abbcf`.
