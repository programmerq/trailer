# 0011 — Reconciling the misfiring "Recognize text" affordance with ADR-0002 §3

- **Status:** proposed
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-15
- **Date accepted / superseded:** —

## Context

Trailer offers to "recognize text" on a page through the `m_largeDocOcrHint`
status-bar chip. The user-facing behaviour in question: **when, and how
non-invasively, a PDF viewer should offer OCR, and how that offer must
distinguish a born-digital document (which already carries a text layer, so
selection should just work) from a scanned one (which has only an image of
text and genuinely needs OCR).** This is Theme 6 of the v0.3.0 UX research
agenda (`docs/research/2026-07-13-ux-research-agenda.md:181-208`).

**What ships today (so this record is not misread as describing the target):**
the `m_largeDocOcrHint` chip **misfires**. It is built at
`src/ui/MainWindow.cpp:411-431` (label "Text isn't selectable here."
`:415`, link "Recognize text on this page" `:416-417`, added as a **permanent**
status-bar widget `:430` with no dismiss control), and its visibility is
computed only in `onCurrentDocumentChanged` at `src/ui/MainWindow.cpp:3052-3060`
as `isLargeDoc() && supportsSelectableText() && !store->hasResults(currentPage())`.
`isLargeDoc()` is `pageCount() > 50` (`src/ui/OcrController.cpp:48-52`). Because
auto-OCR is deliberately skipped on text-layer docs
(`src/ui/OcrController.cpp:74`, `if (doc->hasTextLayer()) return;`), the OCR
store stays empty on a born-digital PDF, so on any >50-page document **that
already has a perfectly good, find-usable text layer** all three conditions
are true and the chip fires anyway. Clicking its link calls
`m_ocrController->submitUserPages(...)` directly (`src/ui/MainWindow.cpp:420-426`);
with the model absent the worker takes the not-ready branch
(`src/ui/OcrController.cpp:392-404`) and returns empty — the "ML icon flash,
then nothing" silent no-op the dogfood report recorded.

This directly contradicts the **accepted** spec in ADR-0002 §3 "Missing model"
(`docs/decision-records/0002-ml-background-removal-progress-cancel.md:127-129`,
gates G5/G6 at `:144-145`) on four points:

| ADR-0002 §3 requirement | `m_largeDocOcrHint` today | Location |
|---|---|---|
| Hint only when the doc "would auto-OCR", i.e. `!hasTextLayer()` | fires on text-layer docs (no guard) | `MainWindow.cpp:3054` |
| Click "enters the existing one-time-consent download flow", "never a silent no-op" | calls `submitUserPages` directly → silent no-op when model absent | `MainWindow.cpp:420-426` + `OcrController.cpp:392-404` |
| "state-driven and persistent … re-derived on document/page change" | recomputed only on document change, not page change | `MainWindow.cpp:3052` |
| Benefit-first, "no lying controls" wording | negative "Text isn't selectable here." | `MainWindow.cpp:415` |

