---
name: electricsim-draft-pr-must-state-hold-reason
description: Owner norm (2026-07-18) — any PR held in DRAFT must state its hold reason ON the PR itself (a comment or a prominent body line) saying exactly what it's waiting on, so the draft state is legible without asking.
metadata:
  type: feedback
  modified: 2026-07-18T05:14:52.853Z
---

# Draft PRs must state their hold reason on the PR

**The norm.** When a PR is (or goes) draft, it must carry an explicit, legible
hold reason **on the PR** — either a comment or a prominent body line — stating
WHAT it's waiting on / why it isn't ready. A silent draft is not acceptable; the
owner must be able to see from the PR itself why it's held.

**Format that satisfies it** (from the concrete case):

> "back to draft to land as one complete PR; folding in X, Y, Z; re-flips when
> whole and reviewed."

**Why.** The owner works async and reviews the PR conversation. A draft with no
stated reason forces him to hunt for what's blocking it. This pairs with the
related norm that a half-done change must NOT be marked ready-for-review.

**How to apply.** Whenever you convert a PR to draft — or open one as draft that
isn't immediately flip-ready — post/maintain a one-line hold-reason (comment or
body) naming the remaining scope or the blocker. Update it when the blocker
changes; it flips ready only once the stated condition is met.

**Context.** Ruled 2026-07-18, same session as the "#300 should be ONE PR, don't
flip ready half-done, ask scope questions via the coordinator" correction (owner
rejected a premature PR1-of-2 ready-flip on electricsim#300).

Related: [[pr-draft-ready-merge-policy]],
[[electricsim-comment-when-addressing-review-feedback]]
