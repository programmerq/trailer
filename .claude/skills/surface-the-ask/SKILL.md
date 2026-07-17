---
name: surface-the-ask
description: When reporting work (PR body, session summary), state the one thing that blocks ready-to-merge up front — or say nothing blocks — so the owner never has to dig for it; triage first, and escalate genuine forks as socratic, one-word-answerable questions with a stated default.
---

# Surface the ask

Use this whenever you hand work back to the owner (programmerq) — opening or
updating a PR, or writing a session/report summary. Its job is the opposite of
hiding the ball: make the **one thing that decides ready-to-merge** the first
thing the owner reads. The owner should never have to comment "what is the
specific ask here?" to find out what you need from them.

This is the report-time / PR-time companion to
[`decision-brief`](../decision-brief/SKILL.md). **decision-brief** governs
questions you raise TO the owner **mid-work**; **surface-the-ask** governs how
you signal, at hand-off, **what (if anything) blocks the merge**. Same triage
discipline, different moment.

## Rationale (owner feedback)

The owner opened a PR (#68) that was a clean 2-line change but whose body never
said whether anything blocked merge, and asked, verbatim:

> "What is the specific ask here to allow ready-to-merge? This should be a skill
> to help agents signal that you need a human to review. Surface the ask, give
> context, and share impact for whatever decision set you suspect. If there's an
> unambiguous answer, don't wait for a human. If you need a human, use socratic
> questions to resolve ambiguity!"

Two standing rulings this encodes:
- **"Don't hang on me for clear direction."** Unambiguous answer ⇒ proceed,
  state your default in one line, don't wait. The owner is "doubtful that every
  question is really so ambiguously divorced from the objectives."
- When you *do* need a human, the ask must be **self-contained, impact-bearing,
  and one-word-answerable** — never a buried aside, never a vague "awaiting
  review".

## Step 1 — TRIAGE FIRST (is there actually an ask?)

Before you surface anything, test whether the decision is already made for you.
Check the candidate against the recorded objectives (same sources
`decision-brief` uses): recorded objectives / accepted records in
`docs/decision-records/` / conventions in `docs/CONVENTIONS.md` /
`AGENTS.md` gates G1–G9 / `PHILOSOPHY.md` / `DESIGN.md`.

- **If any of them derives the answer → there is NO ask.** Decide it, and state
  the chosen default in **one line** ("Kept 1:1 default per DESIGN §… ; owner may
  veto."). Do **not** escalate. Do **not** park the PR as a draft waiting on a
  human.
- **Only if no objective decides it** do you have a genuine ask → Step 2/3.

Bias hard toward "no ask". A PR with nothing blocking says so explicitly (see
the "No ask" template) — that is itself surfacing the ask.

## Step 2 — SURFACE THE ASK (when there is one)

Put it at the **top**, never mid-body, never as "awaiting review". The ask states
three things, self-contained so the owner can answer **without scrolling**:

1. **WHAT** — the specific decision being asked (one sentence).
2. **CONTEXT** — everything needed to answer it, inline (don't make them open
   files or reconstruct state).
3. **IMPACT** — the consequence of each option (the decision set): what happens
   if we go each way / if merged vs. not.

## Step 3 — SOCRATIC FORM (for genuine forks)

Frame a real decision as a **one-word-answerable** question with a **stated
default**, so silence is safe. Prefer a table:

| Option | Meaning | Impact | Default |
|---|---|---|---|
| A | … | … | ← silence = this |
| B | … | … | |

Rules:
- Every ask carries a **default you will take if the owner doesn't reply** —
  silence never blocks you (mirrors `decision-brief`'s default-if-no-reply).
- Where proceeding on the default is safe/reversible, say **"silence =
  default"** explicitly.
- Answerable with one word (the option name, or yes/no). If it isn't, split it.

## Step 4 — The "ready-to-merge ask" PR convention

Every PR body carries, **as its first section**, exactly one of these two.

### Case A — something blocks merge

Use when merge needs an owner decision: owner verification the CI can't do, a
product call embedded in the change, or a decision record awaiting acceptance.

```markdown
## Ready-to-merge ask
**Blocks merge:** <one line — what must happen before this can merge>
- **What:** <the specific decision/verification asked of you>
- **Context:** <inline, self-contained — no need to scroll or open files>
- **Impact:** <consequence each way / if merged vs not>
- **Default if no reply:** <what I'll do on silence, or "holds until you answer">
```

If it's a fork, drop the Step 3 Option|Meaning|Impact|Default table in here.

### Case B — nothing blocks

```markdown
## Ready-to-merge ask
**No ask — mergeable as-is.** <one line: why nothing blocks — e.g. reviewed
locally, CI green, no product call, defaults follow §X.> Merge remains your call
per the owner-gated-merge policy.
```

> Merges to `main` are always the owner's explicit call
> ([`review-before-push`](../review-before-push/SKILL.md)). "No ask" means
> *nothing blocks your decision* — it does not self-merge.

## Worked examples

### GOOD — blocking ask surfaced (from PR #76, oversized-capture product call)

> **Blocks merge (product call):** oversized captures (larger than the screen)
> currently open at **1:1 with scrollbars** in a screen-clamped window.
> **Context:** this matches our "default 1:1" spec, but macOS Preview instead
> *fits* oversized images — so it's a deliberate divergence, not a bug.
> **Impact:** keep 1:1 → pixel-exact, consistent with every other zoom, differs
> from Preview; switch to fit-when-larger → matches Preview, breaks the 1:1
> guarantee for this one case. **Default if no reply:** ship 1:1 (matches spec).

Names WHAT, CONTEXT, and IMPACT of each option, and defaults so silence ships.

### GOOD — verification ask the CI can't cover (from PR #77, TCC recheck)

> **Blocks merge (manual verification):** real-TCC screen-capture behavior can't
> run in CI. **Context + how to check:** `tccutil reset ScreenCapture
> org.trailer.Trailer`, then Take Screenshot and walk: (1) undetermined →
> explainer + OS prompt, no crosshair; (2) Deny → honest degrade, no dead-end;
> (3) reset-after-grant re-prompts. **Impact:** these are the paths the pure
> unit tests can't reach; a regression here is a silent dead-end. **Default:**
> holds for your pass — don't merge unverified.

### BAD — the motivating case (PR #68, ask absent)

The body had solid Before / After / How / Validation — and **no line saying
whether anything blocked merge**. Nothing was actually blocking (a 2-line
verbatim mirror of already-merged steps, CI green), but the body never said so,
so the owner had to stop and ask "what is the specific ask here?". The fix was
one line at the top: *"No ask — mergeable as-is: 2-line verbatim mirror of #61's
merged license-copy steps, reviewed locally, CI green. Merge is your call."*
The failure was not a wrong decision — it was **leaving the ask unstated**.

## Checklist (copyable)

- [ ] Triaged the candidate: is the answer already derived by an objective / ADR
      / convention / gate? If yes → no ask; decided it in one line.
- [ ] PR body's **first section** is `## Ready-to-merge ask` — Case A (blocks) or
      Case B (no ask).
- [ ] If blocking: states WHAT + CONTEXT (self-contained) + IMPACT of each option.
- [ ] Genuine fork framed socratic: one-word-answerable, table form, **default
      stated** so silence is safe.
- [ ] No ask buried mid-body; no vague "awaiting review".
