---
name: review-before-push
description: Mandatory pre-push/pre-PR gate — run 1-2 local reviewer agents with contrasting personas over the diff, disposition every finding, and only then push or open a PR to programmerq/trailer.
---

# Review before push

Run this **before every `git push` and every PR** to `programmerq/trailer` —
code and docs alike (docs get a lighter pass, but still a pass). It is the
local stand-in for the CI and review-bot checks that a GitHub push/PR would
otherwise trigger; doing it here first is what keeps self-hosted runner
minutes from being burned on the push → CI-fail → fix → re-push loop.

## The standing policy (verbatim)

> Standing policy for the entire Trailer project, set by the owner (programmerq)
> on 2026-07-10: whenever you are about to `git push` or open a PR, do NOT do it
> first. Instead run a local code review using a subagent — ideally two, with
> variant/contrasting personas for perspective — over the diff. This substitutes
> for the checks that would otherwise trigger on a GitHub push/PR (CI, review
> bots), performing them here first.
>
> **Why:** GitHub Actions runner minutes are limited per month, and CI now runs
> on the self-hosted trailer-k8s runners. Catching issues in a local review
> round (or two) avoids burning runner minutes and cuts the push -> CI-fail ->
> fix -> re-push back-and-forth noise/volume.

## 2026-07-14 refinement (verbatim)

The owner refined the policy on 2026-07-14. Quoted verbatim:

> "We can't rely on external reviewers each time. ... a project policy to do a
> self review _before_ opening a PR is better. So if an agent feels like it's
> time to open a PR, it isn't. First, it should make sure it has written
> relevant tests for the change. Next, it should spin up a reviewer agent that
> does a code review on the proposed changes. It can even do a pass to address
> those changes if they are clear and unambiguous. Then it can open the PR. We
> may occasionally have a copilot/cursor/claude reviewer chime in, but it won't
> be guaranteed on every PR/change/update."

**Net sequence this establishes, per change:** (1) **relevant tests written for
the change — verify this before anything else** (Step 0 below); (2) reviewer
agent(s) review the diff; (3) address clear/unambiguous findings (ambiguous
ones get dispositioned fix / justify / defer per Step 3); (4) THEN open the PR.
Two load-bearing rules from the refinement:

- **"If it's time to open a PR, it isn't."** Treat the urge to push/open as the
  cue to run Step 0 → Step 4 first, not as permission to skip them.
- **External reviewers (Copilot / Cursor / Claude bots) are occasional bonus
  signal, never a relied-upon gate.** The local tests + reviewer pass are the
  guarantee; a bot chiming in is not, and its absence is never a reason to hold.

**Current practice (since self-hosted CI):** push the per-item branch and open
its PR **ready-for-review** as normal — per-item PRs no longer cost hosted
minutes, and the 2026-07-09/10 batching constraint is retired. Merges to main
remain the owner's explicit call.

This applies to changes that carry **implementation** (code, tests, or a
behavior change — a skill or runbook counts). A **proposal / decision-record /
plan-only** change is *not* a ready-for-review deliverable: don't open a
docs-only PR for it — request direction inline or keep it draft until the
implementation lands, per
[`surface-the-ask`](../surface-the-ask/SKILL.md).

## Procedure

### 0. Write the change's tests FIRST
Before the review round — ideally before/alongside the implementation — make
sure the change carries **relevant tests** and that they pass (and, for a
regression/bug fix or a new invariant, that they *failed* against the old code
first). This is the owner's 2026-07-14 tests-first step: a change arriving at
the review gate without its tests is not ready, regardless of how clean the
diff looks. Structural/perf changes assert counts/ordering, not wall-clock
(see [[trailer-perf-measurement-ruling]]).

### 1. Stage the diff
Compute the review target: `git diff <base>..HEAD` (and `git diff` for any
unstaged work you intend to push). Know the base you will push against.

### 2. Run 1-2 reviewer agents, with variant personas
Spin up review subagents over the diff. Give each a **contrasting persona** so
two passes cover different failure classes. Pick from these three:

- **(1) Correctness skeptic** — logic errors, edge cases, race conditions,
  error/return-value handling, missing or weak tests, off-by-one, resource
  leaks, undo-stack correctness. Assume the code is wrong until the diff proves
  otherwise.
