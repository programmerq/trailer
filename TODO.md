# Trailer — TODO (the always-clean queue)

This file has exactly one job: at any moment, name the **single next
action** with no archaeology. Return after five minutes or five weeks
and the cost of re-entry is the same — read the *Next action* line and
go. If you ever can't, repairing that is itself the next action.

**How to read it.** Priority is not stamped by hand. It is *read off*
each finding as **which job it degrades × how badly × how often that
job is done** — the ranking function in
[CRITERIA.md](CRITERIA.md) §4. Job codes **J1–J8**, the degree ladder
(**Blocks** ▸ **Workaround** ▸ **Mars**), and the frequency tiers are
all defined there; every queue item below carries its `job · degree ·
frequency` so a cold reader can act without re-deriving anything.
**Frequency tiers are PROVISIONAL** (inferred, not owner-confirmed —
see Owner questions); re-tiering would reorder the queue.

Three recurring sources feed findings into this file:

- **HITL passes** — the maintainer driving the actual app and writing
  down what annoyed them. Captures power-user friction. Dated
  subsections in the archive (e.g. *2026-05-19 HITL pass*) are the
  precedent format.
- **Reference-user smoke sessions** — a non-maintainer opening a fresh
  build and performing three small tasks while a note-taker records
  observations. Protocol lives in
  [`docs/smoke-session.md`](docs/smoke-session.md); findings land under
  a dated `## YYYY-MM-DD smoke session` subsection in the same shape as
  the HITL entries. (The old "designer / non-technical-user review"
  bullet folded into this — see *Archive ▸ Process notes*.)
- **Multi-perspective audits** — read-only sweeps by reviewer-chair
  agents (privacy, accessibility, security, etc.) that surface
  structural gaps live use doesn't expose. See
  [`docs/audit-2026-05-19.md`](docs/audit-2026-05-19.md).

A future channel — the recorder → agent-mining pipeline (CRITERIA.md
§6) — will emit findings in the same `{ timestamp, repro, job, degree,
platform }` shape.

**Verification caveat for every "verify in the app" note below.** Green
CI asserts *unit-test-level* Windows + Linux parity, not job-bar parity:
the native-MSVC Windows job is disabled (`if: false`, 2026-07-09, to
conserve Actions minutes) though the mingw-w64 + Wine cross-build still
runs the unit suite on every PR, and the UAT tier that would exercise
J1–J8 end-to-end runs release-candidate-only, on neither OS per PR. So
"passes CI" never substitutes for the live HITL/dogfood check an item
calls for — and per CRITERIA.md §1 a job is *met* only when it passes on
**both** Windows and Linux.

---

## Next action

