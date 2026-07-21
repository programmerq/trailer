---
name: redux-draft-pr-post-questions-on-pr
description: Owner refinement (2026-07-21, redux #66) — a draft PR carrying open owner-questions must post the FULL clarifying-question battery (with recommendations, defaults, and success criteria) as a PR COMMENT, so the draft is never stranded with no activity; encoded in redux CLAUDE.md/AGENTS.md.
metadata:
  type: feedback
  modified: 2026-07-21T04:53:53.221Z
---

Owner ruling 2026-07-21 on redux PR #66 (Phase 0 image triage), verbatim: "Ask a battery of clarifying questions here in the PR as a comment. That should help you disambiguate and identify clear success criteria so you can ask me to review it. Adjust the agents.md/claude.md wording to avoid the problem of a draft PR being stranded in draft mode with no activity."

**The norm.** A draft PR that is draft BECAUSE it carries open owner-questions must post those questions as a PR COMMENT — a self-contained battery: per-question gloss, options+implications, a recommendation, and a DEFAULT the agent will take if unanswered, PLUS the success criteria for review. This makes the draft's path-to-ready visible ON the PR, not only in the coordinator side-channel. Take defaults for anything unanswered, record them as resolved decisions in the PR body, then flip ready. Never leave a draft with neither activity nor a posted question.

**Why.** Extends [[electricsim-draft-pr-must-state-hold-reason]] (state hold reason on the PR) and is compatible with [[decision-requests-ride-coordinator-not-committed-files]] (a PR COMMENT is not a committed decision file; still fine to also relay via coordinator). The failure mode being fixed: a draft PR sitting silent while the owner can't tell what it's waiting on.

**How to apply.** On any draft PR gated by owner-questions: post the battery-as-comment with recommendations+defaults+success-criteria; keep it draft until the owner rules or the defaults are folded in; then flip ready yourself. Encoded in redux CLAUDE.md/AGENTS.md PR section (this PR).