Trailer already ships an ADR-0002-**compliant** sibling affordance,
`m_ocrModelMissingHint` (`src/ui/MainWindow.cpp:517-546`): benefit-worded
("This document's text isn't searchable — install language pack to recognise
it."), state-driven via `autoOcrModelMissing` (`OcrController.cpp:231-249`),
re-derived on page change through `m_ocrPagePoll` (`MainWindow.cpp:548-564`),
and routed through the sanctioned one-time-consent download flow
(`ensureOcrModelsReady`, `MainWindow.cpp:525-543`). That is the compliant
machinery this record folds `m_largeDocOcrHint` into.

This record does **not** open a fresh design debate: ADR-0002 §3 already
settled the missing-model affordance rules and the owner ratified the
disabled+tooltip / in-context-consent pattern. What is open is the narrower
reconciliation — making the recognize-text chip obey those already-accepted
rules and adding the born-digital-vs-scanned distinction the research theme
raises. A **wave-2 sibling session is already implementing the notice guard**
described in the feeding backlog item
(`docs/backlog/2026-07-13-text-selection-and-recognize-notice.md`), whose
Threshold (lines 10-30) and four-point ADR-0002 contradiction table (lines
76-94) this record is written to **ratify, not fork**. Because the fix rides in
the code and only lightly amends ADR-0002 §3 (adding the explicit
`!hasTextLayer()` gate that §3's "would auto-OCR" language already implies),
this record stays **proposed** and rides *behind* that code: the implementing
session accepts it once the guard lands green, at which point it reads as an
amendment that extends ADR-0002 §3 to the recognize-text chip rather than
superseding it.

### External grounding

Reference-app convention confirms the born-digital-silent / scanned-in-context
norm, and confirms that no established viewer fires a nagging "recognize text"
prompt on a document that already has a text layer:

- **Adobe Acrobat "Recognize Text" (Scan & OCR):** Acrobat *refuses* to OCR a
  page that already contains a text layer, returning "Acrobat could not perform
  recognition (OCR) on this page because: This page contains renderable text."
  OCR "only functions on a PDF page that has only an image of text"; renderable
  (editable) text has no image-of-text to process
  (https://helpx.adobe.com/acrobat/kb/error-could-perform-recognition-acrobat.html).
  This is the exact `!hasTextLayer()` gate, enforced at the engine level: the
  affordance is user-invoked, never an auto-firing prompt, and it is inert on
  born-digital docs by construction.
- **Apple HIG — Offering help:** the norm is that contextual help is provided
  "when necessary" over approachable-by-default interfaces, and that non-modal
  overlays (transient notifications) do not block the app, in contrast to
  high-friction modal overlays (sheets, alerts) that block what's behind them
  until the user decides
  (https://developer.apple.com/design/human-interface-guidelines/offering-help).
  A passive, benefit-worded, non-modal document-status chip is the sanctioned
  shape; a persistent negative chip that no-ops is not.
- **Preview.app (Live Text):** on a born-digital PDF the native text layer is
  selected directly, with no recognize prompt. Whether Preview extends
  in-context Live Text drag-select-and-copy to a *scanned* PDF page — with no
  separate "recognize" prompt or modal — is **(needs-live-verification** — not
  confirmable from the reputable docs consulted).
- **PDF Expert:** offers OCR on scanned documents as an in-context action rather
  than an auto-firing modal on every large doc, and does not prompt to OCR a
  born-digital PDF **(needs-live-verification** — the exact surfacing, modal vs
  in-context, is not confirmable from the reputable docs consulted).

The convergent norm: born-digital → selection is silent, no recognize prompt;
scanned → OCR is offered in context, non-modally, and inertly gated so it never
fires on a doc that already has a text layer. Trailer's `m_largeDocOcrHint`
violates every part of this; `m_ocrModelMissingHint` already honours it.

## Options

- **A. Reconcile the chip (fold into the compliant machinery).** Gate the
  recognize-text affordance on `!doc->hasTextLayer()` so it never appears on a
  born-digital doc; route its link through the sanctioned one-time-consent
  download flow (`ensureOcrModelsReady`) instead of calling `submitUserPages`
  directly; make it page-state-driven (re-derived on page change, not only on
  document change) so it clears when text lands or the reader scrolls to a text
  page; and reword it benefit-first, dropping "Text isn't selectable here." for
  benefit-language with no "OCR"/"model" jargon. In practice this means folding
  `m_largeDocOcrHint` into the existing `m_ocrModelMissingHint` machinery
  (`MainWindow.cpp:517-564`) rather than maintaining a second, divergent chip.
- **B. Remove the chip entirely.** Delete `m_largeDocOcrHint`. Born-digital
  selection is fixed by populating `SelectableTextStore` from the native text
  layer (the backlog item's fix (1)), and the missing-model case is already
  covered by the compliant `m_ocrModelMissingHint`, so the second chip is
  redundant. The recognize-text intent is reachable via the explicit menu
  action `onRecognizeText` (`MainWindow.cpp:1916-1950`).
- **C. Leave as-is (rejected).** Keep the chip firing on text-layer docs with
  its direct `submitUserPages` no-op. Retained only to be explicitly rejected.

## Personas debate

- **Office non-technical user:** Opens a large born-digital report, finds and
  highlights fine, then sees a permanent "Text isn't selectable here." chip that
  won't go away and does nothing when clicked. Reads the chip as the app being
  broken or lying about their document. Favours A or B — anything that stops the
  false negative claim on a doc whose text plainly works.
- **Older careful user:** Distrusts a control that offers to do something and
  then silently does nothing; the "ML icon flash, then nothing" is exactly the
  "did it work? did it start a download?" anxiety this lens fears. Wants the
  offer to appear only when it is real (scanned doc, model needed) and to route
  through an explicit, plain-language consent step — Option A's consent routing,
  or B's removal. Option C is the direct trigger of this lens's distrust.
- **Power migrator (ex-Preview/Acrobat):** Expects Acrobat's behaviour — OCR is
  simply unavailable / inert on a doc that already has renderable text — and
  Preview's — selection just works, no prompt. A chip that offers to "recognize
  text" on a text-layer PDF is non-native and reads as a bug. Favours A (gate on
  `!hasTextLayer()` mirrors Acrobat's renderable-text refusal) or B.
- **Occasional user:** Won't know whether a document is born-digital or scanned
  and won't reason about text layers. Needs the app to only offer OCR when it is
  genuinely needed, and to say what the benefit is, not surface jargon. Served
  by A's benefit-wording and state-driven gating; harmed by C's misfire, which
  teaches them to distrust the chip and ignore it on the scanned docs where it
  matters.

## Admissible objections

- **Office / power-migrator user, Option C:** on a >50-page born-digital PDF the
  chip fires (`MainWindow.cpp:3054`, no `!hasTextLayer()` guard) claiming "Text
  isn't selectable here." while find/highlight demonstrably work — a
  no-lying-controls failure at the "I can already select-adjacent this text"
  step. This is the decisive argument against C and the reason a gate on
  `!hasTextLayer()` is required.
- **Older careful user, Option C:** clicking the chip with the model absent runs
  `submitUserPages` directly (`MainWindow.cpp:420-426`), hits the not-ready
  branch (`OcrController.cpp:392-404`), and no-ops silently — violating
  ADR-0002 §3's "enters the existing one-time-consent download flow … never a
  silent no-op" at the "I pressed it once to see what it does" step. Answered
  only by routing through consent (Option A) or removing the trigger (Option B).
- **Any user, Option C (stale chip):** because visibility is recomputed only on
  document change (`MainWindow.cpp:3052`), the chip does not clear when text
  lands or the reader scrolls to a text page — violating ADR-0002 §3's
  "state-driven … re-derived on document/page change." Answered by A's
  page-state-driven re-derivation (mirroring `m_ocrPagePoll`,
  `MainWindow.cpp:548-564`) or by B's removal.
- **Option B vs A, scanned-doc discoverability:** if the chip is removed
  outright (B) the only remaining recognize-text surfacing on a scanned,
  model-missing doc is `m_ocrModelMissingHint`; B is admissible **only** if that
  compliant hint already covers the scanned-doc case the chip was meant to
  serve. Where it does, B and A converge; where a distinct large-doc surfacing
  is still wanted, A folds the chip into the compliant machinery rather than
  dropping the surface. This is the seam the arbiter must resolve against the
  landed guard.

### Rejected as naked preference

- "The recognize-text chip looks helpful, keep it visible." — rejected: names no
  user, step, or failure; a chip that fires on text-layer docs and no-ops is the
  admissible failure above, not a benefit.
- "OCR jargon is fine, users can look it up." — rejected: states a taste, not a
  concrete failure; ADR-0002 §3's benefit-language rule already governs, and the
  admissible version is the occasional user's "don't surface jargon" served by A.

## Checkable threshold this record would establish

This record commits the recognize-text affordance to ADR-0002 §3's four points,
each phrased as an independently checkable UAT assertion, mirroring ADR-0002
gates G5/G6 and the feeding backlog item's declared pass/fail
(`docs/backlog/2026-07-13-text-selection-and-recognize-notice.md:19-30`). Proven
per AGENTS.md G1 (threshold-first) and G2 (offscreen `QWidget::grab()` under
`QT_QPA_PLATFORM=offscreen`):

- **G1.1 — Gate on `!hasTextLayer()`.** On a born-digital text-layer PDF
  (including the 142 MB / >50-page case), the recognize-text affordance is
  **never visible**: `grab()` of the status bar shows no recognize-text chip
  while `doc->hasTextLayer()` is true. Assertion: `chip.isVisible() == false`
  for every page of a text-layer doc. (Mirrors backlog Threshold #2 and
  ADR-0002 G5's "hidden … once model present"; here hidden whenever the doc has
  a text layer at all.)
- **G1.2 — Route through consent.** On a scanned, no-text-layer PDF with the
  model absent, activating the affordance's link enters the existing
  one-time-consent download flow (`ensureOcrModelsReady` /
  `requestModelDownload`) — **no `QDialog`/modal spawned by the chip itself, no
  silent no-op, no direct `submitUserPages` call**. Assertion: the download-
  consent entry point (or its test hook) is invoked exactly once on activation,
  and no OCR batch is submitted before consent resolves. (Mirrors ADR-0002 G5
  and backlog Threshold #3.)
- **G1.3 — Page-state-driven.** The affordance is re-derived on **page change**,
  not only on document change: it appears when the visible page is a scanned
  page that would auto-OCR with the model missing, and **clears once text lands
  for that page or the reader scrolls to a text-layer page**. Assertion: after
  OCR populates the visible page (or after a page change to a text page),
  `grab()` shows the chip gone without a document switch. (Mirrors ADR-0002 §3
  "re-derived on document/page change" and the compliant `m_ocrPagePoll`.)
- **G1.4 — Benefit-worded.** The affordance's text is benefit-first and carries
  no "OCR"/"model" jargon token — it describes what the user gains
  (searchable/selectable text) and the one-time path, not a negative "Text isn't
  selectable here." Assertion: the visible string contains no `OCR`/`model`
  jargon token and is phrased as document status about a benefit. (Mirrors
  ADR-0002 G6's "benefit-worded … no 'model' jargon token".)

Passing G1.1–G1.4 is what makes the recognize-text affordance ADR-0002 §3
compliant; the simplest implementation that satisfies all four is folding
`m_largeDocOcrHint` into the `m_ocrModelMissingHint` machinery (Option A), or
removing it where `m_ocrModelMissingHint` already covers the scanned case
(Option B). Option C fails G1.1–G1.4 by construction.

## Arbiter verdict + rationale

Empty while status is `proposed` — the implementing session runs the
persona/arbiter cycle.

## Evidence required to reopen

N/A until accepted.
