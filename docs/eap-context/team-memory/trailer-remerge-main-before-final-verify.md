---
name: trailer-remerge-main-before-final-verify
description: Process lesson (2026-07-21) — CI builds the PR MERGED WITH MAIN, and main moves during long (~50min) Qt builds; re-merge latest origin/main and re-run tests right before the final push, or a test added to main after your branch point can red-CI a branch that passed locally
metadata:
  type: feedback
  modified: 2026-07-21T00:43:21.492Z
---

# Re-merge origin/main right before the final push — CI tests the merge, and main moves

On 2026-07-21, PR #107 (off-thread PDF open) passed the implementing worker's local run (55/55 unit + 29/29 UAT) but **both** the Linux and Wine unit-test CI jobs went red on the pushed head. Root cause was NOT flakiness and NOT a Wine artifact: while the worker's ~50-min Qt build ran, PR #78 (`#74` quit-and-keep windows) merged to main, adding `test_quit_and_keep_windows`. That new test exercised the branch's async-open change and exposed a genuine regression — moving `QPdfDocument::load` off-thread left `m_valid` false until adoption, so an annotation added before window-attach wasn't dirty-tracked (`isDirty()` false). The worker only reproduced it after merging latest main into the branch, because **GitHub CI builds the PR merged with main**, not the branch head in isolation.

**Why:** a branch cut from origin/main at the start of a long build is stale by the time it pushes; a merge-with-main-only test failure is invisible to a local run against the older base.

**How to apply (standing, for any implementation worker whose build/test cycle is long):**
1. Right before the FINAL local verification + push, `git fetch origin main` and merge/rebase it into the branch, then rebuild clean and re-run `ctest -LE uat` + the UAT suite against the merged tree — that mirrors what CI will build.
2. Treat "passed locally on my base" as insufficient when main has advanced; the honest green is against current main.
3. A red CI on a branch that passed locally: first suspect a test/feature that landed on main after your branch point (build the merge, reproduce) before assuming flake or an environment artifact.

The fix for #107 forced `ensureDocLoaded()` in `PdfDocument::annotations()` to restore the pre-async `m_valid` invariant (drains only the doc-open worker; annotation sweep stays async). Related: [[trailer-review-before-push-policy]], [[trailer-wine-same-file-save-qskip]], [[trailer-verify-remote-after-push]].
