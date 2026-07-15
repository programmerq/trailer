# 0006 — Refine ADR-0002 text-selection & Recognize-text notice affordance (dogfood evidence)

- **Status:** accepted
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-15
- **Date accepted / superseded:** 2026-07-15 (accepted)
- **Refines:** ADR-0002 §3 (Missing model) and gates G5/G6 — this record amends, it does not replace: ADR-0002's progress/cancel/no-substitution spec stands unchanged.

## Context

ADR-0002 settled how the missing-model / auto-OCR affordance behaves: a
state-driven, non-modal, benefit-worded in-context hint whose link enters the
one-time-consent download flow, never a silent no-op or a modal-that-says-no
(ADR-0002 §3, G5/G6). That spec is correct. The **implementation of a second,
older hint** — `m_largeDocOcrHint` in `src/ui/MainWindow.cpp` — predates and
violates it, and a v0.3.0 real-Mac dogfood pass surfaced two user-visible
failures that trace to one architectural gap. Both are recorded in the backlog
item [`docs/backlog/2026-07-13-text-selection-and-recognize-notice.md`](../backlog/2026-07-13-text-selection-and-recognize-notice.md).

Ships-today behaviour the dogfood run hit on a 142 MB **born-digital** PDF (one
that *has* a native text layer — find/highlight already work):

1. **Selection is dead.** `SelectableTextLayer` paints from
   `SelectableTextStore`, which was written at exactly one site — the OCR apply
   step (`OcrController::submitPage` → `store->put`). The PDF's native text
   layer was never ingested, and auto-OCR is deliberately skipped on text-layer
   docs, so the store stayed empty and drag-select copied nothing — even though
   `QPdfSearchModel` finds the same text.
2. **The "Recognize text" notice misfires.** `m_largeDocOcrHint`'s show-
   condition (`isLargeDoc() && supportsSelectableText() && !store->hasResults`)
   had **no real per-page text guard**, so it fired on born-digital docs; it was
   a permanent status-bar widget with **no dismiss**; it recomputed **only on
   document change** (never on page scroll or after OCR) so it never cleared;
   and its link called `submitUserPages()` directly, **bypassing the
   consent/download gate** — a silent no-op ("ML icon flash, then nothing") when
   the model is absent, exactly the ADR-0002 §3 violation.

This record ratifies the four corrections as the accepted behaviour so the
G6 (decision-record) gate is satisfied for the user-visible wording/behaviour
change.

## Options

- **Selection source:** (A) ingest the PDF's native text layer into
  `SelectableTextStore` for born-digital pages so the existing selection layer
  lights up; vs (B) give `SelectableTextLayer` a second, native-only geometry
  backend. (A) reuses the one store both pipelines already target and keeps the
  layer unchanged.
- **Notice guard:** (A) add a real per-page text check (`pageHasText(page)`)
  plus the existing OCR-results check; vs (B) drop the notice entirely on any
  valid PDF. (A) preserves the notice for genuinely text-less scanned pages,
  which is its legitimate job.
- **Dismiss / clearing:** (A) dismissable (×) + re-derived on the existing
  150 ms page poll so it self-clears; vs (B) leave it document-change-only.
- **Action routing:** (A) route the link through `ensureOcrModelsReady()` like
  `onRecognizeText` and the compliant `m_ocrModelMissingHint`; vs (B) keep the
  direct `submitUserPages()` call.

## Personas debate

- **Office non-technical user:** Opened a normal PDF; text won't select and a
  notice keeps telling them the page isn't selectable when find plainly works.
  Reads as broken. Wants selection to just work and the scolding notice gone.
- **Older careful user:** Distrusts a control that fires a download-shaped
  action. Needs the link to ask first (consent flow), and needs a way to
  dismiss a notice that won't go away.
- **Power migrator:** Expects native PDF text selection + Ctrl+C to match every
  other viewer, with no OCR round-trip on a doc that already has text.