> **Add endpoint drag handles to Line/Arrow annotations.**
> *(J3 · Blocks · frequent — queue #1.)*
> In [`src/ui/AnnotationOverlay.{h,cpp}`](src/ui/AnnotationOverlay.h)
> extend the `ResizeHandle` enum beyond its four corners
> ([`AnnotationOverlay.h:295`](src/ui/AnnotationOverlay.h:295) is still
> `{ None, TopLeft, TopRight, BottomLeft, BottomRight }`) with Start/End
> endpoint handles for Line/Arrow, and wire them through
> `handleAt`/`handleRect`, the selection paint loop (~`AnnotationOverlay.cpp:662`),
> and the mousePress/Move drag path so dragging an endpoint moves that
> end of the line. Today the resize path only ever writes
> `updated.bounds`, never `updated.points`, so a Line/Arrow visibly does
> not move on resize.

This is the single head of the queue under the current tie-break — **any
Blocks outranks any lesser degree** (only a Blocks forces the Preview
fallback 1.0 forbids), then higher frequency, then the oldest finding.
That rule is PROVISIONAL and the owner may invert it (Owner questions
§Ranking tie-break); until then the head is deterministic. Read it and
go; do not re-derive.

---

## Ranked queue (open findings, priority order)

Confirmed still-open against `main` @ `5771b15` (≈50 commits past the
June-1 triage baseline; most citations held line-for-line because no
intervening commit touched these paths).

### 1. Line/Arrow annotations lack endpoint drag handles — J3 · Blocks · frequent
**Next action:** extend `ResizeHandle` with Start/End endpoints for
Line/Arrow and wire them through hit-test, paint, and the drag path (see
*Next action* above).
**Sources:** archive *2026-05-19 HITL ▸ Annotation handles (Workstream
D)*; CRITERIA.md §5 J3 Today ("line/arrow endpoint drag is inert");
[`AnnotationOverlay.h:295`](src/ui/AnnotationOverlay.h:295) (enum),
`AnnotationOverlay.cpp` ~1038-1073 (resize writes `bounds` only).

### 2. First-run auto-OCR is a silent no-op when the model isn't on disk — J5 · Blocks · frequent
**Next action:** replace the "no model on disk → no-op" auto-submission
branch (`OcrController.cpp` ~229-237) so the background path routes
`ensureOcrModelsReady`/download-progress + consent through the ML
scheduler, or at minimum shows a one-time "text recognition needs a
download" affordance instead of doing nothing silently.
**Sources:** archive *2026-05-19 HITL ▸ OCR (Workstream F)*; CRITERIA.md
§5 J5 Today ("a Blocks for the automatic promise on a fresh machine");
[`src/ui/OcrController.cpp:207`](src/ui/OcrController.cpp:207)
(`if (!engine->isModelReady()) return;`), no-op comment at ~229-237.

### 3. Copy Page as Image copies the un-annotated raster — every mark is dropped — J3 · Blocks · frequent
**Next action:** `onCopyPageAsImage`
([`MainWindow.cpp:2222`](src/ui/MainWindow.cpp:2222)) uses
`doc->renderThumbnail()` — page raster only. Composite the
`AnnotationOverlay` (and form/OCR layers) onto that `QImage` before
`clipboard()->setImage`.
**Sources:** archive *2026-05-20 HITL ▸ Select All only selects
annotations*; CRITERIA.md §5 J3 Today; `MainWindow.cpp:2215-2229`.
**Notes:** PR #34 (MERGED `5771b15`) landed the base render-to-clipboard
and left this gap — file as a fresh finding `{J3, Blocks}`; it is the
terminal step of the J3 bar. Draft PR #45 only adds a *disabled-state*
tooltip (gate G3) and does **not** composite annotations. The Cmd-A /
"select page for copy" scope this touches is governed by decision record
[`docs/decision-records/0001-select-all-semantics.md`](docs/decision-records/0001-select-all-semantics.md)
(**status: proposed**) — settle that before overloading ⌘A.
Trivial nit carried from PR #34: magic `2200` → named `constexpr`.

### 4. Annotations written against stale page indices after page delete/move — J7 · Blocks · occasional
**Next action:** on save-after-delete/move, re-index annotation page
bindings so markup writes to the page it was drawn on; add a
reopen-based UAT (the in-app tester won't catch it — corruption only
shows after close+reopen).
**Sources:** archive *Cross-cutting ▸ PDF undo/redo (other small
follow-ups)*; CRITERIA.md §5 J7 Today (the exact bug J7 is unmet on);
`docs/uat/03-pdf-pages.md` UAT-PDF-070/071 (spec only, no test file).
**Notes:** worst data-integrity bug in scope, but ranks below the three
frequent Blocks per the frequency-after-degree tie-break. **Not** fixed
by the 2026-07-10 undo-stabilization batch (`60406dc`), which fixed a
different bug (undo-log/AnnotationStore desync on cap eviction);
`PdfDocument::deletePages`/`movePage` still never touch `Annotation::page`.

### 5. No "changed on disk" detection — external edits cause silent reload/overwrite fights + undo desync — J1/J3/J4 · Workaround · frequent
**Next action:** add a `QFileSystemWatcher` on the open file; on external
modification prompt (reload / keep mine) instead of silently
auto-reloading or overwriting.
**Sources:** Issue #7d (no `QFileSystemWatcher`/conflict-detection code
anywhere in `src/`).
**Notes:** DATA-SAFETY — candidate to reclassify **Blocks** (Owner
questions §Issue #7d degree); silent corruption is what PHILOSOPHY's
no-lying-UI gates exist to prevent. Distinct from decision record
[`docs/decision-records/0004-never-worry-save-invariant.md`](docs/decision-records/0004-never-worry-save-invariant.md)
(**proposed**), which covers *close-with-unsaved-edits* silent discard —
a related but different data-loss path, not this external-change conflict.

### 6. Retina/HiDPI images open at 2× displayed size in the main viewer — J3 · Workaround · frequent
**Next action:** FIRST verify live on a real 2× screenshot — Issue #7a is
marked closed/completed but no main-view DPR fix exists in code. If
confirmed, set `devicePixelRatio` on the `QImage` in
`src/document/ImageAdapter.cpp` and re-open as a fresh finding.
**Sources:** Issue #7a (only `setDevicePixelRatio` hit in `src/` is
`ThumbnailModel.cpp:159` for sidebar thumbnails; `ImageAdapter.cpp` has
zero DPR handling for the main path). See Owner questions §Issue #7a.

