# 0003 — Magic-number thresholds: scorer 0.50, "≥3 form widgets", "≥20 pages"

- **Status:** proposed
- **Arbiter:** the maintainer (default), or a delegate the maintainer names for this record.
- **Date proposed:** 2026-07-09
- **Date accepted / superseded:** —

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

### (2) "≥3 AcroForm widgets ⇒ treat as a form" — NOT in code; proposed only

- **Where:** appears only in `TODO.md:138` as a tentative "a document with **≥ N
  AcroForm widgets (suggest ≥ 3)** is unambiguously a form" — for an *unbuilt*
  first-open heuristic (show the form toolbar, suppress the markup toolbar,
  consider hiding the sidebar). **There is no such constant in code.**
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

### (3) "≥20 pages ⇒ auto-open the Thumbnails sidebar" — NOT in code; proposed only

- **Where:** appears only in `TODO.md:143` as "a document with **≥ K pages
  (suggest ≥ 20)** … auto-popping `Sidebar::Mode::Thumbnails`." **No constant in
  code.** (An unrelated "≥20 pages" at `docs/uat/02-viewer.md:329` is a *test
  precondition*, not a threshold.)
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

## Checkable threshold this record would establish

- **(1)** Accept 0.50 as the ratified recommend threshold; the in-code rationale
  at `BackgroundCandidateScorer.h:65–71` stands as the record of record.
- **(2)** If accepted, a named constant (with an in-code rationale comment)
  gates form-*mode* at the chosen field count, *distinct from* the ≥1 live
  fill-enable, verified by a boundary UAT case written against the proposed
  count — e.g. at the proposed ≥3, a form with **2 vs 3 fillable widgets**
  behaves as specified (2 stays in normal mode, 3 enters form mode). The
  boundary number is still `proposed` (see the confound above); this states
  the shape of the test the accepted N would make concrete.
- **(3)** If accepted, a named constant (with rationale) gates the Thumbnails
  auto-open at the chosen page count, *distinct from* the 50-page OCR-skip
  constant, verified by a boundary UAT case written against the proposed count
  — e.g. at the proposed ≥20, a document with **19 vs 20 pages** behaves as
  specified (19 does not auto-open the Thumbnails sidebar, 20 does). The
  boundary number is still `proposed`.
- Acceptance evidence for (2)/(3): the new constants with rationale comments,
  UAT boundary cases, and screenshots of the triggered/not-triggered states
  (gate G2). **These two values need explicit owner sign-off** because they
  promote TODO "suggest" numbers into committed behaviour.

## Arbiter verdict + rationale

<Open — status is proposed. (1) is ready to accept as-is; (2) and (3) need
owner sign-off on the specific numbers and are the reason this record stays
proposed as a set.>

## Evidence required to reopen

Once accepted: for (1), a case where 0.50 mis-badges against the stated
range-tried symptoms; for (2)/(3), real documents where the chosen count
mis-triggers or fails to trigger the layout change — plus owner sign-off.
