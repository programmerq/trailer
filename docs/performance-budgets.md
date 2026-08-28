# Trailer — Performance Budgets

> **STATUS: PROPOSED — FOR OWNER RATIFICATION.**
> The response-time limits the latency budgets cite are grounded in published
> research; but a latency number only *binds* once it names the machine and the
> input file it is measured on. Because no reference rig or corpus exists yet
> (see *Reference rig + corpus* below), budgets **B1–B4 are advisory** until the
> owner ratifies both — only the corpus-independent wall-clock budgets **B5/B6
> bind** today. The absolute budgets (memory envelope, binary size) are the
> maintainer's frugality intuition written down so they can be *measured against
> and ratified* — not yet accepted numbers. Each row is marked **[cited]** or
> **[owner-intuition]**, and B1–B4 additionally carry
> **[advisory until reference rig + corpus ratified]**.
> Once ratified, the frugality budgets back gate G9 in
> [`../AGENTS.md`](../AGENTS.md).

## Framing

The target frame is the maintainer's, and it is deliberately old-fashioned:
**most interactions should land well under 1 second**, and the app should feel
light on a machine that is not new — a Win9x-era frugality ethos (see
[`../PHILOSOPHY.md`](../PHILOSOPHY.md) → *Frugal by construction*). Perceived
responsiveness is governed by three published limits:

- **~0.1 s (100 ms)** — feels instantaneous; no special feedback needed.
- **~1.0 s** — flow of thought preserved; delay noticed but tolerable.
- **~10 s** — attention lost; a percent-done indicator and a cancel path are
  required.

Source: NN/g, *Response Times: The 3 Important Limits* —
https://www.nngroup.com/articles/response-times-3-important-limits/

**Out of scope — pathological inputs.** These budgets describe *typical* files
the reference user actually handles (bills, lease addenda, tax forms, scanned
paperwork, ordinary photos). Adversarial or pathological inputs — a 20 GiB
multi-page TIFF, a PDF with tens of thousands of annotations, a maliciously
malformed file — are **explicitly out of scope** for these budgets. Trailer
must fail such inputs *gracefully* (no crash, no silent corruption, a cancel
path), but it is not required to meet the latency numbers below on them.

**The one standing size exception** is the ML runtime (ONNX Runtime + the
U²-Net / MobileSAM / PP-OCRv3 weights): large by construction, mitigated
because the weights download once on first use with consent rather than
shipping in the binary. The binary-size envelope below is measured *without*
the downloaded weights.

## Invariants (not just budgets)

These are pass/fail behaviours, not tunable numbers. They are
**corpus-independent and structural** — first-page render before a full-file
read, the UI never blocking during long work, no silently-inert shortcut — so
they are the part of this file that CI can and does enforce (they hold on any
machine and do not depend on wall-clock timing). The latency numbers below are
**not** CI-enforced; see *How the latency budgets are verified*.

- **First-page render must not block on a full-file read.** The first visible
  page renders from a partial/streamed read; the app must never wait for the
  entire file to be read before showing page 1. A large multi-page PDF or a
  large image must show *something* — page 1, or a placeholder that fills in —
  as soon as possible, and the UI stays usable while the rest loads.
  **[cited]** Apple HIG *Loading*: "show something as soon as possible," keep
  the app usable while content loads, download large assets in the background —
  https://developer.apple.com/design/human-interface-guidelines/loading
- **The whole UI never blocks during long work.** Long operations (OCR,
  background removal, large exports) run without freezing the window, and carry
  a cancel path. **[cited]** same *Loading* guidance + NN/g response-times.
- **A standard shortcut is never silently inert.** Wired standard shortcuts
  (Save, Select All, zoom, cancel) always either act or are visibly disabled —
  never a no-op. **[cited]** Apple HIG *The menu bar* / *Keyboards*.

## Reference rig + corpus

The latency budgets below (B1–B4) are only reproducible against a *fixed*
machine and a *fixed* set of input files — otherwise two reviewers measuring on
two laptops with two different PDFs reach opposite pass/fail verdicts on the
same number, and the gate swings arbitrarily on unstated variables. Two things
must be ratified before any latency budget can bind:

- **A named reference machine.** One spec (CPU / RAM / OS) that every latency
  measurement is taken on. A number measured on a fast dev workstation is not
  comparable to the same interaction on the "machine that is not new" this
  file's frame invokes. No reference machine has been named yet.
- **A checked-in reference-document corpus.** A small, named set of input files
  committed to the repo (e.g. `docs/perf/corpus/` — a named 1-page form PDF, a
  named 20-page PDF, a named 12 MP photo) that every measurement must use.
  "Typical document / typical PDF / typical hardware" is not a measurable input
  until it points at a specific file on a specific rig. No corpus exists yet.
  Any budget quoting a **median** must also state its sample count (e.g.
  "median of 20 runs"), or the median is not reproducible.

Until the owner ratifies a reference rig **and** a corpus, the latency budgets
B1–B4 are **advisory** — targets to measure against, not merge/release gates —
and are marked `[advisory until reference rig + corpus ratified]` in the table
below. **B5 and B6 stay binding:** they are wall-clock user-perception numbers
(time to first feedback, time to cancel) that hold on any machine and do not
depend on the corpus.

## How the latency budgets are verified

The latency budgets (B1-B6) are verified by **agent-run local measurement on the
reference corpus plus a reviewer check** — **never** a CI wall-clock assertion.
CI is deliberately *not* a timing gate: runner wall-time is too variable to
yield a stable pass/fail oracle. (This held on the old self-hosted runners and
holds just as much on the GitHub-hosted ones they moved to on 2026-08-05 — a
shared-tenancy hosted VM is if anything *more* variable, and two offscreen
tests already fail only under CPU contention; see
`docs/backlog/2026-08-03-load-sensitive-offscreen-test-races.md`.) So:

