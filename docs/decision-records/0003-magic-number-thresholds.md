# 0003 — Magic-number thresholds: scorer 0.50, "≥3 form widgets", "≥20 pages"

- **Status:** accepted
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-09
- **Date accepted / superseded:** 2026-07-12 (accepted)

## Context

Three thresholds are grouped here because they are all first-open / recommend
heuristics and all interact with live constants. PHILOSOPHY → *Hand-tuned
values stay hand-tuned* requires each such constant to carry an in-code comment
(what it represents, range tried, symptom to change); gate G6 in
[`../../AGENTS.md`](../../AGENTS.md) requires user-visible threshold changes to
reference a record like this one *and cite the constant's `file:line`* rather
than duplicating the comment. Two of the three numbers below are **not yet in
code** and each collides with a *different* live number — those confounds are
stated plainly so this record can't be read as describing current behaviour.

## The three thresholds

### (1) Scorer recommend threshold = 0.50 — LIVE, rationale already in code

- **Where:** `src/ml/BackgroundCandidateScorer.h:72` —
  `static constexpr float kRecommendThreshold = 0.50f;` (predicate
  `isRecommended()` at :76–78). A rationale block already exists at
  `src/ml/BackgroundCandidateScorer.h:65–71`.
- **Rationale (as written in code):** the badge misses real photos at 0.6+ and
  surfaces on busy scans below 0.4, so 0.50 is the balance point. Related pivots
  live at `src/ml/BackgroundCandidateScorer.cpp:38–40` (`kEdgePivot=12.0f`,
  `kSaturationPivot=1500.0f`, `kBimodalityPivot=0.15f`) and `:45`
  (`kMinShortEdge=32`).
- **Status of this one:** it already meets the in-code-rationale bar and the
  UX-Done evidence bar. This record simply *ratifies* 0.50 as the accepted
  value and points at the existing rationale; no confound.

### (2) "≥3 AcroForm widgets ⇒ treat as a form" — now LIVE, accepted

- **Where:** now committed as `src/ui/ContentAwareDefaults.h:51` —
  `inline constexpr int kFormFieldThreshold = 3;` (consumed by
  `contentAwareSidebarMode()` at `src/ui/ContentAwareDefaults.h:54+`), cited per
  gate G6. It originated in `TODO.md:138` as a tentative "a document with **≥ N
  AcroForm widgets (suggest ≥ 3)** is unambiguously a form" for the first-open
  heuristic (force the sidebar hidden for a clean fill view; the form-filling
  toolbar surfaces separately).
- **Confound (state plainly):** the *shipped* form behaviour auto-enables
  Fill-Forms at **≥1** fillable field (any `doc->supportsFormFilling()`) —
  `src/ui/MainWindow.cpp:2567` (`hasForms`) and :2581–2583 (one-shot
  auto-enable). So a "≥3" rule is a **different, additional** heuristic
  (form-*mode* / toolbar layout) than what ships (fill-*enablement* at ≥1). This
  record must not be read as changing the ≥1 fill-enable behaviour, and "3" is a
  proposed value, not current behaviour.
- **Rationale for the proposed value:** ≥1 field could be a single date box on
  an otherwise-normal PDF; ≥3 is meant as "unambiguously a form, worth
  rearranging the toolbar for." The value is a guess pending real documents.

### (3) "≥20 pages ⇒ auto-open the Thumbnails sidebar" — now LIVE, accepted

- **Where:** now committed as `src/ui/ContentAwareDefaults.h:50` —
  `inline constexpr int kLongDocumentPages = 20;` (consumed by
  `contentAwareSidebarMode()` at `src/ui/ContentAwareDefaults.h:54+`, which
  returns `Sidebar::Mode::Pages`), cited per gate G6. It originated in
  `TODO.md:143` as "a document with **≥ K pages (suggest ≥ 20)** …
  auto-popping `Sidebar::Mode::Pages`." (An unrelated "≥20 pages" at
  `docs/uat/02-viewer.md:329` is a *test precondition*, not a threshold.)
- **Confound (state plainly):** there **is** a live page-count threshold, but it
  is **50, for a different purpose** — the auto-OCR skip:
  `src/ui/OcrController.h:79` `static constexpr int kLargeDocPageThreshold = 50;`
  (spec'd ">50 pages" at :76–78; used at `MainWindow.cpp:2723`, referenced
  :358). The proposed "≥20 pages" sidebar trigger must **not** be conflated with
  this 50-page OCR threshold; they gate unrelated behaviours.
