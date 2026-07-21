---
name: pr-must-be-self-contained-answer-concerns-scrub-stale-prose
description: Owner ruling (2026-07-21, electricsim #364) — a PR must be self-contained on GitHub. Before flipping ready, SCRUB stale "do not merge / draft for review" prose from the body, and ANSWER every reviewer concern ON the PR (a comment tying the fix + context to the finding). A ready PR whose body says "draft" or whose raised concern has no visible answer is not acceptable.
metadata:
  type: feedback
  modified: 2026-07-21T05:14:57.576Z
---

# A PR must stand on its own on GitHub — scrub stale prose, answer concerns in-thread

Owner ruling 2026-07-21 on electricsim #364 (verbatim): "But now it's marked as ready-for-review. I'm flipping back to 'draft'. Answer the concern and lay out the context. This is not helpful to see a PR that asks for context and then there's none. please and thank you!"

**What triggered it.** #364 was flipped ready while (a) its BODY still said "do not merge yet — draft for adversarial review" (stale — the review was done), and (b) the reviewer's changes_requested concern (a missing vat/report.py test) had been addressed in a commit but was NOT answered anywhere on the PR thread. So a reader saw a raised concern with no visible response, and a "ready" PR whose own body said "draft." The owner converted it back to draft.

**The rule (standing).** A PR is a self-contained artifact the owner reads on GitHub — not a thing whose context lives only in an agent session. Before flipping a PR ready (and whenever addressing review feedback):
1. **Scrub stale/contradictory prose from the PR body** — remove any "do not merge yet", "draft for review", "WIP", or self-adversarial-review notes once that phase is done. The body must describe the CURRENT state.
2. **Answer every reviewer concern ON the PR** — a top-level comment that restates the concern, names the fix commit SHA, and lays out the context/reasoning. Addressing it in a commit alone is NOT enough; the answer must be visible in the PR thread (pairs with [[electricsim-comment-when-addressing-review-feedback]] — this is exactly the "substantive review-finding response" that the minimize-chatter rule explicitly permits).
3. **The body must lay out the context** a reviewer/owner needs: what the PR does (Before/After), review status, how each raised concern was resolved.
4. If the owner manually converts a PR to draft, **do not re-flip it ready** — answer/lay-out as asked and leave it for his click (per the "owner converted → don't re-ready unless asked" webhook guidance).

Related: [[electricsim-comment-when-addressing-review-feedback]], [[no-op-checks-get-no-pr-comment]], [[pr-draft-ready-merge-policy]].