### 7. Continuous-mode Down/Up arrow steps by line, not by page/screenful — J1 · Workaround · frequent
**Next action:** land PR #35's screenful-step fix, but FIRST verify
PageUp/PageDown actually reach `QPdfView` — `MainWindow` registers them
as window-level Prev/Next-Page shortcuts that likely shadow the widget
handler (the offscreen UAT bypasses shortcut routing, so it passes green
regardless). Down/Space are the verified win; confirm PageUp/Down live or
adjust the `MainWindow` shortcuts before merge.
**Sources:** archive *2026-05-20 HITL ▸ Navigation shortcuts*; PR #35
(OPEN, base = current HEAD, no new commits since 2026-06-02).
**Notes:** the shadowing concern is now corroborated by an unresolved
GitHub reviewer thread on PR #35; three more review threads open (missing
Space/PageUp coverage, a loose assertion bound). Merge/rework call is an
Owner question (§PR #35).

### 8. FreeText (Text/SpeechBubble) annotations lack /AP appearance streams — J3 · Mars · frequent
**Next action:** emit `/AP` appearance streams for FreeText subtypes
(needs a font resource in `/Resources` and a `BT` text block).
**Sources:** archive *Annotations ▸ PDF annotation persistence*
(6 of the shape subtypes emit `/AP`; FreeText still property-only);
`PdfEditor.cpp` FreeText builder sets only `/Rect`, `/DA`, `/Contents`.
**Notes:** reopen-in-Trailer is fine via the property fallback;
Highlight/Underline/StrikeOut also skip `/AP` intentionally (their
`/QuadPoints` carry the geometry and viewers reconstruct reliably) — not
a task. Degree hinges on whether J3's "reopening shows the same" means
third-party-viewer fidelity (Owner questions §J3 external-viewer scope).

### 9. Photo-batch markup is "too clicky" — no thumbnail/document-list navigation across a batch — J3 · Mars · frequent
**Next action:** add thumbnail-bar / document-list navigation for a
multi-image batch opened in one window so marking up a 5-photo batch
isn't tab-by-tab.
**Sources:** archive *UX polish (2026-04-24) ▸ Window / document model*
(image-batch consolidation partial); `ThumbnailModel` is still 1:1 with a
single `IDocument *`.
**Notes:** no explicit "browse an image batch" job exists — the J3
mapping is inferred.

### 10. Zoom in/out compounds a fixed multiplier, never snapping to standard percentages — J1/J3 · Mars · frequent
**Next action:** in `PdfDocument::zoomIn/zoomOut` (`PdfAdapter.cpp`) and
`ImageDocument` (`ImageAdapter.cpp`) snap to a discrete ladder
(25/50/75/100/150/200…) instead of multiplying `kZoomStep`.
**Sources:** Issue #7c (`PdfAdapter.cpp:44` `kZoomStep = 1.1`, zoomIn/out
537-546 multiply/divide; `ImageAdapter.cpp:46`/403-413 identical; no
discrete snap anywhere).

### 11. OCR results not persisted across reopens — re-runs from scratch every open — J5 · Mars · frequent
**Next action:** persist `OcrController`'s `(document,page)`-keyed cache to
`AppPaths::ocrCacheDir()` so a previously-recognized document doesn't
re-OCR on every reopen.
**Sources:** archive *2026-05-19 HITL ▸ OCR (Workstream F)*;
`AppPaths::ocrCacheDir()` (`AppPaths.cpp:78`) is defined with **zero call
sites**; store is in-memory (`SelectableTextStore`).

### 12. OCR text selection is block-level, not word-level — J5 · Mars · frequent (possibly Workaround)
**Next action:** refine `SelectableTextLayer` selection to word
granularity.
**Sources:** archive *2026-05-19 HITL ▸ OCR (Workstream F)*;
`SelectableTextLayer.cpp` selects at whole-block granularity.
**Notes:** confirm degree in an owner session — if PP-OCRv3 blocks force
over-select-then-trim it is a Workaround, not Mars (Owner questions
§Word-level OCR).

### 13. Sidebar thumbnail rows render taller than the thumbnails (slack on mixed-orientation decks) — J1 · Mars · frequent
**Next action:** implement a per-row `sizeHint` in the thumbnail delegate
so wide/landscape pages don't leave vertical slack in a fixed-height row.
**Sources:** archive *2026-05-20 HITL ▸ Thumbnail sidebar wastes vertical
space (Workstream C partial)*; `ThumbnailDelegate::sizeHint`
(`Sidebar.cpp:61`) is a single fixed height. PR #37 (OPEN, docs-only)
diagnoses it: `sizeHint`/`visualRect` already match at 108px for
portrait; the gap is landscape/wide rows.
**Notes:** merge PR #37's diagnosis first, fixing its stale
`70×100→141×200` DPR worked example (actual is `80×100→160×200`).

### 14. Content-aware first-open sidebar defaults (long doc → thumbnails; short form → hidden) — J1/J6 · Mars · frequent/regular
**Next action:** merge PR #36 after short-circuiting the
`doc->formFields()` full parse behind the pageCount check and fixing the
test that leaves a process-wide per-type default mutated. No
`ContentAwareDefaults` logic exists in `src/` yet.
**Sources:** PR #36 (OPEN, base = current HEAD); archive *2026-05-20 HITL
▸ Content-aware initial UI defaults* (both bullets).
**Notes:** GATE — the two heuristics ("≥3 AcroForm widgets ⇒ form",
"≥20 pages ⇒ auto-open Thumbnails") are decision record
[`docs/decision-records/0003-magic-number-thresholds.md`](docs/decision-records/0003-magic-number-thresholds.md)
items (2)/(3), **status: proposed**. Per gate **G6** this item is **Not
Done** — regardless of code quality — until ADR-0003 is `accepted` (or
those numbers get owner sign-off). CRITERIA.md orders the queue; G6 +
ADR-0003 gate whether it can be marked done. Someday↔Mars tension is an
Owner question (§Content-aware sidebar defaults).

