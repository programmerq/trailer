---
id: 2026-07-15-image-selectable-text-cue
title: Passive "text is selectable" cue for images (PDF recognize-notice chip has no image equivalent)
priority: P3
status: open
source: ADR 0011 (recognize-text affordance) / ADR 0002 §3 — follow-up from the OCR images PR
created: 2026-07-15
---

## Threshold

When OCR makes an image's text selectable/searchable, there must be a **passive,
non-modal cue** that this happened. Declared pass/fail:

1. **Cue exists.** After OCR populates `SelectableTextStore` for an image's page
   0 (auto on open for a small image, or on-demand), the viewer surfaces a
   transient/passive "text is selectable" cue — a short-lived status line or a
   quiet chip — without a modal and without a persistent control the user must
   dismiss.
2. **Honest and quiet.** The cue follows the ADR 0002 §3 no-lying spine: it
   appears only when the store actually holds usable blocks for the page
   (`hasResults(page)`), never for a text-less page (which is silently
   discarded per ADR 0013 G13.2), and it never claims success where there was
   none.
3. **Not a duplicate of Find.** The cue is independent of the existing Find
   action lighting up and the on-match highlight — those only appear once the
   user opens search; the cue tells a user who has NOT opened Find that the
   image's text became selectable.

## Context

Images gained a real selectable/searchable text layer via the OCR pipeline
(`SelectableTextLayer` + `SelectableTextStore`, image search over OCR blocks in
`ImageDocument`). But there is **no persistent visible cue** that an image's text
became selectable: the only surfaced signals are (a) the Find action becoming
enabled (`supportsSearch()`), and (b) on-match search highlights — both of which
require the user to already be searching.

PDFs have the recognize-notice chip (ADR 0011) for the parallel "this text can be
recognised / is now selectable" story; images have **no equivalent**. A user who
drops in a screenshot and expects Live-Text-style selectability has no affirmative
signal that recognition ran and succeeded — they have to guess and try to select.

Scope: add a transient/passive cue for images (a brief status-bar line or a quiet,
self-clearing chip) that fires when an image page gains usable OCR results,
mirroring the PDF recognize-notice's role without becoming a persistent control.
Honesty rules from ADR 0002 §3 apply: no cue for a silently-discarded empty page
(ADR 0013 G13.2).

Grounded by `docs/decision-records/0011-*.md` (the PDF recognize-notice chip),
`docs/decision-records/0002-*.md` §3 (missing-model / no-lying honesty), and
`docs/decision-records/0013-image-ocr-pipeline-lazy-window-bounded-cache.md`.

File pointers:
- `src/document/ImageAdapter.{h,cpp}` — the image OCR store, search, and
  `supportsSearch()`/highlight surface (the only current selectability signals).
- `src/ui/MainWindow.cpp` — the PDF `largeDocOcrHint` / recognize-notice
  status-bar plumbing an image cue would parallel.
