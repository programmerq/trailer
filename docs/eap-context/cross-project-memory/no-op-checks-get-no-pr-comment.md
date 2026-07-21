---
name: no-op-checks-get-no-pr-comment
description: Owner rule (2026-07-18) — a no-op / clean check result gets NO PR comment; comment on a PR only when something actually changed. "If there's nothing, then do nothing."
metadata:
  type: feedback
  modified: 2026-07-18T05:12:50.569Z
---

Owner correction 2026-07-18, verbatim: "This is noise. Don't comment saying there's nothing. If there's nothing, then do nothing." Prompted by a clarifying "no random-suffixed backlog IDs found" comment posted on a draft PR (electricsim #299) after a scan came up empty — the owner had it deleted.

**Why:** no-op confirmations are noise on the PR thread; the owner reads PR comments as signal that something changed.

**How to apply:** After running a check/scan/scrub on a PR that finds nothing to change, post NO PR comment and make no commit — just report the clean result up-channel (coordinator / status / reply) instead. Comment on a PR only when an actual change was made. Applies to any "verify X" / "scrub Y" / "audit Z" task whose result is a no-op. Related: be frugal about GitHub comments generally.
