---
name: trailer-startup-hang-shipped
description: P0 startup-hang-large-pdf FIXED + MERGED via PR #63 (2026-07-15) — annotation sweep + qpdf parse + AcroForm detection moved off the GUI thread; residual QPdfDocument::load + loading placeholder is P2
metadata:
  type: project
---

The P0 "startup-hang-large-pdf" (opening a large annotated PDF froze the app for minutes) is FIXED and MERGED: PR #63 (https://github.com/programmerq/trailer/pull/63), merged 2026-07-15. Was the top item in [[trailer-followup-docket]] / [[trailer-v030-dogfood-synthesis]].

**Root cause:** the `PdfDocument` ctor did three synchronous whole-document passes on the GUI thread at open — `QPdfDocument::load`, qpdf `processFile`, and an all-pages annotation sweep (`PdfEditor::readAnnotations`). On a 195MB / 1,000,000-annotation synthetic the sweep alone was ~12–16s (linear in annotation count, whole-object-graph resolution).

**Fix (PR #63):** the annotation sweep runs off the GUI thread on an ISOLATED throwaway qpdf instance (shares nothing with the GUI-thread editor → no locks; consumers repaint on `AnnotationStore::changed`). The qpdf editor parse + AcroForm detection also moved off-thread (owner feedback on the PR) with a new `CapabilityNotifier` signal enabling the forms toolbar when ready; `supportsFormFilling()` returns not-ready until then. `m_editor` is `shared_ptr` and adopted parse-only across the QFutureWatcher barrier (Option B — annotation-swept instance NOT retained, so steady-state RSS unchanged). Synchronous open on the stress fixture: ~16s → ~1.6s (just `QPdfDocument::load`; ~16ms on normal PDFs).

**B1 data-loss bug (caught in local review, fixed in the same PR):** undoing an annotation drawn DURING the async load window wiped all file annotations — snapshot-based `AnnotationStore` undo vs. `addBatch` populating with no history frame. Fixed with an `AnnotationStore` pre-edit hook that commits the loaded baseline (via `ensureAnnotationsLoadedSync()`) before the first user edit's snapshot; RED→GREEN regression test in `test_adapters.cpp`.

**Design + deferral:** `docs/decision-records/0006-defer-offthread-pdf-open.md`. Perf enforced by deterministic proxies only (per [[trailer-perf-measurement-ruling]]): structural tests in `tests/test_perf_lazy_open.cpp` (sweep/parse not on the sync open path, run on a worker thread; forms capability false-then-true) — no CI wall-clock gate; wall-time measured locally.

**RESIDUAL / P2 FOLLOW-UP** (`docs/backlog/2026-07-15-offthread-pdf-open-placeholder.md`): back `QPdfDocument::load` off-thread + a loading placeholder + flip the `test_perf_gui_thread_io` QSKIP; plus minor nits — Save-As `_marked`/`_signed` suffix omitted if Save-As invoked during the load window (cosmetic), a chunked/yielding `addBatch` to avoid the ~125–200ms one-shot GUI commit hitch on 1M-annotation docs, and a cosmetic Fill-Forms menu-checkmark flicker when switching between two large form PDFs.

**Process note:** #63 carried the 2026-07-14 review-before-push SKILL amendment (grounded in [[trailer-review-before-push-policy]]). Peer-relayed 2026-07-15 policy edits (AGENTS.md G2 / SKILL inline-grab-evidence, `docs/backlog/README` closures-ride-code-PRs convention) and 5 backlog dispositions were HELD (unverified peer authority; auto-mode classifier flagged writing peer-relayed "owner rulings" into instruction files) and dropped from #63 per coordinator — they ride a future session that receives them in its start-of-session brief. Reinforces [[trailer-verify-remote-after-push]] (remote SHA cited on every push).
