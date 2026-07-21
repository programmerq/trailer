---
name: ready-for-review-not-gated-on-ci
description: Owner ruling (2026-07-21, electricsim #368) — ready-for-review is NOT gated on CI being green; CI is a separate signal the owner watches himself. Flip a PR ready once the WORK/rework is done; never hold a PR in draft waiting for CI green.
metadata:
  type: feedback
  modified: 2026-07-21T11:40:47.431Z
---

# Ready-for-review is decoupled from CI status

Owner ruling 2026-07-21 on electricsim PR #368 (the anti-stranding-rule PR itself), verbatim: "No, don't gate ready for review on CI status. I can see CI status just fine. That's a separate signal from 'ready for review'."

**The rule.** Flipping a PR to ready-for-review means the WORK (or the requested rework) is DONE and the PR wants the owner's eyes. It is NOT conditioned on CI being green. CI status is an independent signal the owner watches himself — never a reason to hold a PR in draft. So: don't wait for CI to go green before flipping ready; flip the moment the work is done. Draft is reserved for (a) rework actively in progress, or (b) a blocking question that is BOTH posted on the PR and genuinely unanswered (apply stated defaults and flip rather than strand).

**Why.** Conflating "ready for review" with "CI green" strands PRs in draft while CI churns, for no information gain to the owner — he can read CI himself. This refines [[pr-draft-ready-merge-policy]], whose earlier phrasing ("flip ready as soon as CI is green AND local review passed") over-coupled the two; the "work is done" gate stands, the "CI green" gate does not.

**How to apply.** After addressing review feedback, flip the PR back to ready-for-review yourself as soon as the rework is done — do not wait on CI, and do not wait to be asked. Mechanism: mcp__github__update_pull_request with draft:false (the dedicated mark-ready path can be permission-blocked — see [[ready-flip-use-update-pull-request]]).

**Context.** This ruling landed while codifying the anti-stranding rule across electricsim/redux/ev1sim AGENTS.md+CLAUDE.md; the owner flipped #368 back to draft to trigger exactly the rework-then-reflip loop the rule describes. The in-repo wording was corrected to remove the CI-green precondition. Related: [[electricsim-draft-pr-must-state-hold-reason]], [[redux-draft-pr-post-questions-on-pr]], [[trailer-signal-human-review-norm]].