### 15. My Card dialog still cluttered with rarely-used fields (feeds J6 sign/address step) — J6 · Mars · regular
**Next action:** audit `MyCardDialog` (still 15 `QLineEdit` fields) and
trim to the ones that feed J6's fill/sign step; keep the rest behind a
disclosure.
**Sources:** archive *UX polish (2026-04-24) ▸ AcroForm fields ("Trim My
Card")*; `src/ui/MyCardDialog.cpp`.

---

## Someday

Items that degrade **no** listed job (CRITERIA.md §4). They never compete
for the head of the queue; whether they get a periodic sweep or resurface
only when a future finding implicates a job is an Owner question
(§Someday pool policy).

- **Content-aware AcroForm docs: default Inspector + markup-toolbar
  state.** Residual beyond PR #36's sidebar behavior; touches no clause
  of the J6 bar. Someday↔Mars is undrawn.
- **Long documents auto-open the thumbnail sidebar.** SUBSUMED by queue
  #14 (PR #36); kept for traceability, not a separate task.
- **Keyboard shortcut for Two-Page mode.** `Cmd-3` reserved but disabled;
  blocked on `QPdfView::PageMode` lacking a facing-page layout (needs a
  custom widget). `Cmd-1`/`Cmd-2` already satisfy the only J1
  keyboard-mode clause.
- **Embed OCR text as an invisible layer on PDF export.** Makes an
  exported file searchable in external readers; no J-bar requires it (J2
  is search *inside* Trailer). `ImageDocument::exportAs`'s PDF branch only
  `drawImage`s into the `QPdfWriter`.
- **SAM encoder cache eviction beyond the 3-entry LRU.** Smart Lasso
  tooling (DESIGN §6.5.3), not in any J-bar; speculative, no reported
  failure.
- **Disk cache of background-removal candidate scores.** Background
  removal heuristic (DESIGN §6.5.4) isn't in any J-bar; no reported
  slowdown.
- **Sidebar differential update** (diff instead of clear-and-rebuild).
  The Wave-2 debounce was "the big win"; further optimization "not worth
  the complexity until profiling shows it matters."
- **Inspector debounce** (match Sidebar's update pattern). Explicitly
  lower priority; "doesn't dominate undo-replay cost today."
- **Menu organisation review** (Tools→File/Edit regrouping of Export
  As/Screenshot/Flip/Rotate/Adjust Size/Colour). Pure discoverability
  polish; changes no bar's pass/fail.
- **HiDPI: mixed-DPR multi-monitor** (thumbnail cache stale on screen
  change). No J-bar mentions multi-monitor DPR transitions.
- **HiDPI: manual test pass on 1×/2×/3× displays.** Test-coverage task
  (CI is offscreen-only), not a finding against a job bar.
- **Region/window/app screenshot pickers on Linux and Windows**
  (currently full-screen only). AMBIGUOUS — no J-bar names "capture a
  screenshot"; if the owner treats capture as the on-ramp to J3, Windows
  full-screen-only becomes a Workaround (Owner questions §Screenshot
  capture scope).
- **Revisit `QTabWidget` vs `QStackedWidget` for the central widget.**
  Internal refactor; no user-visible bar depends on the widget class.
- **Signature vector storage as a sidecar** (`.strokes.json`).
  Explicitly "not blocking" — the current 2× raster already meets J6's
  legibility bar.
- **Ink pressure metadata lost on cross-app round-trip** (no Trailer
  `/InkList` extension key). Standard `/InkList` carries only x/y; the
  rendered appearance J3 checks is preserved via `/AP`. No current
  degradation.
- **Test-shape principle — generative fixtures** for AutoFill/OCR/bg-
  removal tests. Test-authoring methodology, not a finding against a bar.
- **Wire an ed25519-signed auto-update channel** (Sparkle 2 +
  WinSparkle). Release/distribution infra; the job-degradation function
  doesn't cover update delivery, so a literal §4 read parks it here.
  Active and well-scoped — whether infra needs a *parallel* gate so it
  isn't starved is an Owner question (§Someday pool policy). Tracked in
  detail in [TODO-packaging.md](TODO-packaging.md); carried as a *Next*
  item in ROADMAP.md.
- **PR #37 — docs(todo): thumbnail row-height diagnosis** (docs-only, no
  behavior change). Substance folded into queue #13; merge the PR (fix
  the DPR worked example), it degrades nothing itself.
