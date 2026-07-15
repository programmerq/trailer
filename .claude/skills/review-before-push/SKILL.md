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
- **(2) HIG-polish critic** — platform-native feel and the UX gates **G2-G5**:
  screenshots of every affected state (G2), no lying controls / disabled +
  tooltip (G3), platform-native shape with no feature dropped (G4), correct
  empty state (G5). Flags anything that would make a persona hesitate or that
  reads as non-native.
- **(3) Frugality auditor** — scope creep, unrequested features, new
  dependencies or binary/asset bloat, and the **G9** frugality budget
  (binary size + RSS envelopes in `docs/performance-budgets.md`). Asks "does
  this change need to be this big, and does it move a budget row?"

**How many to run (minimum):**
- **Code change →** persona **(1) + (3)** minimum.
- **UI / user-visible change →** add persona **(2)** (so 1 + 2 + 3).
- Docs-only change → one lighter pass is enough.

### 3. Collect and disposition EVERY finding
Gather the passes and analyze them. Each finding gets exactly one disposition:

- **fix** — do it now, in this change, before pushing.
- **justify** — record inline (PR body / commit message) why it is acceptable
  as-is. State the reason; don't just wave it off.
- **defer-with-Decision-Record** — open or append a record in
  `docs/decision-records/` (copy `TEMPLATE.md`, next free number) capturing
  the deferral and its rationale.

### 4. Gate
Only after **every** finding is dispositioned (fix / justify / defer) may you
`git push` or open the PR. After pushing/opening, proceed as normal.

## Checklist (copyable)

- [ ] Relevant tests written for the change and passing (regression tests failed against old code first). ← Step 0, do before everything
- [ ] Identified push base; computed `git diff <base>..HEAD`.
- [ ] Ran reviewer #1 (correctness skeptic).
- [ ] Ran reviewer #3 (frugality auditor).      ← code change minimum
- [ ] Ran reviewer #2 (HIG-polish critic).      ← required if UI/user-visible
- [ ] Every finding dispositioned: fix / justify / defer-with-Decision-Record.
- [ ] All "fix" items applied; "justify" reasons written down; "defer" records opened.
- [ ] ONLY NOW: push / open PR.
