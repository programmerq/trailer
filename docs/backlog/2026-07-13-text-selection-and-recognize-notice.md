---
id: 2026-07-13-text-selection-and-recognize-notice
title: Text selection is dead on text-layer PDFs and the "Recognize text" notice misfires (two disconnected text pipelines)
priority: P1
status: open
source: v0.3.0 real-Mac dogfood report (2026-07-13)
created: 2026-07-13
---

## Threshold

On a born-digital text-layer PDF, text selection works from the native text
layer AND the large-doc OCR notice never appears; on a scanned no-text-layer PDF
with the OCR model missing, the notice appears, its link opens the
one-time-consent download flow (not a silent flash), and it clears once text
lands or the page changes away.

Declared pass/fail (G2 evidence via offscreen `QWidget::grab()` per AGENTS.md
G2; mirrors ADR-0002 gates G5/G6):

1. **Selection.** On a text-layer PDF, dragging over a paragraph yields a
   non-empty `selectedText()` and Copy produces the same string that find
   matches — no OCR run required.
2. **Notice suppressed on text-layer docs.** On the 142 MB text-layer PDF the
   `m_largeDocOcrHint` notice never shows (`grab()` of the status bar).
3. **Notice correct on scanned docs.** On a scanned no-text-layer PDF with the
   model missing, the notice shows, clicking its link enters the consent
   download flow (no `QDialog`/modal spawned, no silent no-op), and it clears
   once text lands or the page changes away.

## Context

Owner ran a 142 MB PDF that *has* a text layer: find/highlight works, yet drag
selection is dead, and a "Text isn't selectable here. Recognize text on this
page" notice fires, won't dismiss, and no-ops (an ML icon flashes, then
nothing). Both symptoms trace to **one** architectural gap and are tracked here
as a single investigation.

Root cause — two independent text pipelines that are never connected:
- **Find/search** uses Qt's native `QPdfSearchModel`, reading the PDF's
  built-in text layer — `src/document/PdfAdapter.cpp:773-811` (`setSearchQuery`,
  model created `:778`). This is why find works.
- **Selection** uses `SelectableTextLayer` painting from `SelectableTextStore`,
  which is populated **only** by the ML OCR path —
  `SelectableTextStore::put` is called from exactly one site, the OCR apply
  step `src/ui/OcrController.cpp:426`; the layer is wired to the store at
  `PdfAdapter.cpp:424-425`. The native text layer is never loaded into the
  store, and auto-OCR is deliberately skipped on text-layer docs
  (`src/ui/OcrController.cpp:74` `if (doc->hasTextLayer()) return;`). So the
  store stays empty, `SelectableTextLayer` has nothing to hit-test, and
  selection is dead.

The misfiring notice is the `m_largeDocOcrHint` widget (not the ADR-0002 hint):
- Built at `src/ui/MainWindow.cpp:411-431` — label "Text isn't selectable here."
  (`:415`), link "Recognize text on this page" (`:416-417`), added as a
  **permanent** status-bar widget (`:430`) with **no dismiss affordance**.
- Shown/hidden only in `onCurrentDocumentChanged` at the visibility block
  `MainWindow.cpp:3052-3061`:
  `show = isLargeDoc() && supportsSelectableText() && !store->hasResults(currentPage())`.
  `isLargeDoc()` = `pageCount() > 50` (`OcrController.cpp:48-52`). The condition
  has **no `!doc->hasTextLayer()` guard**, so on a >50-page valid PDF whose OCR
  store is empty (because auto-OCR is skipped) all three are true and it fires
  even though the doc has a perfectly good, find-usable text layer.
- Never clears: visibility is recomputed only on document/tab switch
  (`:3052`), not on page scroll and not after OCR populates a page; the widget
  has no close button.
- Click no-ops: the handler (`MainWindow.cpp:420-426`) calls
  `submitUserPages(...)` directly, **bypassing** the model-download consent
  flow. With the model absent the worker takes the not-ready branch
  (`OcrController.cpp:392-404`): returns empty, does not cache, only resolves the
  batch counter — the progress widget reveals then finishes (the "ML icon
  flash") and nothing is written. Contrast the correct menu path
  `MainWindow::onRecognizeText` (`:1916-1950`), which gates on
  `ensureOcrModelsReady()` (`:1945`) first.

### Contradicts ADR-0002 §3 "Missing model" on four points

`docs/decision-records/0002-ml-background-removal-progress-cancel.md` §3
(lines 126-129) and gates G5/G6 (`:144-145`) govern this affordance. The
`m_largeDocOcrHint` widget violates it:

| ADR-0002 §3 requirement | `m_largeDocOcrHint` actual | Location |
|---|---|---|
| Hint only when the doc "would auto-OCR", i.e. `!hasTextLayer()` | fires on text-layer docs (no guard) | `MainWindow.cpp:3054` |
| Click "enters the existing one-time-consent download flow", "never a silent no-op" | calls `submitUserPages` directly → silent no-op when model absent | `MainWindow.cpp:420-426` + `OcrController.cpp:392-404` |
| "state-driven and persistent … re-derived on document/page change" | recomputed only on document change, not page change | `MainWindow.cpp:3052` |
| Benefit-first, "no lying controls" wording | negative "Text isn't selectable here." | `MainWindow.cpp:415` |

Trailer already ships an ADR-0002-**compliant** hint — `m_ocrModelMissingHint`
(`MainWindow.cpp:517-546`): benefit-worded ("This document's text isn't
searchable — install language pack to recognise it."), state-driven via
`autoOcrModelMissing` (`OcrController.cpp:231-249`), re-derived on page change
(`:548-559`), routed through the sanctioned consent flow (`:525-543`). That is
the reference to fold `m_largeDocOcrHint` into.

Fix direction: (1) populate `SelectableTextStore` from the native PDF text
layer for born-digital docs (or back `SelectableTextLayer` with native text-layer
geometry when `hasTextLayer()`), wired at `PdfAdapter.cpp:419-451`. (2) Retire /
gate `m_largeDocOcrHint` — add `&& !doc->hasTextLayer()` at
`MainWindow.cpp:3054`, better fold it into the compliant `m_ocrModelMissingHint`
machinery, route its link through `ensureOcrModelsReady()`, re-derive on page
change, reword benefit-first. The text-layer-selection-vs-OCR research theme in
`docs/research/2026-07-13-ux-research-agenda.md` feeds the affordance-convention
decision.

Cross-links: `docs/decision-records/0002-ml-background-removal-progress-cancel.md`
(§3, G5/G6); `2026-07-12-bg-removal-progress-cancel-widget` and
`2026-07-12-page-changed-signal-no-poll` (adjacent ML-feedback / page-signal
items).

## Provenance

v0.3.0 real-Mac dogfood report, 2026-07-13. Root-cause file:line refs from the
grounded investigation pass against `a4abbcf`.