- **PR #38 — chore(deps): bump actions/checkout 6→7.** Routine CI-only
  Dependabot bump; base is 2 commits behind `main` — rebase, then merge
  whenever.

---

## Owner questions (ambiguity ledger)

Genuinely open questions that need owner judgment. Per the scope rule
these are **not guessed** — they wait for the next interview pass while
the PROVISIONAL defaults above stand so the machine keeps producing a
single next action. A non-empty ledger is normal; an *ignored* one is the
failure mode.

1. **N for the 1.0 dwell test.** *Q:* provisional 4 weeks, owner-tunable —
   confirm or set the number. *Why undecidable:* the dwell window is a
   value judgment about how much continuous real-use proof equals
   confidence; the ranking function scores per-item friction, not the
   release clock. *Unlocks:* fixes the length of the "lived-with-it"
   clock in the §7 1.0 gate, making 1.0 testable rather than open-ended.
2. **1.0 bar wording.** *Q:* the CRITERIA §7 wording was proposed and not
   objected to, never explicitly ratified — needs an explicit yes. *Why:*
   ratification is an owner speech-act; "not objected to" ≠ "confirmed."
   *Unlocks:* locks the definition of 1.0 so every downstream gate
   (parity + dwell) has a ratified target.
3. **Frequency tiers for J1–J8.** *Q:* the whole column is inferred
   (J1–J5 frequent, J6 regular, J7 occasional, J8 periodic) — confirm or
   re-tier. *Why:* the §4 sort multiplies degree × frequency; the
   function consumes these tiers, it can't derive them. *Unlocks:*
   stabilizes the axis the entire ranked list is sorted on (e.g.
   promoting J6 to frequent lifts Trim My Card and form defaults).
4. **Ranking tie-break for incomparable (degree, frequency) pairs.** *Q:*
   provisional default is "any Blocks outranks any lesser degree" —
   ratify or invert (e.g. weight frequency above degree). *Why:* the
   three factors are confirmed but their exchange rate is owner-reserved.
   *Unlocks:* the highest-leverage question — determines whether the head
   is a frequent-job Blocks (current: Line/Arrow handles) or could be a
   frequent Workaround; inverting would lift the frequent-J1/J3
   workarounds above the occasional-J7 Blocks.
5. **The "someday" pool policy.** *Q:* do items degrading no listed job
   get a periodic sweep, or resurface only when a future finding
   implicates a job? *Why:* a process choice about starvation risk, not a
   property of any item. *Unlocks:* decides the fate of the 19 someday
   items (incl. the auto-update infra), which otherwise risk indefinite
   starvation.
6. **J8 scope.** *Q:* does "Preview parity" for scanning require in-app
   scanner drivers (SANE/WIA/ImageCaptureCore, Phase 7 "Later"), or is
   "import already-scanned pages, then edit" sufficient? *Why:* J8 is
   met-or-not depending on where the owner draws the parity line; it
   stands "Blocks by construction" either way until decided. *Unlocks:*
   whether J8's path to done is a large driver-integration effort or the
   much smaller "Insert Pages accepts images + multi-select" fallback.
7. **macOS regressions.** *Q:* macOS is confirmed reference/fallback and
   not scored (§1) — but is a macOS regression nonetheless a release
   blocker, or purely best-effort? *Why:* §1 settles scoring, not
   regression policy; a separate release-gate decision. *Unlocks:*
   whether macOS-only bugs can ever block a release.
8. **Cross-doc drift (1.0 definition) — reconciled this pass.** *Q:*
   PHILOSOPHY.md "What 1.0 means" and ROADMAP.md "Path to 1.0" now both
   defer to CRITERIA.md §7's parity+dwell gate, and PHILOSOPHY.md "What
   Trailer is for" + DESIGN.md §2.5.2 carry the personas-to-lenses
   recentering (CRITERIA.md §2), so a cold reader no longer meets three
   different 1.0 definitions. *Status:* the drift this entry named is
   gone; nothing unique stays open here. Owner ratification of the
   recentering lives in *1.0 bar wording* (2) and *Governance
   ratification* (9); the last stale fact ("Today's version is 0.1") in
   *Version drift* (10). Delete this entry once those close.
9. **Governance ratification of the acceptance-gate recentering.** *Q:*
   moving the acceptance gate from reference-user to owner touches
   PHILOSOPHY "How decisions get made" step 1. *Why:* whether this
   warrants a PHILOSOPHY edit or an ADR is a governance judgment reserved
   to the owner. *Unlocks:* confirms the recentering is intended and
   legitimate rather than a quiet override.
10. **Version drift — reconciled this pass.** *Q:* `VERSION` read 0.2.0
    while PHILOSOPHY.md "What 1.0 means" said "Today's version is 0.1";
    PHILOSOPHY.md now says 0.2. *Why kept:* recorded so the owner can
    veto the factual correction. *Unlocks:* nothing further — close on
    sight unless wrong.
