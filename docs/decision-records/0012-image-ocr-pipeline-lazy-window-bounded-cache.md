# 0012 — OCR pipeline for images: auto-OCR small images, lazy per-page for large docs, on-demand triggers, bounded disk cache

- **Status:** accepted
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-15
- **Date accepted / superseded:** 2026-07-15 (accepted)
- **Builds on:** ADR-0002 §3 (Missing model / no-lying honesty) and ADR-0008 (staged document open: never block the GUI thread on whole-file work). **Complements** ADR-0011, which reconciled the PDF recognize-text affordance; 0011 fixed *when the chip fires and how it routes*, and 0012 extends the OCR **pipeline** to images and defines the eager-vs-lazy scheduling and caching policy that governs *when text is computed at all*. It does not amend 0011's affordance rules.

## Context

Trailer's selectable-text pipeline populates `SelectableTextStore` (per-document,
per-page OCR results, keyed by page index) and paints it through
`SelectableTextLayer`. Today the store is **in-memory only** — its own header says
so: *"In-memory only — no disk persistence in this phase. Re-OCRing on reopen is
acceptable; a disk cache is a follow-up."*
(`src/document/SelectableTextStore.h:18-20`). Invalidation is already by content
hash: callers stash the source-image hash (`hashImageContent`,
`SelectableTextStore.h:97`; `contentHashFor`, `:75`) alongside results, and a
per-page pixel edit calls `invalidate(page)` (`:65`).

**What ships today (so this record is not misread as describing the target):**

- **Auto-OCR scope is page-count-gated, not size-aware.** `OcrController::onVisiblePageChanged`
  (`src/ui/OcrController.cpp:53-108`) runs background OCR on the visible page **± 1
  neighbour** (visible page at `VisiblePage` priority, neighbours at `Prefetch`;
  `:99-107`), but only for documents that are **not** "large":
  `isLargeDoc()` is `pageCount() > 50` (`OcrController.cpp:47-52`,
  `kLargeDocPageThreshold = 50` at `OcrController.h:125`). A large document gets
  **no** ambient OCR at all — it is cancelled down to explicit user action
  (`OcrController.cpp:79-83`). There is **no pixel-area rule**: a single enormous
  image and a small one are treated identically (both single-page, both eligible),
  and the "±1" window is fixed regardless of document size.
- **No garbage-discard contract.** There is no defined behaviour for an OCR pass
  that returns zero blocks or only low-confidence noise beyond "the store has no
  usable results"; the completion-status wording for that case is unspecified.
- **No disk cache.** Every reopen re-OCRs from scratch.
- **CPU-discipline gates already exist.** The `[ml.scheduler]` knobs
  (`src/settings/Settings.h:134-141`) are `recognize_text_in_background` (default
  **true**) and `run_on_battery` (default **false**); `MlScheduler` suppresses
  `Prefetch`/`Idle` speculative work on battery when `run_on_battery=false`
  (`src/ml/MlScheduler.h:81-97`, `MlScheduler.cpp:65-90,256-262`) while
  `VisiblePage`/`UserAction` always run.

**The decision on the table** is the owner's directive (owner authority — recorded
verbatim below) for how the pipeline should *schedule and cache* OCR across the
size spectrum, now that images are in scope alongside PDFs:

> "For such a small image, it should run OCR automatically in the background. If it
> turns out there isn't really any text, or the text is noisy/garbage, then we can
> dismiss that discovered layer silently. For larger documents, we probably don't
> need to greedily calculate OCR. It'd be sane to calculate OCR per-page starting
> with the page(s) that are visible as thumbnails and perhaps a few above/below. If
> the user jumps to another part of the document, it can calculate OCR on demand. If
> the user does a search and replace, then it'd need to calculate OCR as it scans
> the document. It can all be relatively transparent to the user. We don't want to
> cook the CPU for large document OCR, but we want search and replace to work. Some
> sort of space efficient caching would be appropriate too. The user shouldn't know
> if we cache stuff like this, and we shouldn't let it grow unbounded if we persist
> it to disk."

