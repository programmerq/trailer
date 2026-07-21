---
name: post-merge-conflict-sweep
description: Owner rule (2026-07-16) — after every merge to main, the coordinator checks all open PRs' mergeable state and dispatches conflict resolution to the owning sessions
metadata:
  type: feedback
---

After any PR merges to electricsim main, the coordinator runs a periodic check of the remaining open PRs' mergeable state and dispatches resolution to each conflicted PR's owning session. Owner (2026-07-16): "do periodic checks after you detect a merge event to make sure other PRs get resolved if conflicts come up. Most are small mechanical conflicts that aren't a problem."

**Why:** main moves in bursts during merge trains; every big merge (e.g. #211) conflicted the whole open-PR set, and waiting for the owner to notice each conflict wastes his review windows.

**How to apply:** on each pr_closed/merged event, if other PRs are open: dispatch one worker to list open PRs + mergeable_state; message each conflicted PR's session to resolve (mechanical conflicts: rebase or merge-of-main at the session's discretion — merge-of-main avoids the force-push guard; regenerated artifacts regenerate, never hand-merge; full gate before push). Sessions confirm mergeable-clean. Related: [[pr-draft-ready-merge-policy]], [[sessions-close-own-todo-items-on-branch]].