11. **Operationalizing the invariant in ROADMAP.** *Q:* the collapse is
    done this pass — ROADMAP.md's "Now" is a single computed head and this
    file's *Next action* reads off the same item, so the invariant now
    holds in the working docs, not just CRITERIA.md §3; the residual is
    whether to keep hand-maintaining this ranked queue or migrate to the
    generated findings-queue model the recorder→agent pipeline implies
    (adjacent to §Findings storage). *Why:* which model to adopt
    long-term is an owner/pipeline choice, not something the ranking
    function decides. *Unlocks:* whether the always-clean queue stays a
    curated document or becomes a generated one.
12. **Findings storage.** *Q:* where do the ranked findings physically
    live — this file, a FINDINGS file, issue labels, the recorder-agent's
    output format? *Why:* §3/§6 describe the five-field shape but not its
    store; choosing the sink is a pipeline decision. *Unlocks:* an
    operational always-clean queue (not just a described one) for the
    recorder→agent pipeline to write into.
13. **PR #35 — do PageUp/PageDown reach `QPdfView`?** *Q:* or does
    `MainWindow`'s window-level Prev/Next-Page shortcut shadow them so
    half the fix is dead code in the shipped app? *Why:* a checkable Qt
    dispatch-precedence fact source-reading can't resolve — the offscreen
    UAT injects key events directly into the widget, passing green
    regardless; needs a live HITL pass. *Unlocks:* the merge/rework call
    on queue #7.
14. **Issue #7d degree.** *Q:* is silent "changed on disk"
    overwrite/undo-desync a Workaround (avoid opening the file elsewhere)
    or a Blocks (data loss forces leaving Trailer)? *Why:* turns on
    whether silent corruption counts as "the job cannot be finished in
    Trailer" — an owner call about data-safety tolerance. *Unlocks:*
    whether queue #5 is head-adjacent (Blocks) or mid-queue (Workaround).
15. **Issue #7a — fixed or mis-marked closed?** *Q:* the issue is marked
    closed but no traceable main-view DPR fix exists. *Why:* code
    inspection found zero `setDevicePixelRatio` hits in the main image
    path (only the unrelated thumbnail fix); resolving needs a live
    session on a real 2× screenshot. *Unlocks:* whether queue #6 is a
    live finding to fix or genuinely resolved.
16. **J3 external-viewer fidelity scope.** *Q:* does J3's "reopening the
    saved file shows the same" mean Trailer's own reopen (fine today) or
    third-party-viewer fidelity? *Why:* J3's bar text, unlike J4's and
    J6's, doesn't say "another app" for this clause. *Unlocks:* the
    degree of FreeText `/AP` (queue #8) — Mars vs Blocks — and whether
    every markup job carries a third-party-fidelity requirement.
17. **Word-level OCR selection degree.** *Q:* Mars (coarse but works) or
    Workaround (owner over-selects a block then trims)? *Why:* depends on
    how large PP-OCRv3's blocks are in real documents — an empirical
    observation, not derivable from the selection code. *Unlocks:*
    correct placement of queue #12 within the frequent-J5 tier.
18. **Content-aware sidebar defaults — mar or someday?** *Q:* does a
    non-auto-opened sidebar "mar" J1/J6, or is the job fine (sidebar one
    toggle away)? *Why:* the TODO triage scored it someday, the PR triage
    scored it Mars — exactly the owner-judgment §4 reserves; also
    unconfirmed whether Preview itself auto-pops thumbnails by page count.
    *Unlocks:* whether queue #14 / PR #36 is a ranked mars item worth
    merging now or a someday nicety; feeds the general someday-vs-mars
    boundary (§Someday pool policy). (Merge is *separately* gated by
    ADR-0003 / G6 — see queue #14.)
19. **Screenshot capture in-scope for the J3 substitution contract?**
    *Q:* region/window/app pickers are missing on Linux/Windows. *Why:*
    no listed job names "capture a screenshot" — J3 begins once an image
    exists; whether capture is the on-ramp is an owner scope call.
    *Unlocks:* moves the screenshot-picker item from someday into a
    ranked J3 Workaround if capture is deemed in-scope.

---

## Archive

Compact, provenance-preserving history. Strikethrough items from the
original passes are summarized to one line each with their evidence;
every still-open item has been lifted into the queue, someday, or the
ledger above, and each pass below points to where its live items went, so
nothing is dropped.

### Process notes

- **Designer / non-technical-user review → smoke session (upstream fold,
  merged `9d7eaac`).** The old "designer review" bullet is now codified
  as the reference-user smoke session — see
  [`docs/smoke-session.md`](docs/smoke-session.md) for the protocol
  (fresh build, non-maintainer observer, three open-do-close cycles on a
  text PDF + scanned PDF + photo; observations land in a dated subsection
  of this file). The original bullets that lived here (modal dialogs that
  interrupt, controls enabled-but-noop, hidden entry points, lost direct
  manipulation, too-loud/too-quiet feedback) are now positive rules in
  PHILOSOPHY's *How Trailer reduces friction* section. Trigger: schedule
  a smoke session before any 1.0 polish milestone, and opportunistically
  whenever a willing non-maintainer is in the room.
- **Decision records now govern several queue items.** Adjudicated
  decisions live in [`docs/decision-records/`](docs/decision-records/)
  (full ADR lifecycle `proposed → accepted → superseded-by`) — distinct
  from the lighter [`docs/decisions/`](docs/decisions/) log. Pointers
  wired into the queue above: **ADR-0001** (Select-All semantics,
  proposed) → queue #3; **ADR-0003** (magic-number thresholds, proposed;
  gate G6) → queue #14; **ADR-0004** (never-worry-save invariant,
  proposed) → adjacent to queue #5. Settle the record before shipping
  the gated item.