This record formalizes that directive into concrete, checkable thresholds, runs
the four persona lenses against it to stress-test it, records the admissible
objections, and — the arbiter having found no admissible objection that defeats it —
**accepts** it. The one genuine tension the personas surface (silent discard vs the
no-lying honesty rule of ADR-0002 §3) is reconciled in the verdict, not waved off.

### External grounding

The tiered policy matches established viewer/OS convention:

- **Apple Live Text** recognises text in images **automatically and in the
  background** the moment an image is displayed, with no user action and no
  persistent "recognize?" prompt; when an image has no text, nothing is surfaced —
  the feature simply yields no selectable text
  (https://support.apple.com/guide/preview). This is the small-image auto-OCR +
  silent-discard shape.
- **Adobe Acrobat / PDF Expert** OCR **on demand** for large scanned documents
  rather than eagerly OCRing every page on open, precisely to avoid pinning the CPU
  on a big deck — the lazy, per-page-as-needed shape the owner describes.
- The **"don't cook the CPU"** constraint is already encoded in Trailer's own
  `[ml.scheduler]` battery/background gates (above); this record binds the new
  scheduling to those existing gates rather than inventing a second throttle.

## Options

- **A. The owner's tiered policy (this record).** (1) Eagerly auto-OCR a *small*
  image in the background on open; (2) silently discard the discovered text layer
  when OCR yields nothing usable, while the completion status stays honest; (3) for
  anything above the small threshold, do **not** greedily OCR — OCR a lazy window of
  the visible page(s) **± N**, OCR on demand when the user jumps, and OCR pages as a
  search/replace scan reaches them, all within the existing `[ml.scheduler]` CPU/battery
  gates; (4) a bounded, content-hash-keyed, size-capped LRU on-disk cache that is
  invisible to the user and never grows unbounded. The disk-cache *design* is decided
  here; its *implementation* is deferred to the backlog.
- **B. Greedy whole-document OCR on open.** OCR every page eagerly when a document
  opens. Simplest "search always works instantly" story. Retained to be rejected: it
  is exactly the "cook the CPU for large document OCR" the owner rules out, and it
  violates ADR-0008's don't-block-on-whole-file-work invariant.
- **C. No auto-OCR; explicit Recognize Text only.** Never OCR without a user click.
  Retained to be rejected: a small image's text would never become selectable
  "transparently," contradicting "it should run OCR automatically in the background."
- **D. Persist a visible affordance after a garbage OCR** (e.g. keep a "No text found —
  Recognize?" chip). An alternative to silent discard for the zero/noise case. Weighed
  against A's silent discard in the debate.
- **E. Unbounded persistent disk cache.** Persist every page's OCR forever. Retained
  to be rejected: "we shouldn't let it grow unbounded if we persist it to disk."

## Personas debate

- **Office non-technical user:** Drops a screenshot or a small scanned receipt in and
  expects to select the text like Live Text does — no button, no wait, no jargon.
  Served by A's small-image auto-OCR. Fears a big scanned contract making the fan spin
  and the app lag (Option B). If a picture with no text flashed a "recognize failed"
  banner they would read the app as broken — favours A's silent discard over D, **but**
  would be misled if a status line claimed "Text recognition complete" on an image that
  yielded nothing (the honesty seam the verdict resolves).
- **Older careful user:** Distrusts background work they can't see and fears the laptop
  quietly overheating on battery. Wants the "don't cook the CPU" promise to be real and
  checkable — favours A only because its background OCR stays inside the existing
  `run_on_battery=false` / `recognize_text_in_background` gates. Would be unsettled by
  an app that *claimed* success when it found nothing (an honesty concern that cuts
  against a naive reading of "dismiss silently"), and reassured by a quiet, truthful
  "No text found."
- **Power migrator (ex-Preview/Acrobat):** Expects Acrobat/PDF Expert behaviour — big
  scans are OCR'd on demand, not all at once — and Live Text's instant small-image
  recognition. A matches both. Rejects B (no peer tool greedily OCRs a 500-page scan on
  open) and C (Live Text set the "just works" bar for small images). Expects
  search/replace to actually find text on a not-yet-visited page — A's OCR-as-scanned
  path is the one that must hold.
- **Occasional user:** Won't know what OCR is or that caching exists, and shouldn't have
  to. Served by A's "transparent to the user" framing and the invisible cache ("the user
  shouldn't know if we cache"). Harmed by any design that surfaces cache state or leaves
  a dangling empty layer they can't act on.

## Admissible objections

Each names a user/persona, a step in a real flow, and the failure that user would hit.

- **Office / older-careful user, Option A's "dismiss silently" vs ADR-0002 §3 honesty
  (the decisive tension).** At the step "I OCR'd (or the app auto-OCR'd) an image that
  turned out to have no text," a design that *both* discards the layer *and* shows a
  completion toast reading "Text recognition complete" would be **lying** — claiming
  success where there was none — which ADR-0002 §3 and PHILOSOPHY *No lying controls*
  forbid. This is the one objection that reshapes A rather than defeating it: it forces
  the reconciliation that "silent discard" governs the **layer/affordance** (don't leave
  an empty selectable layer or a persistent chip cluttering the UI), while the
  **completion status must stay honest** ("No text found"), never a false success claim.
  Answered by the verdict's split of "discard the layer silently" from "report the
  outcome truthfully."
- **Older-careful user, Option B (greedy), battery step.** At "I opened a big scanned PDF
  on battery at a café," greedy whole-document OCR pins a core and drains the battery —
  the exact "cook the CPU" failure the owner rules out, and a violation of ADR-0008's
  don't-block-on-whole-file invariant. Decisive against B; the reason A binds background
  OCR to the `[ml.scheduler]` gates.
- **Power-migrator, Option C (explicit-only), search step.** At "I run search/replace on
  a large scan and expect it to find text on page 300 which I've never scrolled to,"
  explicit-only OCR finds nothing because page 300 was never recognised — search silently
  under-reports. Decisive against C; the reason A includes the OCR-as-scanned path so
  "search and replace [must] work."
- **Occasional user, Option D (persistent garbage affordance), no-text step.** At "I
  opened a photo with no text," a lingering "No text found — Recognize?" chip is clutter
  the user can neither use nor clear, on a document where there is genuinely nothing to
  do. Favours A's silent discard over D. (D is not *wrong* about honesty — it is answered
  more cheaply by A's honest transient status than by a persistent control.)
- **Any user, Option E (unbounded cache), long-session step.** At "I've opened hundreds
  of scanned pages over a week," an unbounded on-disk OCR cache grows without limit —
  precisely what the owner forbids. Decisive against E; the reason A caps the cache with
  an LRU size ceiling.

### Rejected as naked preference

- "Just OCR everything up front, it's simpler." — rejected: states no user, step, or
  failure; the concrete failure is the older-careful user's battery drain / CPU pin under
  Option B, already admissible above. Simplicity is not a checkable user benefit here.
- "A ±3 window feels more thorough than ±2." — rejected as stated: names no user, step, or
  failure and no measured difference. The window size is set below as a hand-tuned value
  with a rationale, not by feel; a *measured* case that ±2 under-serves a real jump/scan
  pattern is the admissible form and is listed under *Evidence required to reopen*.
- "Users should be able to see and manage the OCR cache." — rejected: contradicts the
  owner's "the user shouldn't know if we cache," and names no failure a *hidden, bounded*
  cache causes. Surfacing cache state is the occasional user's harm above, not a benefit.

## Checkable threshold this record would establish

Phrased so an agent or reviewer can independently declare pass/fail, mirroring
ADR-0011's G-numbered assertions. G2 evidence is offscreen `QWidget::grab()` under
`QT_QPA_PLATFORM=offscreen` per AGENTS.md G2; scheduling assertions use the OCR
batch/handle spies already used by the OcrController tests.

- **G12.1 — Small-image eager auto-OCR; large is not greedy.** *Small* is defined
  concretely as **a single-page document (`pageCount() == 1`, which images always are)
  whose source pixel area is ≤ 4 megapixels** (≤ 4,194,304 px, e.g. 2048×2048).
  Rationale for 4 MP: it spans a phone screenshot and a typical letter-size scan at
  ~240 DPI, recognises on the worker thread well inside the sub-1 s interaction budget,
  and one such page cannot "cook the CPU"; a single 4 MP cap is chosen over an
  uncompressed-byte cap because pixel area (not colour depth) is what drives OCR cost and
  it is available before decode via the existing page-size hint. Assertion: opening a
  single-page image with area ≤ 4 MP (model present, `recognize_text_in_background=true`,
  not battery-suppressed) submits background OCR for page 0 at `VisiblePage` priority
  **with no user action** — `SelectableTextStore::hasResults(0)` becomes true, or the
  G12.2 discard path fires. Opening a single-page image with area **> 4 MP** submits **no**
  eager whole-image auto-OCR on open; it falls to the G12.3 lazy path. (This adds a
  pixel-area rule the current code lacks; `> 50` pages remains large by construction and
  is unaffected.)
- **G12.2 — Silent discard of the layer + honest completion status.** When an OCR pass for
  a page yields **zero blocks, or only blocks below the confidence floor** (noise/garbage),
  (a) **no persistent selectable-text layer and no persistent recognize affordance** is
  left for that page — `grab()` of the viewer/status bar after completion shows no dangling
  empty-layer control or "recognize?" chip attributable to the discard, and the store holds
  no *usable* results for that page; **and** (b) the completion status line is an **honest
  "No text found"-class message**, asserted by string check to be the no-text wording and
  **never** a false success claim such as "Text recognition complete." This is the explicit
  reconciliation of "dismiss that discovered layer silently" (part a) with ADR-0002 §3 / no
  lying (part b): the *layer* is discarded silently; the *outcome* is reported truthfully.
- **G12.3 — Lazy window ± N, on-demand on jump, OCR-as-scanned, within CPU gates.** For a
  document above the small threshold (or multi-page), background OCR is confined to the
  visible page(s) **± 2** (**N = 2**, a 5-page window). Rationale for N = 2: a modest,
  hand-tuned step up from today's ±1 that pre-covers the thumbnails immediately above/below
  the fold without a large speculative burst; the neighbours run at `Prefetch` (first
  cancelled, battery-suppressed), so widening the window trades directly against the
  don't-cook-the-CPU budget. Assertions: (i) **window** — after settling on page *k*, pages
  in `[k-2, k+2]` are OCR-submitted and **no page outside that window** is; (ii) **jump** —
  jumping to page *j* recenters OCR on `[j-2, j+2]` and cancels/does-not-submit pages outside
  it (on-demand, not greedy); (iii) **search/replace** — a scan submits pages **only as it
  reaches them**, not the whole document at once; (iv) **CPU discipline** — with
  `run_on_battery=false` on battery, only the `VisiblePage` submission runs while the ±2
  neighbours and scan-ahead (`Prefetch`/`Idle`) are suppressed, and with
  `recognize_text_in_background=false` there are **zero** background OCR submissions
  (explicit Recognize Text is unaffected). This is what operationalises "we don't want to
  cook the CPU … but we want search and replace to work."
- **G12.4 — Bounded on-disk cache: design accepted here, implementation deferred.** The
  on-disk OCR cache, when implemented, is **keyed by content hash**
  (`hashImageContent`/`contentHashFor`), bounded by a **total-size ceiling of 256 MB with
  LRU eviction** (matching the thumbnail cache budget landed in PR #55 for one memory story),
  **invalidated on per-page pixel edit** via the existing `SelectableTextStore::invalidate(page)`,
  and **never user-visible**. Assertions for the *implementing* PR: (i) inserting entries past
  the 256 MB ceiling evicts least-recently-used entries so total on-disk size stays ≤ the
  ceiling (never unbounded); (ii) editing a page's pixels drops that page's cached entry;
  (iii) reopening a document whose page content hash matches a cached entry restores
  selectable text **without re-OCR**; (iv) no user-facing control or status ever exposes the
  cache. **This record accepts the design only** — `SelectableTextStore` is in-memory today
  (`SelectableTextStore.h:18-20`), and the disk-cache implementation is deferred to a backlog
  item (filed alongside this record). G12.4 is the acceptance test that item must meet.

## Arbiter verdict + rationale

**Accepted 2026-07-15 — Option A (the owner's tiered policy), with the honesty
reconciliation folded in.** The policy is the owner's verbatim directive (owner
authority); the arbiter's job here was to stress-test it against the four persona lenses
and confirm no admissible objection defeats it — and none does. Options B, C, and E each
fail a decisive admissible objection by construction (B cooks the CPU on battery / violates
ADR-0008; C makes search under-report on unvisited pages; E grows unbounded), so each is
rejected. Option D (a persistent garbage affordance) is rejected in favour of A because the
occasional user's clutter objection is answered more cheaply by A's honest *transient* status
than by a persistent control the user cannot act on.

The one objection that genuinely reshaped the policy is the honesty tension the office and
older-careful lenses raised against a naive reading of "dismiss silently." The resolution,
now binding as **G12.2**, splits the directive cleanly: *silent* governs the **layer and any
persistent affordance** — we do not clutter the UI with an empty selectable layer or a
"recognize?" chip when there is nothing to select — while the **completion status stays
honest** under ADR-0002 §3, reporting "No text found" rather than a false "Text recognition
complete." This is the same no-lying spine ADR-0002 §3 and ADR-0011 enforce, applied to the
image OCR outcome. It honours the owner's "dismiss that discovered layer silently" literally
(the *layer* is what is dismissed) without letting it become a lie.

The concrete thresholds the record commits to — **≤ 4 MP single-page = small/eager**, **> 4 MP
or multi-page = lazy**, **± 2 lazy window with on-demand jump and OCR-as-scanned search**, all
**bound to the existing `[ml.scheduler]` battery/background gates**, and a **256 MB
content-hash-keyed LRU disk cache** — are the checkable form of "run small images
automatically, don't greedily OCR large docs, keep it transparent, don't cook the CPU, cache
space-efficiently and never unbounded." The disk-cache **implementation** is explicitly
deferred to the backlog (the store is in-memory-only today per `SelectableTextStore.h:18-20`,
and re-OCR-on-reopen remains acceptable until the cache lands); G12.4 is the acceptance test
for that deferred work. The lazy-window and OCR-as-scanned scheduling for large/multi-page
docs is likewise filed as a backlog item scoped to G12.3, as it extends the current ±1 /
large-doc-no-ambient-OCR behaviour rather than shipping in the images PR that accepts this
record.

## Evidence required to reopen

A measured case where one of the chosen thresholds misfires on a real document, plus owner
sign-off. Specifically: (a) a real jump/scan usage pattern where the **± 2** window
demonstrably under-serves (e.g. a measured stall on a common navigation pattern that ±3 would
avoid) or over-serves (measurable CPU/battery cost); (b) a real single-page image near the
**4 MP** boundary where eager-vs-lazy is measurably the wrong call; (c) a document class where
the **confidence floor** for garbage discard drops real text or keeps real noise; or (d) a
usage pattern where the **256 MB** cache ceiling thrashes (constant re-OCR) or is wastefully
large. Naming "someone prefers a different number" is not sufficient — the reopen needs a
measured failure at a named step, per the admissibility rule.
