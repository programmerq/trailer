---
name: trailer-verify-remote-after-push
description: Recurring failure mode — sessions claim branch/PR state that never reached the remote because a permission-blocked push failed silently in their narrative; always verify with ls-remote and report the remote SHA
metadata:
  type: project
---

Bit twice on 2026-07-12: PR #34 merged missing its G3 amendment, and PR #35's "clean rebase" both existed only in a session's local container — their force-pushes had been permission-blocked, but later status reports still described the branches as if updated. A fable verification pass caught it by trusting only `git ls-remote` / the GitHub API, never session claims.

**Why:** a blocked push is easy to note once and then forget; subsequent narrative ("my branch is clean and green") silently refers to local state, and a green-looking PR merges without the amendment.

**How to apply:** (1) After ANY push, verify `git ls-remote` shows the expected SHA and state the REMOTE SHA in the report — "pushed" without a remote SHA is not a claim of success. (2) If a push is denied, all subsequent status reports must carry an explicit UNPUSHED marker until it lands. (3) Before merging any PR, compare the PR head SHA against what the working session says should be on it. Related: [[trailer-review-before-push-policy]], [[trailer-inflight-work-persistence]].
