---
name: electricsim-comment-when-addressing-review-feedback
description: Owner standing policy (2026-07-18) — every fix commit that addresses PR review feedback must be PAIRED with a PR comment tying the fix to the feedback + SHA; PREFER a CONSOLIDATED TOP-LEVEL PR comment mapping each commit → what it addressed, because inline-thread replies ALONE can be missed by the owner (who scans the PR timeline). A silent fix commit is not enough.
metadata:
  type: feedback
  modified: 2026-07-18T05:39:04.191Z
---

Owner standing policy (2026-07-18): every time you push a commit that addresses PR review feedback, PAIR it with a PR comment tying the fix to the feedback — either reply on the inline comment threads, or post one consolidated comment listing which items were addressed, what changed, and the commit SHA(s). A silent fix commit is not enough.

**Why:** the owner reviews the conversation, not just the diff. An unexplained fix commit makes him hunt for what it did and whether it actually covers the feedback.

**How to apply:** embed "after pushing the fix, post a PR comment mapping each addressed finding to its change + SHA" into every review-fix agent prompt.

This REFINES the older "be frugal with GitHub comments" guidance — review-fix acknowledgements are explicitly wanted, so they are not the frugality target.

## Refinement (2026-07-18): inline-thread replies alone can be MISSED — always post a consolidated top-level comment

Inline review-thread replies ALONE can be MISSED by the owner, who scans the PR **timeline**, not the collapsed/resolved inline threads. So when addressing review feedback, ALWAYS post a **CONSOLIDATED TOP-LEVEL PR comment** that maps each pushed commit → what it addressed (SHA + one line), in ADDITION to (or instead of) inline thread replies.

**Trigger seen in practice:** on electricsim#302, two fix commits landed with only inline Copilot-thread replies, and the owner flagged "added two commits but no comments after the review." The inline replies existed but were invisible on the timeline he was scanning.

**How to apply:** the top-level comment is a terse commit map — one bullet per pushed SHA (`` `<sha>` `` — what it addressed / whether fixed, deferred, or chore), plus a one-line test-status footer. Post it as a top-level PR/issue comment (github add_issue_comment with the PR number), not as an inline review reply.

## Refinement (2026-07-20): the acknowledgment STANDS, but write it AS A DESCRIPTION OF THE CODE CHANGES for the OWNER — no reviewer/bot/CI mention

The review-fix acknowledgment comment still stands (the owner scans the timeline and wants it), but it must be written as a **description of the CODE CHANGES for the OWNER** — with **NO mention of the review bot, its caveats, the reviewer's identity, or CI mechanics/status**. Each bullet is purely "here's what this commit changed," mapped to its SHA. No Copilot mention, no model identifier, no "addressed the reviewer's caveat about X."

**Reconciles with** the 2026-07-20 "stop review-bot comment noise" directive ([[review-bot-ignore-ci-visibility]]): that ban covers comments aimed **AT the bot / at the reviewer-CI process meta**; the owner-facing fix→SHA map is aimed at the **OWNER** and stands. 

**Principle:** *silent toward the reviewer, legible toward the owner.* The commit-map comment says what the commits did, in owner-legible plain language — it does not narrate the review exchange.
