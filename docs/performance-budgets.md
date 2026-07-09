# Trailer — Performance Budgets

> **STATUS: PROPOSED — FOR OWNER RATIFICATION.**
> The externally-cited latency thresholds below are grounded in published
> research and are ready to adopt. The absolute budgets (launch time, memory
> envelope, binary size) are the maintainer's frugality intuition written down
> so they can be *measured against and ratified* — they are not yet accepted
> numbers. Each row is marked **[cited]** or **[owner-intuition]** accordingly.
> Once ratified, these budgets back gate G9 in [`../AGENTS.md`](../AGENTS.md).

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

These are pass/fail behaviours, not tunable numbers:

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

## Budget table

Every row: the budget value, the cited or owner-set rationale, and how it is
measured. "Measured" means the method a PR uses to produce the evidence gate G9
/ G1 asks for.

| # | What | Budget | Basis | How measured |
|---|---|---|---|---|
| B1 | **Click / control acknowledgement** | Visual feedback within **50 ms** of any button/control press | **[cited]** asktog *First Principles*: "Acknowledge all button clicks … within 50 milliseconds" — https://asktog.com/atc/principles-of-interaction-design/ | Instrument the press→first-paint path, or high-speed capture; feedback frame within 50 ms |
| B2 | **Direct interactions (zoom step, page flip, pan, toolbar toggle)** | Complete within **~100 ms**; always well under **1 s** | **[cited]** NN/g 0.1 s instantaneous / 1 s flow limits — https://www.nngroup.com/articles/response-times-3-important-limits/ | Frame-time / input-latency measurement on a typical document; median under 100 ms, no interaction over 1 s |
| B3 | **Scroll responsiveness & scroll-step size** | Scrolling stays smooth (no visible jank); a scroll step advances a predictable, consistent amount and paints within the B2 budget | **[cited]** NN/g flow limit + asktog latency reduction; DESIGN §2.3 "Scroll, zoom, and pan are instant and smooth — jank at this layer destroys trust" | Scroll a typical multi-page PDF; measure per-step paint time (within B2) and confirm a fixed, predictable step distance |
| B4 | **Launch → first page visible (typical PDF)** | Target **≤ 1 s** on typical hardware for a typical file; hard ceiling well before the 10 s attention limit | **[owner-intuition]** frame value, to be ratified; the 1 s flow limit is **[cited]** (NN/g) as the *reason* the target is ~1 s | Cold-launch the built app with a representative PDF on a reference machine; wall-clock from launch to first-page paint; record the machine spec |
| B5 | **Progress-indicator latency (when a spinner/percent-bar appears)** | **< 1 s:** no looped animation. **~2–10 s:** indeterminate spinner. **≥ 10 s:** percent-done bar. Immediate feedback the moment the action starts. One style per run — never switch spinner↔bar mid-task | **[cited]** NN/g *Progress Indicators* — https://www.nngroup.com/articles/progress-indicators/ ; Apple HIG *Progress indicators* (no spinner↔bar switch) — https://developer.apple.com/design/human-interface-guidelines/progress-indicators | For each long operation, time from start to first feedback (<1 s) and confirm the indicator style matches the operation's duration band |
| B6 | **Cancel responsiveness** | A cancel affordance is present the entire time a progress indicator is shown; pressing it (⌘. / Esc) aborts and returns the pre-operation state within **~1 s**, with no partial write | **[cited]** NN/g (cancel past 10 s) + Apple HIG *Progress indicators* (let people halt, warn on consequence); asktog cancel-by-10 s band | UAT: start a long op, cancel, assert pre-op state restored and no partial output, within ~1 s |
| B7 | **Resident memory envelope (typical file)** | **[owner-intuition, to be ratified]** a modest RSS ceiling for a typical document/image, excluding downloaded ML weights and the active ML inference working set | **[owner-intuition]** frugality ethos (PHILOSOPHY → *Frugal by construction*); no external number — this is the ethos made measurable | Measure peak RSS opening/viewing a representative file with ML idle; ratify a ceiling against the observed baseline |
| B8 | **Binary-size envelope** | **[owner-intuition, to be ratified]** a minimal shipped-binary size, measured **without** downloaded ML weights (the standing exception) | **[owner-intuition]** frugality ethos; ONNX weights are excluded by policy because they download on first use | Measure the packaged binary/installer size per platform, weights excluded; ratify a ceiling and flag any dependency that moves it |

## What to do with this file

- **Before ratification:** treat the **[cited]** rows as binding (they are
  research-grounded) and the **[owner-intuition]** rows as targets to measure
  against, not yet gates. Gate G9 in AGENTS.md is advisory until B4/B7/B8 are
  ratified.
- **Ratification:** the owner reviews B4, B7, B8 against real measurements on a
  named reference machine and replaces "[owner-intuition, to be ratified]" with
  accepted numbers, changing this file's status header. That ratification is an
  owner-level decision (PHILOSOPHY → the owner is escalation-only) and may be
  recorded as a decision record if it proves contentious.
- **When a PR plausibly moves a budget** (new dependency, bundled asset, a
  change to the load path), it reports the measured number for the affected row
  in the PR body (gate G1 threshold + gate G9 evidence).
