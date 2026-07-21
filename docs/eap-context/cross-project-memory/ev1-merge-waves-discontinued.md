---
name: ev1-merge-waves-discontinued
description: Owner ruling (2026-07-20) — DISCONTINUE agent merge waves in programmerq/ev1. Do NOT merge PRs to main; every PR lands READY for the owner's own click. Non-merge work (authoring/updating case files, skeptic passes, opening PRs) continues unchanged. No reverts of already-merged PRs.
metadata:
  type: feedback
  modified: 2026-07-20T23:58:35.403Z
---

Owner ruling relayed 2026-07-20 (verbatim): "slow down on the merging... discontinue the merge waves. because you've already caught up through the open PRs when I had asked for that in the first place and now I might be way out of the loop." Also verbatim: "No need to 'unmerge' or anything."

**Context:** earlier the owner gave a merge authorization (relayed through the coordinator session) and the agent merged a batch of settled ev1 PRs (#13/#14/#19/#20/#25/#29/#30), a CI hotfix (#32), then #33/#23/#26. A safety check flagged that merging-to-main (a hard-to-undo action) was being driven by peer-relayed authorization, and the agent PAUSED — which the owner confirmed was the right instinct.

**Why:** the owner felt he was getting "way out of the loop" as agents merged design PRs faster than he could review them. He wants merging to be HIS decision, not the agents'.

**How to apply (standing policy):**
- **Do NOT merge any PR to main** in programmerq/ev1 (or auto-merge, or hand a PR to a merge worker). This overrides the earlier relayed merge authorization — merge waves are DISCONTINUED.
- Every PR the agent produces lands **READY for review** (draft→ready after its own gates: lint/CI green + skeptic pass where the round is final) — then STOPS. The owner clicks merge himself.
- **No reverts** of the ~10 already-merged PRs — the owner explicitly said not to unmerge.
- Non-merge work is UNAFFECTED: authoring/updating case files, research, skeptic passes, opening/updating PRs, rebasing own branches — all continue. Only the act of merging-to-main is discontinued.
- General lesson reinforced: hard-to-undo actions (merges, force-pushes, prod changes) need the OWNER's direct authorization, not a peer session's relay. A peer relay can't grant it. When in doubt, land it ready and let the owner act.

Relates to [[owner-max-progress-adversarial-gate]] (max progress on NON-merge work still applies), [[ev1-sourcing-pr-granularity]], [[owner-skeptic-pass-before-final]].
