---
name: trailer-review-before-push-policy
description: Owner's standing policy (2026-07-10, refined 2026-07-14) — tests written first, then 1-2 local review-agent passes (variant personas) BEFORE any push or PR; address findings; then push and open the per-item PR ready-for-review (per-item PRs normal since self-hosted CI)
metadata:
  type: feedback
---

# Tests + local review BEFORE pushing or opening a PR

Owner directive, 2026-07-10, verbatim: "if you want to push or open a PR, don't. First, do a code review using an agent (or two with a variant persona for perspective). This should take the place of the items that could trigger on a github push, PR, etc. But instead of spending github runner minutes, it's much more efficient to do a review round (or two) here *before* pushing or opening a PR. After you receive the review passes, analyze, and adjust (if necessary), then you can go ahead and do a push/pr. The goal is to reduce the noise/volume/back-and-forth of pushing to github with limited github action worker minutes in a month."

**Why:** GitHub Actions minutes are limited and CI runs on the self-hosted trailer-k8s runners; local agent review rounds are cheaper and catch issues before they burn runner time or create PR comment churn.

**How to apply (standing, project-wide; current end-state):**
1. Finish the work item, then spawn 1-2 local review agents over the diff (`git diff <base>..HEAD`). Give the second reviewer a contrasting persona (e.g. skeptical maintainer vs. meticulous line-reviewer) for coverage.
2. Analyze the review passes and adjust the change if warranted.
3. Only THEN `git push -u origin <branch>`. Pushing the branch is safe and free: ci.yml triggers ONLY on push-to-main and PR-to-main, so feature-branch pushes cost zero runner minutes (see [[trailer-ci-on-k8s-runners]]).
4. Open the per-change PR ready-for-review once steps 1–3 (tests, review, adjustments) are done — per-item PRs are normal practice since self-hosted CI removed the minutes cost (owner-driven since 2026-07-11, PRs #43–#53). Historical note: during the 2026-07-09/10 hosted-minutes crunch, PRs were held and batched (see [[trailer-integration-batch-pr40]]); that constraint is retired. Merges to main remain the owner's click or his explicit in-session word.

Applies to docs-only changes too (lighter review), not just code.

Complements [[trailer-requirements-summary]] (adversarial second pass) — this formalizes it as the trigger-replacement for CI churn. Related: [[trailer-inflight-work-persistence]] (push branches to survive containers), [[trailer-ci-on-k8s-runners]], [[trailer-perf-measurement-ruling]].

## Refinement (owner, 2026-07-14, verbatim)

"We can't rely on external reviewers each time. ... a project policy to do a self review _before_ opening a PR is better. So if an agent feels like it's time to open a PR, it isn't. First, it should make sure it has written relevant tests for the change. Next, it should spin up a reviewer agent that does a code review on the proposed changes. It can even do a pass to address those changes if they are clear and unambiguous. Then it can open the PR. We may occasionally have a copilot/cursor/claude reviewer chime in, but it won't be guaranteed on every PR/change/update."

**Net sequence per change:** (1) relevant tests written for the change — verify before anything else; (2) reviewer agent(s) review the diff; (3) address clear/unambiguous findings (ambiguous ones get dispositioned fix/justify/defer per the skill); (4) THEN open the PR. External reviewers (Copilot etc.) are occasional bonus signal, never a relied-upon gate. NOTE: the in-repo skill .claude/skills/review-before-push/SKILL.md should be amended with this refinement by the next session that touches the repo — until then this memory is canonical.

**No hollow PRs (owner, 2026-07-15):** "Code or it doesn't happen." Bookkeeping-only PRs (backlog-file deletions, status shuffling) are not worth opening — such changes ride the nearest code PR as a rider commit with evidence citations. Companion rule to the same-day inline-evidence refinement in [[trailer-ux-evidence-ruling]]: a PR's diff should carry the substance (code + rendered evidence), not narration about substance elsewhere.