- **Test-shape principle (2026-04-28).** Prefer generative fixtures
  (`writeRandomFormPdf(seed, recipe)`, invariant assertions across many
  seeds) over pinning a UAT to one reviewer-supplied form. Applies to
  AutoFill, OCR, and background removal. (Carried as a someday item.)

### Shipped — 2026-05-20 HITL pass (post-#25, on `main`)

Live walkthrough after PR #25. Context: at the time
[ROADMAP.md](ROADMAP.md)'s "In flight" section still described Wave 1–4
as upcoming though PR #24 (`4dba247`) had already merged it; reframed in
`969d46f`.

- Rectangle rough-edges trio — DONE: auto-switch to Select after
  placement (UAT-ANN-131); restyle from Inspector without vanishing;
  rectangles-disappear-without-deletion — all one root cause (dangling
  `const Annotation*` held across the modal `QColorDialog`), fixed by
  snapshot-then-refetch-by-id (UAT-ANN-130).
- Search "Close" button now collapses the toolbar slot — DONE
  (`51e59e2`; hides the wrapping `QWidgetAction`, Esc shares the path).
- Page-mode shortcuts — DONE: `Cmd-1`→Continuous, `Cmd-2`→Single Page,
  zoom moved off the digit row (Actual `Cmd-0`, Fit Page `Cmd-9`).
- **Still live from this pass:** thumbnail row-height → queue #13; Select
  All / Copy Page as Image → queue #3; content-aware first-open defaults
  → queue #14 + someday; `Cmd-3` Two-Page (reserved-but-disabled) →
  someday; continuous-mode `↓` viewport-step → queue #7.

### Shipped — 2026-05-19 HITL pass (live use on Windows 11)

Landed as PR #24 in four waves; the items here were explicit scope
deferrals.

- Linux power detection for the ML scheduler — DONE (`9b24eb4`;
  `PowerSource.cpp` scans `/sys/class/power_supply/*`).
- **Still live from this pass:** shape-aware Line/Arrow handles (Workstream
  D) → queue #1; auto-trigger model download from background OCR
  (Workstream F) → queue #2; OCR disk cache → queue #11; word-level OCR
  selection → queue #12; embed OCR text on PDF export → someday; SAM
  encoder-cache eviction (Workstream G) → someday; background-removal
  candidate-score cache (Workstream H) → someday; Sidebar differential
  update → someday; Inspector debounce → someday; Inspector chrome-restore
  → shipped/already-true (`MainWindow` `restoreState()` walks every dock;
  the earlier "CODE-FACT TENSION" is resolved).

### Shipped — 2026-04-30 HITL pass (live use on macOS)