- **Agent-measured locally.** When a PR plausibly moves a latency budget, the
  agent measures the affected row on the reference corpus (once a rig + corpus
  is ratified) and reports the number in the PR body.
- **Review-checked.** A reviewer confirms the reported measurement and method;
  this is the enforcement mechanism, in the same spirit as the review-only
  enforcement of the no-telemetry constraint.
- **CI enforces only the structural invariants** in *Invariants (not just
  budgets)* — first-page render before full read, no UI block, no inert
  shortcut — which are corpus-independent and need no wall-clock timing. CI
  never asserts a latency number.

## Budget table

Every row: the budget value, the cited or owner-set rationale, and how it is
measured. "Measured" means the method a PR uses to produce the evidence gate G9
/ G1 asks for.

| # | What | Budget | Basis | How measured |
|---|---|---|---|---|
| B1 | **Click / control acknowledgement** | Visual feedback within **50 ms** of any button/control press **[advisory until reference rig + corpus ratified]** | **[cited]** asktog *First Principles*: "Acknowledge all button clicks … within 50 milliseconds" — https://asktog.com/atc/principles-of-interaction-design/ | Instrument the press→first-paint path, or high-speed capture; feedback frame within 50 ms |
| B2 | **Direct interactions (zoom step, page flip, pan, toolbar toggle)** | Complete within **~100 ms**; always well under **1 s** **[advisory until reference rig + corpus ratified]** | **[cited]** NN/g 0.1 s instantaneous / 1 s flow limits — https://www.nngroup.com/articles/response-times-3-important-limits/ | Frame-time / input-latency measurement on a typical document; median under 100 ms, no interaction over 1 s |
| B3 | **Scroll responsiveness & scroll-step size** | Scrolling stays smooth (no visible jank); a scroll step advances a predictable, consistent amount and paints within the B2 budget **[advisory until reference rig + corpus ratified]** | **[cited]** NN/g flow limit + asktog latency reduction; DESIGN §2.3 "Scroll, zoom, and pan are instant and smooth — jank at this layer destroys trust" | Scroll a typical multi-page PDF; measure per-step paint time (within B2) and confirm a fixed, predictable step distance |
| B4 | **Launch → first page visible (typical PDF)** | Target **≤ 1 s** on typical hardware for a typical file; hard ceiling well before the 10 s attention limit **[advisory until reference rig + corpus ratified]** | **[owner-intuition]** frame value, to be ratified; the 1 s flow limit is **[cited]** (NN/g) as the *reason* the target is ~1 s | Cold-launch the built app with a representative PDF on a reference machine; wall-clock from launch to first-page paint; record the machine spec |
| B5 | **Progress-indicator latency (when a spinner/percent-bar appears)** | **< 1 s:** no looped animation. **~2–10 s:** indeterminate spinner. **≥ 10 s:** percent-done bar. Immediate feedback the moment the action starts. One style per run — never switch spinner↔bar mid-task | **[cited]** NN/g *Progress Indicators* — https://www.nngroup.com/articles/progress-indicators/ ; Apple HIG *Progress indicators* (no spinner↔bar switch) — https://developer.apple.com/design/human-interface-guidelines/progress-indicators | For each long operation, time from start to first feedback (<1 s) and confirm the indicator style matches the operation's duration band |
| B6 | **Cancel responsiveness** | A cancel affordance is present the entire time a progress indicator is shown; pressing it (⌘. / Esc) aborts and returns the pre-operation state within **~1 s**, with no partial write | **[cited]** NN/g (cancel past 10 s) + Apple HIG *Progress indicators* (let people halt, warn on consequence); asktog cancel-by-10 s band | UAT: start a long op, cancel, assert pre-op state restored and no partial output, within ~1 s |
| B7 | **Resident memory envelope (typical file)** | **[owner-intuition, to be ratified]** a modest RSS ceiling for a typical document/image, excluding downloaded ML weights and the active ML inference working set | **[owner-intuition]** frugality ethos (PHILOSOPHY → *Frugal by construction*); no external number — this is the ethos made measurable | Measure peak RSS opening/viewing a representative file with ML idle; ratify a ceiling against the observed baseline |
| B8 | **Binary-size envelope** | **[owner-intuition, to be ratified]** a minimal shipped-binary size, measured **without** downloaded ML weights (the standing exception) | **[owner-intuition]** frugality ethos; ONNX weights are excluded by policy because they download on first use | Measure the packaged binary/installer size per platform, weights excluded; ratify a ceiling and flag any dependency that moves it |

## What to do with this file

- **Before ratification:** treat **B5/B6** as binding (corpus-independent,
  research-grounded wall-clock numbers) and everything else as a target to
  measure against, not yet a gate — **B1–B4** are advisory until a reference rig
  + corpus is ratified, and **B7/B8** until the owner sets a number. Gate G9 in
  AGENTS.md is advisory until **B7/B8** are ratified.
- **Ratification:** the owner (a) ratifies a reference rig + corpus (see
  *Reference rig + corpus*) — which is what lets the latency budgets B1–B4 bind
  — and (b) reviews B7, B8 against real measurements on that named rig,
  replacing "[owner-intuition, to be ratified]" / "[advisory until reference rig
  + corpus ratified]" with accepted numbers and updating this file's status
  header. That ratification is an owner-level decision (PHILOSOPHY → the owner
  is escalation-only) and may be recorded as a decision record if it proves
  contentious.
- **When a PR plausibly moves a budget** (new dependency, bundled asset, a
  change to the load path), it reports the measured number for the affected row
  in the PR body (gate G1 threshold + gate G9 evidence).
