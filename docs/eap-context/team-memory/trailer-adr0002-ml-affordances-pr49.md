---
name: trailer-adr0002-ml-affordances-pr49
description: ADR 0002 (ML progress/cancel/missing-model) ACCEPTED 2026-07-12 via persona/arbiter machinery; implemented on feat/ml-affordances as PR #49 MERGED 2026-07-12 (CI green on rebased head 175ca5b); two disclosed follow-ups remain (rebased 175ca5b)
metadata:
  type: project
---

ADR 0002 (docs/decision-records/0002-ml-background-removal-progress-cancel.md) moved `proposed` → `accepted` on 2026-07-12 via the persona/arbiter machinery (four lenses). Implemented on branch `feat/ml-affordances`, opened as **PR #49** (https://github.com/programmerq/trailer/pull/49), MERGED to main 2026-07-12 (head 175ca5b, CI green — Linux+Windows/Wine+clang-format). Remote head SHA `175ca5b16557cbf46d8cad07a7d27aa8bc175264` (ls-remote verified) (rebased onto main 268436d over #47+#48 on 2026-07-12; earlier pre-rebase head was 6471377). CI GREEN (Linux build+unit tests, Windows cross-build+Wine tests, clang-format all pass; release/packaging jobs skipped on PR events).

**What shipped:** status-bar-only feedback (no modal, per CONVENTIONS #12). New `src/ui/MlProgressWidget.*` = determinate "N/M pages" bar + Cancel ✕ + terminal message, revealed only after ~1s (B5). `OcrController` gained per-batch epoch identity, batch progress signals, per-page no-partial-write guard (cancel keeps completed pages, discards only the interrupted page — B6 applied at page granularity for OCR), `cancelBatchTrackedHandles()` (ambient auto-OCR never cancelled by a batch cancel), and a state-driven non-modal missing-model hint ("install language pack") replacing the old silent no-op at the former OcrController.cpp:207. Cancel key is `Ctrl+.` only; bare Esc intentionally unbound (accidental-loss trap). Gates G1–G6 all green; full suite 49/49. MainWindow +64/−2 (additive; `m_mlIndicator` untouched). Post-rebase: Recognize Text now uses #48's `makeDisabledAction` helper (benefit tooltip preserved, G6 holds); coexists with #47's dirty-close veto signal (decoupled — veto on `documentCloseRequested`, our ML reset on `currentDocumentChanged`). Full suite green after rebase (16 UAT + 34 unit).

**Open follow-ups (disclosed in the PR, NOT done):**
1. Route the **background-removal op** (the ADR's named exemplar) through `MlProgressWidget` — indeterminate spinner + elapsed-time text + cancel, plus the G3 "post-cancel image bytes == pre-op" test arm. The widget is general and ready to adopt it; OCR is fully wired, background removal is not.
2. Missing-model hint re-derivation on **page scroll** currently uses a 150ms poll (mirroring `Sidebar`'s page-sync timer) because `IDocument` is not a QObject and exposes no page-changed signal; could be upgraded to a real signal.

Belongs on the [[trailer-followup-docket]]. Related: [[trailer-requirements-summary]], [[trailer-perf-measurement-ruling]] (progress/cancel verified structurally, not by CI wall-time), [[trailer-review-before-push-policy]] (two review lenses ran pre-push; a P0 batch-counter-corruption and a P0 stale-hint bug were caught and fixed before push).
