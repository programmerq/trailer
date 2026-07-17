---
id: 2026-07-12-untitled-close-save-regression
title: Non-blocking progress for large saves (untitled-close-save half resolved)
priority: P2
status: open
source: affordances session harvest; consolidated docket 2026-07-10
created: 2026-07-12
---

## Threshold

Large saves show non-blocking progress: the save does not freeze the UI
thread and a reviewer can observe progress during a large save. TBD —
declare the concrete progress-widget / timing pass/fail line before work
begins.

> The other half of this item — *a headless regression test covers the
> untitled-document close-save path* — is **RESOLVED** (see Update below).
> Only the non-blocking-progress work remains open.

## Update — 2026-07-16 (untitled-close-save half resolved)

Branch `fix/untitled-close-save`. The silent-data-loss bug where a macOS
"New from Clipboard" / "Acquire from screenshot" document closed without a
prompt (it was clean-on-creation, backed only by a transient temp file, so
the dirty-close gate never fired and the pasted content was discarded
silently — an ADR-0004 violation) is fixed, and the close-save path now has
headless regression coverage.

Fix: an `isUntitled()` concept on `IDocument` (a content-bearing doc whose
only backing is a transient temp file). The close-decision gate
(`MainWindow::closeEvent` filter + the `documentCloseRequested` veto) now
prompts Save-As / Discard / Cancel when `isDirty() || isUntitled()`, Save on
an untitled doc routes through Save-As (never overwrites the temp file), and
a successful Save-As clears the untitled state. Auto-save skips untitled
docs. `ImageDocument` presents a clean "Untitled" title instead of the UUID
temp filename.

Headless regression slots (in `tests/uat/test_uat_foundations.cpp`, all
green):

- `uat_fnd_014_closeUntitledTabPromptsAndCancelKeepsIt` — an untitled
  (clean) doc prompts on close; Cancel vetoes and keeps it untitled.
- `uat_fnd_014_closeUntitledTabDiscardDropsDoc` — Discard drops it without
  writing the temp file.
- `uat_fnd_014_closeUntitledTabSaveRoutesThroughSaveAs` — Save routes
  through Save-As and writes the CHOSEN path, not the temp file.
- `uat_fnd_014_untitledImageDocReportsUntitledAndClearsOnSave` — drives the
  real production entry `Application::openFiles(paths, markUntitled=true)`
  with an on-disk PNG and asserts the resulting `ImageDocument` reports
  `isUntitled()==true` (title "Untitled"), then clears on save to a chosen
  path.

These meet the "headless regression test covers the untitled-document
close-save path" threshold; that half is done. The non-blocking-progress
half stays open under the Threshold above.

## Context / Body

Leftover from the P2 regression-proofing batch (the makeDisabledAction helper,
the `-Werror=switch` enum-switch convention, and the Settings live-vs-restart
registry all landed in PR #48). The untitled-doc close-save regression guard
(above) is now closed; large saves still block rather than showing
non-blocking progress.

## Provenance

Affordances work session, harvested into the consolidated follow-up docket
2026-07-10 (P2 regression-proofing; the remaining open sub-item).