- **Occasional user:** Won't reason about "text layer vs OCR"; a permanent,
  undismissable, no-op notice is pure noise.

## Admissible objections

- **Office user, born-digital doc, dead selection:** drag-select yields nothing
  on a doc whose text find can locate — a capability the user reasonably
  expects and peer tools provide. Concrete failure at "select a paragraph,
  Ctrl+C".
- **Office/occasional user, misfiring notice:** the notice claims the page
  isn't selectable on a text-layer doc, cannot be dismissed, and never clears —
  a persistent false statement (a lying control) at "read the status bar".
- **Older careful user, silent no-op link:** clicking the notice with the model
  absent flashes progress and writes nothing, bypassing the consent flow —
  violates ADR-0002 §3 and the model-download consent rule at "click the offered
  action once to see what it does".

### Rejected as naked preference

- "Just remove the notice on all PDFs." — rejected: names no user problem for
  the scanned-doc case the notice legitimately serves; a genuinely text-less
  large scan still needs the offer.

## Checkable threshold this record would establish

On a born-digital text-layer PDF: dragging over a paragraph yields a non-empty
`selectedText()` / clipboard string matching the native text find matches, with
no OCR run; and `m_largeDocOcrHint` never shows. On a text-less (scanned) large
(>50-page) PDF with the model absent: the notice shows, its link enters the
consent/download flow (no `QDialog`/modal spawned, no silent no-op), it can be
dismissed (× ; sticky per-document), and it self-clears once the page gains text
/ OCR results / the document changes. Verified by offscreen `QWidget::grab()`
evidence per AGENTS.md G2 (mirrors ADR-0002 G5/G6).

## Arbiter verdict + rationale

**Accepted 2026-07-15.** The four corrections are ratified:

1. **Native text feeds selection.** `IDocument::pageHasText(int)` (default
   false; `PdfDocument` implements it as
   `!m_doc->getAllText(page).text().trimmed().isEmpty()`) plus
   `PdfDocument::ingestNativeTextLayer(int)`, which builds line-level
   `OcrEngine::TextBlock`s (native text + point-space geometry via
   `getSelectionAtIndex`) into `SelectableTextStore`, wired lazily per page as
   pages become current (`PdfAdapter.cpp` `createView`). Guarded on
   `!store->hasResults(page)` so it never clobbers real OCR output. The coarse
   `hasTextLayer()` stub is left untouched (it gates the auto-OCR path).
2. **Notice guarded by a real per-page check.** The show-condition gains
   `&& !doc->pageHasText(page) && store && !store->hasResults(page)`, keeping the
   existing `isLargeDoc()` (>50-page) scope by design.
3. **Dismissable + self-clearing.** A × button sets a per-document dismissed
   flag (reset on document change); visibility is re-derived by the existing
   150 ms `m_ocrPagePoll` tick via `MainWindow::updateLargeDocOcrHint()` (called
   from both `onCurrentDocumentChanged` and the poll) so it hides the moment the
   page gains text / OCR results.
4. **Action routes through consent + standard progress.** The link handler now
   gates on `ensureOcrModelsReady(this, engine)` (mirroring `onRecognizeText` and
   `m_ocrModelMissingHint`) before `submitUserPages`, and is re-worded benefit-
   first ("This page's text isn't selectable yet."). `MlProgressWidget` shows
   progress automatically off the batch signals.

Rationale: admissible objections 1–3 above drove every point; the naked
"remove it everywhere" preference was rejected because it discards the notice's
legitimate scanned-doc job. This aligns `m_largeDocOcrHint` with the ADR-0002
§3 pattern the compliant `m_ocrModelMissingHint` already embodies.

## Evidence required to reopen

A measured case where native-text ingestion produces wrong/misaligned selection
on a real born-digital PDF, or where the per-page `pageHasText` guard suppresses
the notice on a page that genuinely needs OCR (e.g. a text layer that is a
watermark only), plus owner sign-off. The RecognizeText dialog's force-re-run
path already covers the watermark case without reopening this record.
