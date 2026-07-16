---
id: 2026-07-15-single-page-force-rerun
title: Lightweight force-re-run of OCR for single-page documents (dialog-skip removed the control)
priority: P3
status: open
source: ADR 0013 (OCR pipeline for images) — follow-up to the single-page dialog-skip
created: 2026-07-15
---

## Threshold

A single-page document (every image, single-page PDFs) that OCRs to **non-empty
but wrong** text — watermark noise, a stray glyph, garbage — must offer a way to
re-run recognition from the menu. Declared pass/fail:

1. **Affordance exists.** For a single-page document, the user can trigger a
   **force-re-run** of Recognize Text without a multi-page picker — e.g. a
   modifier-click on Tools → Recognize Text…, or an explicit "Re-run" entry —
   that reaches `OcrController::submitUserPages(doc, {page}, /*forceRerun=*/true)`.
2. **Invalidate-then-OCR.** The force path invalidates the page's store entry
   (and its attempted-and-empty memo) before submitting, so a page that already
   `hasResults()` (or was memoed empty) is genuinely re-recognised rather than
   skipped by the ambient cache key.
3. **No extra clicks for the common case.** The plain, non-modifier path stays
   the current one-click resolve-to-current-page skip — the re-run is an opt-in
   affordance, not a new prompt in the default flow.

## Context

`MainWindow::onRecognizeText` (`src/ui/MainWindow.cpp`) skips the
`RecognizeTextDialog` entirely for a single-page document via
`resolveRecognizePages()` (`pageCount() == 1` → the current page), because the
dialog would only add a click when there is no page range to choose. But the
dialog was also the **only** place that exposed the `forceRerun` checkbox, so the
skip made `forceRerun` unreachable for single-page docs: `onRecognizeText` hard-
codes `forceRerun = false` on the skipped path.

Consequence: a single image whose OCR lands **non-empty garbage/watermark** text
gets a `hasResults()` entry, and the ambient cache-skip
(`OcrController::submitPage`, `store->hasResults(page)`) then treats the page as
done — the user cannot re-run recognition from the menu to replace it. (The
empty-result case is separately handled: an empty page memoes via
`SelectableTextStore::wasAttempted` and never claims a text layer, per ADR 0013
G13.1/G13.2. This item is about the **non-empty-but-wrong** case.)

Scope: add a lightweight force-re-run affordance for single-page docs
(modifier-click or a dedicated "Re-run" menu entry) that routes to
`submitUserPages(..., forceRerun=true)`, whose `invalidate(page)` already drops
both the results entry and the attempted memo. Keep the plain skip path
unchanged.

Grounded by `docs/decision-records/0013-image-ocr-pipeline-lazy-window-bounded-cache.md`.

File pointers:
- `src/ui/MainWindow.cpp` — `onRecognizeText` / `resolveRecognizePages` (the
  single-page dialog-skip that drops the force-rerun control).
- `src/ui/OcrController.cpp` — `submitUserPages` / `submitPage` (the
  `forceRerun` → `invalidate(page)` path the affordance must reach).
