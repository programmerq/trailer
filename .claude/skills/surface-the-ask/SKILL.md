---
name: surface-the-ask
description: When reporting work (PR body, session summary), state the one thing that blocks ready-to-merge up front — or say nothing blocks — so the owner never has to dig for it; triage first, and escalate genuine forks as socratic, one-word-answerable questions with a stated default. Also rules PR granularity: one change, one PR.
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

The templates below are written for a PR body, but the rule is medium-agnostic.
A **session or report summary leads with the same triaged ask** — the
WHAT/CONTEXT/IMPACT of Step 2, or an explicit "No ask" — so the owner reads what
you need from them first, whether or not a PR exists.

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

The owner later drew the line on *what deserves a PR at all* (#74, verbatim):

> "It's too much to have one PR for a doc/proposal. ... Flip this to a draft and
> then ask questions if you're not confident enough to implement a plan. This is
> not ready to merge with no code changes, no tests, no behaviors. It's fine to
> ask for a 👍 before you implement things, but you haven't put forward anything
> to review."

Encoded as **Step 4 — the reviewable-deliverable gate** below: it runs *before*
the ready-to-merge convention, because it decides whether a PR should exist.

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
  "Blocks you" means the **agent's forward progress** — you always have a default
  to proceed on. It does *not* mean the owner's merge gate: a default can still be
  "holds — don't merge unverified" (see #77), which holds the *merge* without
  stranding the *agent*.
- Where proceeding on the default is safe/reversible, say **"silence =
  default"** explicitly.
- Answerable with one word (the option name, or yes/no). If it isn't, split it.

## Step 4 — The reviewable-deliverable gate (should a PR exist at all?)

Run this **before** you open a PR. It decides whether a PR belongs here, or
whether you should just *ask*. The one-liner to remember:

> **Clear work → open ready-for-review WITH the code. Proposal / direction-needed
> → ask (a question or a 👍-request), don't open a docs-only PR.**

The rule in full:

1. **A PR is a reviewable deliverable only if it carries IMPLEMENTATION — code,
   tests, or a behavior change** (a skill or runbook the team will follow counts,
   even when its diff is text-only). A proposal-only, decision-record-only, or
   plan-only change is **not** a ready-for-review deliverable: there is nothing
   to review yet.
2. **A decision record merges WITH its implementing PR — never alone.** Same
   rule as backlog closures: *code or it doesn't happen.* A DR by itself is a
   proposal about future work, not a deliverable.
3. **Pre-implementation direction is requested as QUESTIONS, not a PR.** Ask
   socratic, one-word-answerable questions with stated defaults (Steps 2–3), or
   make a single 👍-request on the plan — and make it **inline** (in the
   conversation / your report), *not* as a docs-only PR.
4. **If a proposal PR exists at all, it stays DRAFT** until implementation lands
   in it. Don't mark it ready-for-review with nothing to review.
5. **The tension, resolved:** the owner *also* dislikes PRs left lingering in
   draft for **clear** work. So draft is only for a proposal awaiting its
   implementation — never a parking spot for finished, mergeable work. Clear
   work is never a draft; a proposal is never a ready-for-review docs-only PR.

**Don't let this misfire on its own kind.** "Implementation" is anything that
**changes behavior when merged** — a bug fix, a feature, a config change, *and an
agent skill or a runbook the team will follow*, even when the diff is only
text/markdown. A docs-only diff that *is itself the requested deliverable* (this
skill; a runbook) is **clear work** — open it ready-for-review with the content.
The gate targets **proposals ABOUT future work** (a decision record or plan with
no accompanying implementation), not every text diff.

**Decision path (the whole skill in one line):** triage the ask (Step 1) →
*is there implementation?* **no** → ask inline / draft-until-impl, don't open a
docs-only PR → **yes** → *is this one change or two?* (Step 5 — a stacked base
means one) → *is there a blocking ask?* **yes** → Case A → **no** → Case B, open
ready-for-review.

## Step 5 — The PR-granularity gate (one PR, or two?)

Step 4 decided that a PR belongs here. This step decides **how many** — same
diff, same moment.

> **Split only if each piece would still be worth opening if the other never
> existed.**

- **A blocking owner decision is not a reason to split.** When one part of a
  change is clean and another carries an owner decision, **the clean part does
  not get its own PR**: one PR, the ask stated per Step 6 Case A, default
  `hold` — a default that holds the *merge* without stranding the *agent*
  (Step 3).
- **A stacked base is the tell.** At PR-open time you type a `base:`; if it
  isn't `main`, stop — you are splitting one change. The same tell fires
  earlier, at `git checkout -b` while standing on the first branch.
- **Legitimate splits — and you state the justification in the PR body**,
  naming the fact that makes it true: *genuinely independent work* (give both
  halves' titles, neither mentioning the other); *too large to review in one
  sitting* (say how large); *a piece you would revert alone* (say what would
  trigger that). An exception you don't write down is not an exception.
- **Don't over-correct.** This gate bars *fragmenting one change*; it does not
  mandate *bundling unrelated ones* — independent items stay separate PRs.

## Step 6 — The "ready-to-merge ask" PR convention

Every PR body carries, **as its first section**, exactly one of these two.

> **Any user-visible UI change (both cases):** the PR is only reviewable once
> its inline **before/after** screenshot pair is in the PR body (committed under
> `docs/uat/images/`, SHA-pinned raw URLs per AGENTS.md G2) — an after-only or
> comment-only shot means the UI PR is **not** ready-for-review. This is part of
> the reviewable-deliverable gate (Step 4) for UI work, not a separate ask.

### Case A — something blocks merge

Use when merge needs an owner decision: owner verification the CI can't do, a
product call embedded in the change, or a decision record awaiting acceptance.

```markdown
## Ready-to-merge ask
**Blocks merge:** <one line — what must happen before this can merge>
- **What / Context / Impact:** <the WHAT/CONTEXT/IMPACT from Step 2 — the
  decision, self-contained context, and consequence each way>
- **Default if no reply:** <what I'll do on silence, or "holds until you answer">
```

If it's a fork, drop the Step 3 Option|Meaning|Impact|Default table in here.

#### Owner-verification asks post a checklist COMMENT

When the blocking ask is **manual / on-device / owner-run verification** (the
verification flavor of Case A — see the #77 example below), don't bury the steps
in the PR body. **Post a PR comment containing a Markdown `- [ ]` checklist of
the exact manual steps** for the owner to run against his local dev build — he
ticks each box off as he goes. Each box is **one exact, self-contained step:
what to do plus what a pass looks like**, so he isn't reverse-engineering the
test. The PR body's Ready-to-merge ask still **names the verification as the ask
and points to the checklist comment** — the checklist lives in the comment (so
he can tick it), not in the body. Precedent: PR #59's 12-box "Manual Testing"
checklist; the owner standardized this on PR #72 ("Comment here with a `[ ]`
checklist for manual testing with my local dev build. … This is a good thing to
do for any manual testing requests going forward.").

This rides on a PR that **already carries implementation** — a manual-testing
checklist is not a substitute for code and does not turn a proposal into a
reviewable deliverable (Step 4 still gates).

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
- [ ] Reviewable-deliverable gate (Step 4): does this PR carry implementation —
      code / tests / a behavior change (a skill or runbook counts)? If it's a
      proposal / DR / plan **only** → ask inline or keep it draft; don't open a
      docs-only PR. A DR merges with its implementing PR, not alone.
- [ ] PR-granularity gate (Step 5): each PR I'm opening would still be worth
      opening if the other never existed, and **no PR's `base` is another agent
      branch**. A blocking owner decision is not a reason to split off the clean
      half — one PR, Case A, default `hold`. Any split **states its
      justification in the PR body**.
- [ ] PR body's **first section** is `## Ready-to-merge ask` — Case A (blocks) or
      Case B (no ask).
- [ ] If blocking: states the WHAT/CONTEXT/IMPACT from Step 2.
- [ ] Genuine fork framed socratic: one-word-answerable, table form, **default
      stated** so silence is safe.
- [ ] No ask buried mid-body; no vague "awaiting review".