All 22 items landed. Bugs/affordances: drag-file-onto-Dock opens it
(`uat_fnd_050`, macOS/unscored); `Cmd-Tab` during a Zoom-Lens drag no
longer leaves an undo-less annotation; Select-tool click-drag no longer
draws a stray rectangle (`4b74f80`); PDF thumbnails composite on opaque
white in dark mode. UX defaults: default tool is Select; sidebar ships
`Mode::Hidden`; default stroke dark grey `QColor(60,60,60)`; Inspector on
`Ctrl/Cmd+I`; macOS launch-with-no-files opens a window (`8bd9ad0`,
unscored); Magnifier deactivates on Esc/Cmd-Tab/focus-loss. Inspector no
longer auto-shows on annotation click. Markup bar uses SVG icons (29+6,
themed via `IconHelper`; `ff8541a`). Slim main toolbar
(`buildMainToolbar`). Window menu on macOS (unscored). Go menu
(First/Previous/Next/Last). Sidebar explicit modes
(Hidden/Thumbnails/Search Results/TOC/Highlights & Notes;
`uat_toc_010..012`, `uat_hn_010..012` — the stale 2026-05-11 "blocked on
placeholders" note is superseded). Search: yellow match highlight
(`uat_vwr_064`), "Match X of Y" counter, auto-open Search-Results
sidebar. `Tools → Reset Trailer Settings`. `File → Export as PDF` for
images (J8 fallback path; J8 overall still Blocks-by-construction on
scanner acquisition). **Nothing from this pass is still live.**

### Shipped — 2026-04-24 UX polish pass

- PDF text selection + text-aware markup — DONE: click-drag selects text
  on text-layer PDFs; Highlight/Underline/StrikeOut follow wrapped-text
  quads; contextual tool gating hides text-aware tools on non-text docs
  (`test_markup_toolbar`).
- Annotation editing — DONE: re-selectable/move/resize/restyle after
  creation (hitTest + handles + Inspector; `3a9a5bc`, `d611d1b`,
  `0b8d274`); markup toolbar auto-shows on first annotatable doc.
- Inline editing — DONE: text boxes edit in place via `QPlainTextEdit`
  (`deb1a40`); signature placement via popover under Sign-Here.
- Signature/freehand — DONE for the 80% case (bigger canvas, 2× raster,
  pressure priority, smoothing); pressure-aware Ink tool with per-segment
  width in screen and saved `/AP`.
- Window-per-file is the default (`open_files_in=new_window`; tabs
  opt-in).
- AcroForm direct-manipulation — DONE: form widgets visible by default on
  any fillable PDF; subtle field-border cue; Tab navigates in reading
  order; AutoFill demoted into `Tools → Forms →`.
- **Still live from this pass:** image-batch thumbnail navigation → queue
  #9; Trim My Card → queue #15; `QTabWidget` vs `QStackedWidget` →
  someday; signature vector sidecar → someday; Ink pressure cross-app
  round-trip → someday; test-shape generative fixtures → someday.

### Shipped / confirmed — Cross-cutting

- HiDPI sidebar thumbnails render at DPR-scaled native resolution — DONE
  (`af6621c`).
- HiDPI screenshot capture (`grabWindow(0)`), Magnifier overlay, and
  SignatureCanvas 2× raster — CONFIRMED DPR-correct, no bug.
- PDF undo/redo for the five page mutations (Rotate/Delete/Move/Insert/
  Crop) — DONE; merged into one chronological typed log shared with
  annotation edits; bounded histories evict in lockstep; the 2026-07-10
  undo-stabilization batch (`60406dc`) fixed the undo-cap desync.
- PDF save/export moved to a worker thread (async, `QProgressDialog`) —
  DONE.
- Continuous-mode annotation overlay drift — **SHIPPED / SUPERSEDED.** The
  per-page coordinate mapping (`PdfAdapter.cpp` `pageOriginInView` +
  `AnnotationOverlay` per-annotation-page draw/hit-test) has existed
  since `6ecc366`, which *predates* the June-1 baseline; each annotation
  maps through its own page's origin, not just `QPdfView`'s current page.
  The old TODO/ROADMAP prose that described this as an open bug was
  misranked from stale text alone. Residual (not dropped): TODO/ROADMAP
  wording is being corrected here; a quick live HITL confirmation on a
  real multi-page Continuous-mode doc is still worth doing to close it out
  — the code-level bug is gone.
- **Still live from Cross-cutting:** annotation re-indexing on
  delete/move/insert → queue #4; FreeText `/AP` appearance streams →
  queue #8.

### Resolved — Issues & PRs

- Issue #7b — window resizes to fit the opened image/document
  (`contentSizeHint`, PR #24 / hardened `4c12205`) — DONE.
- Issue #11 — macOS File/New/Open/Acquire menu with no window open
  (`8bd9ad0`) — DONE (macOS/unscored).
- Issue #18 — ML model status/consent transparency + `ModelManagerDialog`
  (`ed60618`/`adacec0`/`7dd282c`) — DONE, serves J5 consent friction.
- Issue #20 — CI builds all artifacts (DMG/MSI/RPM + release-candidate
  gating + dockerless Windows cross-compile PR #43 + ccache PR #40/#44) —
  DONE.
- PR #33 — a11y: name Search button + guard unnamed icon buttons
  (UAT-XCT-061) — MERGED `8003f11`; 3 cosmetic review nits (test wording,
  vacuous-pass guard, doc wording) open as unblocking fast-follow.
- PR #34 — Edit > Copy Page as Image base feature — MERGED `5771b15`;
  leaves the J3 annotation-compositing Blocks now at queue #3; nit: magic
  `2200` → named `constexpr`.

### Obsolete / out of scope

- Apple Pencil / iOS — OUT OF SCOPE (Trailer is Qt6 widgets, desktop-only).
- AutoFill matcher tuning — RETIRED as a "gimmick dead end" (2026-04-28
  reframe to direct field-click-and-type, which is what the J6 bar tests).
- Apple Developer Team ID / signing identity — DEFERRED indefinitely
  (macOS unscored; awaits a funding plan). See TODO-packaging.md.
- notarytool macOS notarization — DEFERRED (gated on Apple Developer
  enrollment).
- macOS packaging in CI — SETTLED: the unsigned adhoc-signed DMG is the
  intended end state for now.
