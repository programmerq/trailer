---
name: ev1-sourcing-pr-granularity
description: Owner PR-granularity preference (2026-07-20) for the EV1 replica program (programmerq/ev1 sourcing) — prefer LARGER, rolling PRs; keep building on an open PR/branch as long as the work is coherent instead of one-PR-per-round. Commits stay small/well-described; the PR unit grows.
metadata:
  type: feedback
---

Owner directive relayed 2026-07-20 (verbatim): "I think you might be able to adjust the guidelines in the project. Not necessarily to merge more PRs, but to keep building for as long as you can on a given PR/branch/whatever. I prefer larger PRs anyway!"

**Why:** the owner is reviewing the EV1 replica sourcing program and prefers fewer, larger, coherent PRs over a proliferation of small one-round-each PRs. Less PR-management overhead for him; the branch accretes related work.

**How to apply (programmerq/ev1 sourcing lanes specifically):**
- Keep building on an OPEN PR/branch as long as the work is coherent — accumulate related sourcing rounds, the harness strategy, hardware cases, etc. on ONE rolling branch/PR rather than cutting a fresh small PR per round.
- Commits stay small, focused, well-described (unchanged); it's the PR *unit* that grows, not the commit.
- Start a new branch/PR only at a genuine coherence boundary (a distinct workstream), not per round.
- This is the owner's stated preference for HIS ev1 umbrella repo; it does NOT override the electricsim repo's own "small focused PRs" convention (different repo, different owner-of-that-convention).
- Consider reflecting this in the ev1 `sourcing/README.md` guidelines when convenient (the owner suggested "adjust the guidelines in the project").

Related: [[ev1-catalog-sweep-complete]] [[owner-max-progress-adversarial-gate]] [[ev1-replica-sourcing-plan]] [[ev1-umbrella-repo]].
