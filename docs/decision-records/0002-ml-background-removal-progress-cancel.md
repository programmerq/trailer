# 0002 — ML background removal: progress, cancel, and the missing-model prompt

- **Status:** accepted
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-09
- **Date accepted / superseded:** 2026-07-12 (arbiter verdict)

## Context

Background removal (U²-Net, in `src/ml/`) is a local ML operation that takes
long enough to need feedback and can be started when the model weights are not
yet on disk. Two behaviours need to be settled: (1) how progress and cancel are
presented while the operation runs, and (2) what the control does when the ML
model isn't present.

The **missing-model** case is governed by PHILOSOPHY → *No lying controls*: a
control for a feature whose model isn't downloaded must be **disabled with a
tooltip** that says why and offers the path to download it — it must **not**
silently degrade to a nearest-equivalent (e.g. a crude thresholding cutout)
presented as the requested background removal. That would be exactly the banned
substitution: the user asks for X, gets almost-X, and believes they got X.

Progress and cancel are governed by the latency research and by the platform
progress-indicator guidance:

- NN/g response-time limits: under 0.1s feels instantaneous; under 1.0s keeps
  flow; beyond 10s the user loses attention and needs a percent-done indicator
  and, ideally, a cancel
  (https://www.nngroup.com/articles/response-times-3-important-limits/).
- NN/g progress indicators: **<1s** show no looped animation; **~2–10s** a
  spinner/indeterminate indicator; **≥10s** a percent-done bar; a determinate
  bar is worth ~3× the patience of none
  (https://www.nngroup.com/articles/progress-indicators/).
- Apple progress indicators: prefer determinate when you can report progress;
  keep it moving; **do not switch spinner↔bar mid-task**; let people cancel and
  warn if cancel has a consequence
  (https://developer.apple.com/design/human-interface-guidelines/progress-indicators).
- Trailer's own idiom (PHILOSOPHY → *Prefer the document over the dialog*): a
  long-running computation surfaces as a status-bar indicator, not a modal.

## Options

- **Progress placement:** (A) status-bar determinate indicator on the document
  surface, per Trailer's dialog→in-place idiom; vs (B) an overlay chip on the
  image being processed.
- **Indicator style by duration:** commit to the NN/g bands — no animation
  under ~1s, indeterminate spinner ~2–10s, percent-done bar ≥10s — and pick one
  style up front so it never switches mid-run (Apple rule).
- **Cancel:** always present once the operation is shown; ⌘. / Esc cancels;
  cancel restores the pre-operation image with no partial write.
- **Missing model:** the control is disabled + tooltip ("Download the
  background-removal model to use this — Settings → Background removal"); the
  first *deliberate* use offers the one-time consented download (existing model
  download consent flow), never a silent substitute cutout.

## Personas debate

- **Office non-technical user:** Needs to know the app is working, not frozen,
  and needs a way out if it's taking too long. A frozen-looking window during a
  multi-second inference reads as a crash.
- **Older careful user:** Distrusts anything that starts a large download
  without asking. The missing-model path must ask first, in plain language,
  and must be cancellable.
- **Power migrator:** Expects ⌘. / Esc to cancel and expects the original image
  back untouched on cancel.
- **Occasional user:** Won't remember whether the model is installed; the
  disabled-control tooltip is what tells them why the feature is greyed out and
  what to do.

## Admissible objections

- **Office user, no feedback under load:** if inference runs >1s with no
  indicator, the user believes the app hung and force-quits mid-operation — a
  visibility-of-system-status failure with data-loss risk.
- **Older careful user, silent download:** if a missing model triggers a
  background download without consent, this violates the model-download consent
  rule and the user's trust; concrete failure at the "I pressed the button once
  to see what it did" step.
- **Any user, silent substitution:** if a missing model silently falls back to
  a low-quality thresholding cutout labelled as background removal, the user
  ships a bad cutout believing it is the real feature — the exact
  no-lying-controls failure.

### Rejected as naked preference

- "A modal progress dialog looks more finished." — rejected: names no failure,
  and contradicts the in-place idiom without a concrete user problem.

## Checkable threshold this record would establish

- The operation shows feedback within the response-time budget in
  [`../performance-budgets.md`](../performance-budgets.md): visible progress
  within 1s of start; indeterminate spinner for the ~2–10s band; percent-done
  for ≥10s; a cancel affordance present the entire time the indicator is shown;
  cancel restores the pre-operation image with no partial write. One indicator
  style per run (no spinner↔bar switch).
- Missing-model: the control is `setEnabled(false)` with a tooltip naming the
  download path, and no code path substitutes a different algorithm for the
  requested removal.
- Acceptance evidence: UAT cases for the progress/cancel/restore behaviour and
  the disabled-control tooltip, plus screenshots of the running/cancelled/
  disabled states (gate G2), checked against the budget rows.

## Arbiter verdict + rationale

**Accepted 2026-07-12** by the arbiter role for this record, after a four-persona adversarial review (office non-technical, older careful, power migrator, occasional). Owner veto not invoked. Personas raised concrete objections; admissible ones are folded into the spec below, and two are recorded as rejected-with-reason.

This record governs feedback for all MlScheduler operations (background removal and OCR). All feedback lives in the status bar — never a modal (upholds CONVENTIONS #12 and PHILOSOPHY "a popup is a last resort").

### Accepted spec

**1. Progress presentation** (status bar; one style per run; determinate when the count is known)
- Any ML op still running after a reveal delay of ~1s surfaces a status-bar progress widget. Ops that finish under ~1s never reveal it — honours B5 "< 1 s: no looped animation" and avoids flicker. The click's immediate feedback is the op starting instantly; the animated/quantified affordance appears only if the op outlives the delay.
- Style is fixed at submit by whether total work is known, and never switches (B5 "one style per run, never switch spinner↔bar"):
  - Known unit count ≥ 2 (OCR batch of N pages): determinate — an "N / M pages" counter with a proportional bar. The counter is the primary always-honest signal; the bar fill maps to pages-done/total and is labelled by the counter so it cannot be misread as elapsed time. No time-remaining ETA (would lie on heterogeneous pages — power-migrator A3).
  - Unknown-length single op (background removal; single-page OCR): indeterminate spinner. Past ~10s, append elapsed-time text ("Removing background… 18s") as reassurance — not a style switch, stays indeterminate (older-careful C).
- Determinate is preferred whenever the unit count is known (Apple HIG); B5's "2–10 s spinner" band governs unknown-length ops only.

**2. Cancel** (present throughout; ~1s; no partial write)
- A Cancel ✕ control is part of the progress widget and present the entire time it shows (B6).
- Esc / ⌘. also cancel, but scoped: they cancel only the running explicit user-initiated (UserAction) op, and only after higher-priority transient UI (find bar, popovers) has had the keystroke. Ambient auto-OCR (VisiblePage/Prefetch priority) is never cancellable by a bare keystroke — removes the accidental-loss trap (power-migrator A4, older-careful B).
- Cancel takes effect within ~1s and shows a brief terminal status message — "Text recognition cancelled — no changes saved" / "Background removal cancelled — image unchanged" — before returning to idle. The message is the positive confirmation careful/occasional users need; idle = message-then-hide, not silent disappearance (older-careful A, office O6, occasional).
- No partial write, at the correct granularity:
  - Background removal (atomic single-image op): cancel restores the pre-op image; nothing written.
  - OCR (per-page atomic unit): the in-flight page and all not-yet-started pages contribute no text — any partial blocks from the interrupted page are discarded, never persist a half-recognised page. Pages fully completed before the cancel retain their complete, correct text; discarding them would be a capability regression vs peer tools and is not what B6 "no partial write" targets (power-migrator A1). The B6 language applies literally to background removal; for OCR the atomic unit is the page.

**3. Missing model**
- Explicit menu action (Recognize Text / Remove Background): remains disabled + tooltip naming the benefit and the one-time download path, in benefit language, no "model"/"OCR" jargon — e.g. "Text recognition needs a one-time language download (Settings → Manage ML models)". Upholds PHILOSOPHY "no lying controls" and "never a popup to explain why a feature isn't available". Rejected alternative: enabling the item to prompt-download on click (office O3, occasional B) — reverses the owner-ratified disabled+tooltip anti-lying pattern; discoverability is instead answered by the in-context affordance below. Reopen only with owner sign-off.
- Auto/background OCR (replaces the silent no-op at OcrController.cpp:207): when auto-OCR would run on the visible document but the language model is not installed, surface a non-modal in-context status-bar affordance phrased as passive app status about the document, benefit-first — e.g. "This document's text isn't searchable — install language pack to recognise it." It is state-driven and persistent (shown whenever the visible doc would auto-OCR and the model is missing; re-derived on document/page change — not a fire-once toast lost if missed — office O5) and also surfaces on a select-text-on-un-OCR'd-scan intent. Clicking it enters the existing one-time-consent download flow (the sanctioned download popup, PHILOSOPHY). Never auto-downloads without consent, never a modal to explain unavailability, never a silent no-op.

**4. Concurrency** — the MlScheduler runs one task at a time on a single worker thread, so ops never execute simultaneously; the progress widget reflects the running UserAction op and other submissions queue. Esc/⌘./✕ act on the running foreground op. A multi-indicator stack is unneeded and out of scope (power-migrator A2 acknowledged, mooted by single-worker execution).

**5. No substitution** (unchanged) — no path substitutes a different algorithm for a missing model; a failed/empty result may be dropped for retry, which is not substitution.

### Persona objections — disposition
- ACCEPTED into spec: terminal cancel/completion confirmation message; scoped Esc/⌘. (never kills ambient auto-OCR; ✕ is primary); elapsed-time text on long indeterminate spinners; benefit-language (drop "model"/"OCR" jargon); auto-OCR hint phrased as passive doc-status and state-driven/persistent, not fire-once; per-page OCR commit (keep completed pages, discard only the interrupted page); determinate keyed to an honest "N/M pages" counter (no ETA).
- REJECTED with reason: (a) enabling the disabled explicit menu item to prompt-download on click — reverses owner-ratified no-lying disabled+tooltip pattern; in-context hint covers discoverability. (b) busy/wait mouse cursor over content during an op (office O1) — the UI-never-blocks invariant makes a wait cursor a lie; mitigated instead by a clearer, briefly-emphasised status-bar widget within the ratified idiom. Moving feedback into content/overlay or a modal is foreclosed by CONVENTIONS #12 + PHILOSOPHY.

### Gates (checkable thresholds, declared before implementation)
- G1 determinate OCR progress: OCR batch of N≥2 pages → widget enters Determinate(total=N) with monotonically non-decreasing done reaching N. UAT counter/state assertion + grab() of running state.
- G2 reveal delay honours <1s: sub-threshold stub op never reveals the widget; over-threshold reveals. Deterministic via controllable stub duration + timer threshold (no wall-time).
- G3 cancel semantics: cancel mid-run → SelectableTextStore has text only for pages completed before cancel, none for in-flight/not-started, no half-recognised page; terminal "cancelled — no changes saved" message then idle. Background removal: post-cancel image bytes == pre-op. grab() of cancelled/idle.
- G4 cancel present + scoped keys: Cancel ✕ present/enabled the whole time the widget shows; Esc cancels the running UserAction op only, and a bare Esc does not cancel an ambient auto-OCR-only state.
- G5 auto-OCR missing model: model absent + auto-OCR enabled on supported doc → in-context affordance becomes visible, NO QDialog/modal spawned, clicking invokes the existing download-consent entry point; hidden again once model present. grab() of affordance.
- G6 explicit menu regression: model absent → Recognize Text action disabled with a benefit-worded tooltip naming the download path (no "model" jargon token).
- Evidence (not a gate): local wall-clock for a real ≥10-page OCR run and a background-removal run reported in the PR (B5/B6), per trailer-perf-measurement-ruling.

### Reopen criteria
A measured case where the "N/M pages" counter or the reveal-delay bands mislead a real user, or where per-page commit on cancel produces a misleading "looks-done" document, plus owner sign-off.

## Evidence required to reopen

Once accepted: a measured case where the chosen indicator bands mislead a user
about progress or completion, or a flow where the disabled-control path blocks a
legitimate use, plus owner sign-off.
