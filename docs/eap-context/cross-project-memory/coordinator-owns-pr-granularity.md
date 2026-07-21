---
name: coordinator-owns-pr-granularity
description: Owner ruling (2026-07-18, electricsim PR #298) — the COORDINATOR decides PR granularity for small deliverables (research memos, one-file docs, small assets); subagents return files to the coordinator instead of opening tiny standalone PRs at will
metadata:
  type: feedback
  modified: 2026-07-18T05:06:25.004Z
---

Owner ruling 2026-07-18 (on electricsim PR #298, a one-file research memo opened as its own PR), verbatim: "too small for standalone PR. Don't close... The coordinator should be able to make these calls instead of letting smaller agents open tiny scoped PRs at will."

**Rule:** the COORDINATOR decides PR granularity for small deliverables — research memos, one-file docs, small assets. Sessions/subagents producing such deliverables return the file + report to the coordinator (or push to a shared batch branch) instead of reflexively opening a standalone PR; the coordinator batches related deliverables (e.g. sibling research memos) onto one branch/PR, holds them, or approves a standalone when scope warrants.

Code changes with their own test/gate lifecycle still get their own PRs per the existing fewer-larger-PRs preference ([[owner-prefers-larger-prs]]).

**Why:** tiny PRs multiply review overhead — same rationale as the owner's fewer-larger-PRs rule, now with explicit coordinator authority over the call.

**Note:** existing open memo PRs (#292 #298 #299 #295-297) stay as-are per the owner's "don't close"; the rule applies going forward.
