# 0002 — ML background removal: progress, cancel, and the missing-model prompt

- **Status:** proposed
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-09
- **Date accepted / superseded:** —

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

<Open — status is proposed.>

## Evidence required to reopen

Once accepted: a measured case where the chosen indicator bands mislead a user
about progress or completion, or a flow where the disabled-control path blocks a
legitimate use, plus owner sign-off.
