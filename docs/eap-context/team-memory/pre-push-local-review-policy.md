---
name: pre-push-local-review-policy
description: Standing policy — before any git push or PR to programmerq/trailer, run 1-2 local review-agent passes (variant personas) first; only push/PR after reviews pass and adjustments are made
metadata:
  type: feedback
---

# Review locally BEFORE pushing or opening a PR

Standing policy for the entire Trailer project, set by the owner (programmerq) on 2026-07-10: whenever you are about to `git push` or open a PR, do NOT do it first. Instead run a local code review using a subagent — ideally two, with variant/contrasting personas for perspective — over the diff. This substitutes for the checks that would otherwise trigger on a GitHub push/PR (CI, review bots), performing them here first.

**Why:** GitHub Actions runner minutes are limited per month, and CI now runs on the self-hosted trailer-k8s runners. Catching issues in a local review round (or two) avoids burning runner minutes and cuts the push -> CI-fail -> fix -> re-push back-and-forth noise/volume.

**How to apply:**
1. Before any push or PR, spin up 1-2 review agents over the diff (`git diff <base>..HEAD`). Give the second reviewer a contrasting persona (e.g. skeptical maintainer vs. meticulous line-reviewer) for coverage.
2. Collect the review passes, analyze them, and adjust the change if warranted.
3. Only THEN push / open the PR. After pushing/opening, proceed as normal.

Applies to docs-only changes too (lighter review), not just code. Related: [[trailer-inflight-work-persistence]], [[trailer-ci-on-k8s-runners]].
