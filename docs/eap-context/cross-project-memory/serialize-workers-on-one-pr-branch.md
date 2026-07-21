---
name: serialize-workers-on-one-pr-branch
description: Never run multiple background workers that MUTATE the same PR branch concurrently — they race on force-push, produce an ambiguous branch head, and trigger redundant review cycles. Serialize: one branch-mutating worker at a time; the next only after the prior confirms its pushed head.
metadata:
  type: feedback
---

# One PR branch → one mutating worker at a time (serialize)

**The mistake (2026-07-20, ev1sim #31):** several background workers were spawned against the SAME PR branch while an earlier one was still running — a rebase worker, a "fix redundant restatements" worker, and an "authoritative reconciliation" worker, overlapping the original sweep worker. They race-pushed (`--force-with-lease` / fast-forward) against a branch head that moved under each of them, producing an ambiguous head (`eaedb61` vs `f6ef8f2` vs `797d71b`), one worker whose commit landed on a different parent than it reset to, and multiple redundant automated re-review cycles. It resolved, but only after stopping the extra workers and doing a single authoritative pass.

**Why it's a trap:** each worker `git fetch`+`reset --hard`s to the head it sees at *start*, then pushes minutes later. If another worker pushed in between, the loser either force-clobbers real work or its push is rejected as stale — and you can't tell from the outside which head is canonical.

**How to apply:**
1. **At most ONE background worker mutating a given branch at a time.** Do NOT spawn a second worker to touch a branch while the first is still running/unconfirmed.
2. If a branch needs several changes, put them in ONE worker's brief, or dispatch them strictly SERIALLY — wait for worker N to report its final pushed head sha before dispatching worker N+1 (which resets to that sha).
3. If you realize workers are overlapping on one branch, **TaskStop the extras immediately**, then run a single authoritative worker that resets to the *current remote head*, verifies the intended content is all present, and fixes any gap.
4. Read-only watcher/poller workers on the same PR are fine (they don't push) — the rule is about MUTATING (commit/push/rebase) workers.

Related: [[verify-pr-diff-scope-before-push]].
