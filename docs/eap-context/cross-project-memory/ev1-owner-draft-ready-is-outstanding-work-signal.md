---
name: ev1-owner-draft-ready-is-outstanding-work-signal
description: Owner clarification (2026-07-21, programmerq/ev1 PR #22) — in this repo the owner uses GitHub draft/ready state as his OUTSTANDING-WORK signal because PRs are opened under his own account, so he can't use GitHub's request-changes review machinery. Therefore the AGENT owns marking a PR ready-for-review the moment it's actually done; when the owner manually flips a PR to draft to flag outstanding work, the agent must flip it BACK to ready once that work is genuinely complete — do NOT leave it drafted waiting for his click.
metadata:
  type: feedback
  modified: 2026-07-21T11:24:05.476Z
---

Owner ruling 2026-07-21 (programmerq/ev1 PR #22), verbatim: "I manually flipped it to draft so I could mark it as having outstanding work. If it's ready for me to review again, mark it as ready to review again. That's the policy in this repo. I can't use the request change review machinery in github since you open PRs with my github account."

**Why:** because the agent opens PRs using the owner's OWN GitHub account, the owner cannot act as an independent reviewer (can't use GitHub's request-changes / review-approval machinery on his own PR). So he repurposes the draft/ready toggle as his own status flag: flipping a PR to DRAFT is how he marks "this has outstanding work / I'm not reviewing it yet." It is NOT a signal that the agent must stop touching it or wait for his click to re-ready.

**How to apply (programmerq/ev1):**
- Marking a PR **ready-for-review is the AGENT's job** and is the signal "this wants your review." Flip to ready the moment the PR is genuinely done: CI/local-green + local/adversarial review passed + zero outstanding work ON THAT PR.
- **If the owner manually flips a PR to draft, that does NOT mean "leave it drafted."** It means he's flagging outstanding work. Once that work is actually complete (or if it was already complete and he just wanted to signal), the agent flips it BACK to ready itself — with a narration comment describing current state. Do not bounce the click back to him ("say the word and I'll mark it ready" is wrong here).
- This REFINES the general "owner manually converted to draft → don't re-flip unless asked" guidance ([[pr-must-be-self-contained-answer-concerns-scrub-stale-prose]] rule 4): in THIS repo, re-flipping to ready once the work is done IS what the owner wants, precisely because draft/ready is his personal work-status flag, not a lock.
- Distinguish OUTSTANDING WORK ON THE PR (keep draft until done) from SEPARATE FORWARD-LOOKING artifacts (e.g. a next-phase scope battery) — the latter does not block flipping a complete PR ready.
- Still: merges to main remain the owner's explicit in-session call ([[ev1-merge-waves-discontinued]]); this is only about the draft↔ready flip.

Relates to [[pr-draft-ready-merge-policy]] [[pr-must-be-self-contained-answer-concerns-scrub-stale-prose]] [[redux-draft-pr-post-questions-on-pr]].