- **Rationale for the proposed value:** a long document is where a thumbnail
  strip earns its space; 20 is a guess for "long enough that page-hunting
  starts to hurt." Pending real documents.

## Personas debate

- **Office non-technical user:** Benefits from a form toolbar appearing on real
  forms and thumbnails on long documents — but only if the trigger doesn't
  misfire on ordinary PDFs and rearrange the UI unexpectedly.
- **Older careful user:** Distrusts the UI changing shape on its own. A
  too-eager trigger ("it moved my toolbar") is exactly the surprise this lens
  dislikes; argues for a conservative threshold and reversibility.
- **Power migrator:** Compares against Preview/Acrobat auto-behaviours; will
  notice if Trailer's thresholds feel off relative to those tools.
- **Occasional user:** Won't know why the layout differs between two documents;
  needs the change to be self-explanatory and undoable.

## Admissible objections

- **Older careful user, false-positive form mode:** if form *mode* triggered at
  ≥1 field (borrowing the live fill-enable number), a one-date-box PDF would
  rearrange the whole toolbar — a concrete "the app reshaped itself for no
  reason" failure. This is the reason the proposed form-mode threshold is higher
  than the live ≥1 fill-enable.
- **Any user, conflated page thresholds:** if the sidebar trigger were wired to
  the existing 50-page OCR constant, a 30-page document would get no thumbnail
  strip despite being long — a failure caused precisely by conflating 20 with
  50.

### Rejected as naked preference

- "Round numbers like 3 and 20 feel arbitrary." — rejected: names no user,
  step, or failure; the values are explicitly guesses pending real documents,
  which is a data question, not a preference.

## Checkable threshold this record establishes

- **(1)** Accept 0.50 as the ratified recommend threshold; the in-code rationale
  at `BackgroundCandidateScorer.h:65–71` stands as the record of record.
- **(2)** A named constant with an in-code rationale comment
  (`src/ui/ContentAwareDefaults.h:51`, `kFormFieldThreshold = 3`) gates the
  short-form sidebar-hidden behaviour at the committed field count, *distinct
  from* the ≥1 live fill-enable, pinned by the `exactBoundaries` unit test — a
  form with **2 vs 3 fillable widgets** behaves as specified (2 stays untouched,
  3 forces the sidebar hidden).
- **(3)** A named constant with an in-code rationale comment
  (`src/ui/ContentAwareDefaults.h:50`, `kLongDocumentPages = 20`) gates the
  page-thumbnail auto-open at the committed page count, *distinct from* the
  50-page OCR-skip constant, pinned by the `exactBoundaries` unit test — a
  document with **19 vs 20 pages** behaves as specified (19 does not auto-open
  the Pages sidebar, 20 does).
- Acceptance evidence for (2)/(3): the committed constants with rationale
  comments and the `exactBoundaries` unit test asserting the inclusive
  boundaries (19≠20, 2≠3). The magnitudes were promoted from TODO "suggest"
  numbers into committed behaviour under the owner's escalation-only veto and
  sign-off on the values (see verdict above).

## Arbiter verdict + rationale

Accepted. The two content-aware first-open thresholds — kLongDocumentPages = 20
and kFormFieldThreshold = 3 — are ratified as the committed values, gating an
orthogonal pair of behaviors (auto-open the page-thumbnail sidebar for long
documents; force the sidebar hidden for short forms) that are provably distinct
from the live ≥1 fill-enable and the 50-page OCR-skip constants. Both boundaries
are inclusive as specified and pinned by unit tests (exactBoundaries: 19≠20,
2≠3). The long-form tie is resolved deterministically in favour of navigation
because the page-count check is evaluated first. The values are deliberately
conservative so the heuristic fires only when a document is unambiguously long
or unambiguously a form; ≥3 avoids the single-field false-positive that ≥1 would
reintroduce, and 20 (vs 50) keeps a 30-page document from being denied its
thumbnail strip. These remain hand-tuned values subject to the reopen clause;
the owner retains escalation-only veto and sign-off on the magnitudes.

Threshold (1), scorer 0.50, was already live and is ratified here against its
existing in-code rationale.

## Evidence required to reopen

Once accepted: for (1), a case where 0.50 mis-badges against the stated
range-tried symptoms; for (2)/(3), real documents where the chosen count
mis-triggers or fails to trigger the layout change — plus owner sign-off.
