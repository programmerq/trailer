---
name: review-bot-ci-visibility-complaint-response
description: Owner standing practice (2026-07-19) — when the claude review bot complains it can't see CI checks, reply telling it to review on merits and not gate on CI-status visibility; required checks enforce CI.
metadata:
  type: feedback
---
Owner 2026-07-19, verbatim: "The claude review bot is now complaining that it can't see CI checks. Just tell it to review and not worry about CI checks."

**The practice.** When the claude review bot posts that it could not confirm CI status (e.g. `gh pr checks <n>` fails with `GraphQL: Resource not accessible by integration` — a bot-token permission limit, not transient), reply on the review thread instructing it to review the code on its merits and NOT gate on CI-status visibility. CI gating is enforced by the repo's required checks, independently of what the bot can see. Standing practice on all PRs (including #332).

**Durable fix option** (better than replying per-thread forever): update the review workflow prompt (`.github/workflows/claude-pr-review.yml`) to stop instructing the bot to rely on `gh pr checks` — same ergonomics lane as the #320 read-only-static-review + wait-for-CI change. Related: [[electricsim-draft-pr-must-state-hold-reason]].
