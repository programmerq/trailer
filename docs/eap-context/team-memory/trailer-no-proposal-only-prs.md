---
name: trailer-no-proposal-only-prs
description: Owner ruling 2026-07-17 (PR #74 comment) — proposal/decision-record-only PRs are not reviewable deliverables; DRs merge WITH their implementing PR; pre-implementation direction = socratic questions or a single 👍-request, never a docs-only PR
metadata:
  type: feedback
---

Owner on PR #74 (a decision-record-only PR), 2026-07-17: "oof. It's too much to have one PR for a doc/proposal. I know I've merged a few of these, but yikes. Flip this to a draft and then ask questions if you're not confident enough to implement a plan. This is not ready to merge with no code changes, no tests, no behaviors. It's fine to ask for a 👍 before you implement things, but you haven't put forward anything to review. Adjust the skill/threshold here."

**Why:** a proposal with no code/tests/behavior gives the owner nothing to review; it burns his review window on paperwork. This extends "code or it doesn't happen" (hollow-PR ruling on #56) from backlog closures to decision records.

**How to apply:** (1) decision records merge WITH the implementing PR, never alone. (2) Need direction first? Ask one-word-answerable socratic questions with defaults, or request a single 👍 on the plan — via the coordinator, not a docs-only PR. (3) If a proposal PR exists at all it stays DRAFT until implementation lands in it. (4) Distinguish from the no-lingering-drafts rule: clear work → ready-for-review WITH code; proposal → ask, don't PR. Encoded in the surface-the-ask skill. Related: [[trailer-review-before-push-policy]], [[trailer-dr-naming-date-slug]], [[proceed-on-clear-defaults]].