- **(2) HIG-polish critic** — platform-native feel and the UX gates
  **G2-G5 and G10**: screenshots of every affected state (G2), no lying
  controls / disabled + tooltip (G3), platform-native shape with no
  feature dropped (G4), correct empty state (G5), and — distinct from
  G3 — permanent chrome that only restates state the document already
  shows, or a control that changed on-screen position as a side effect of
  unrelated state (G10: deference and spatial constancy). Flags anything
  that would make a persona hesitate, reads as non-native, or moves
  furniture that shouldn't have moved.
- **(3) Frugality auditor** — scope creep, unrequested features, new
  dependencies or binary/asset bloat, and the **G9** frugality budget
  (binary size + RSS envelopes in `docs/performance-budgets.md`). Asks "does
  this change need to be this big, and does it move a budget row?"

**How many to run (minimum):**
- **Code change, no `src/ui/` diff →** persona **(1) + (3)** minimum.
- **Any diff touching `src/ui/`, or otherwise adding/reshaping on-screen
  chrome or control layout →** persona **(2) is MANDATORY**, in addition
  to (1) + (3) — so all three run. This is stronger than "add it if the
  change feels UI-ish": per AGENTS.md gate G10, a `src/ui/` diff that
  reaches Step 4 without a persona-(2) pass is not ready to push.
- Docs-only change → one lighter pass is enough.

**No subagent-spawning tool available? Run the persona yourself, and say
so.** Some sessions have no tool that can spin up a subagent. That does
not waive persona (2) for a `src/ui/` diff — it means running the
HIG-polish-critic pass yourself: read the diff adopting that persona's
lens deliberately (not as a mental aside while doing something else), and
disclose the substitution in the PR body — e.g. "Persona (2) run
in-session; no subagent-spawning tool was available this run." A
disclosed self-run pass satisfies this step; a silently skipped one does
not, and looks identical to negligence to the next reviewer.

### 3. Collect and disposition EVERY finding
Gather the passes and analyze them. Each finding gets exactly one disposition:

- **fix** — do it now, in this change, before pushing.
- **justify** — record inline (PR body / commit message) why it is acceptable
  as-is. State the reason; don't just wave it off.
- **defer-with-Decision-Record** — open or append a record in
  `docs/decision-records/` (copy `TEMPLATE.md` to a `YYYY-MM-DD-<slug>.md` file) capturing
  the deferral and its rationale.

### 4. Gate
Only after **every** finding is dispositioned (fix / justify / defer) may you
`git push` or open the PR. After pushing/opening, proceed as normal. The PR body
must lead with its ready-to-merge ask — either the specific thing blocking merge
or an explicit "No ask — mergeable as-is" line — per
[`surface-the-ask`](../surface-the-ask/SKILL.md).

### 5. Inline UAT evidence in the PR (2026-07-15 refinement)
For any UI / user-visible change, the G2 UAT evidence must be **inline** on the
PR page, not merely produced in-session or attached to chat — the merge decision
has to be evidence-visible (what the owner merges is what the owner saw). Commit
the **curated** before/after grab() captures under `docs/uat/images/` and
reference them inline in the PR body (e.g. a `raw.githubusercontent.com` URL
pinned to the commit SHA so the image renders on the PR). **Working/throwaway
captures stay gitignored** (`uat-screenshots/`, `docs/screenshots/`) — only the
curated, referenced images are committed.

## Checklist (copyable)

- [ ] Relevant tests written for the change and passing (regression tests failed against old code first). ← Step 0, do before everything
- [ ] Identified push base; computed `git diff <base>..HEAD`.
- [ ] Ran reviewer #1 (correctness skeptic).
- [ ] Ran reviewer #3 (frugality auditor).      ← code change minimum
- [ ] Ran reviewer #2 (HIG-polish critic) — MANDATORY for any `src/ui/` diff (self-run + disclosed in the PR body if no subagent-spawning tool was available).
- [ ] `src/ui/` diff: G10's two questions answered in the PR body (what was added to the permanent surface and why; whether any existing control's on-screen position changed).
- [ ] Every finding dispositioned: fix / justify / defer-with-Decision-Record.
- [ ] All "fix" items applied; "justify" reasons written down; "defer" records opened.
- [ ] UI change: curated UAT evidence committed under `docs/uat/images/` and referenced inline in the PR body (throwaway captures left gitignored).
- [ ] UI change: reviewer confirmed an inline **before/after** screenshot pair (same state in both shots) is present in the PR body before approving the push — an after-only or comment-only shot does not pass (AGENTS.md G2).
- [ ] PR body leads with its ready-to-merge ask (blocking ask, or "No ask — mergeable as-is") per [`surface-the-ask`](../surface-the-ask/SKILL.md).
- [ ] ONLY NOW: push / open PR.
