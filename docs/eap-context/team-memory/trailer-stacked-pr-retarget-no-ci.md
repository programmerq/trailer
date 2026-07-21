---
name: trailer-stacked-pr-retarget-no-ci
description: A stacked PR that is later retargeted from its parent branch to main gets ZERO CI checks — ci.yml keys on pull_request→main, and changing the base is an 'edited' event it ignores, so no run fires. Force-push a fresh main-based head (rebase onto current main, or empty commit) to trigger CI; verify checks actually attach.
metadata:
  type: project
  modified: 2026-07-21T14:16:00.124Z
---

Observed 2026-07-21 on PR #108 (OCR disk cache, stacked on #100's branch claude/ocr-lazy-window). Sequence that produces zero CI: (1) the stacked PR's head was force-pushed while its base was still the parent feature branch — ci.yml (`on: pull_request` with `branches: [main]`) did NOT run because the PR's base was not main at push time; (2) after #100 merged, the PR base was changed to main via the API — but a base change is a `pull_request: edited` event, which ci.yml does not listen for (`synchronize`/`opened`/`reopened` only). Net: the PR sits with base=main and `get_check_runs` total_count=0, so absence-of-checks ≠ green (the PR body may even claim "CI runs normally").

**How to apply:** After retargeting a stacked PR to main, do NOT trust that CI will attach on its own. Create a NEW push event on a main-based head: `git fetch origin main && git checkout -B <branch> <verified-remote-head> && git rebase origin/main` (if main advanced, the head SHA changes → push fires CI; if it did not, add one `git commit --allow-empty -m "ci: re-trigger checks"`), then `git push --force-with-lease=<branch>:<old-sha>`. Then VERIFY via github MCP `get_check_runs` on the new head that checks are now present (queued/in_progress/completed) — that proves CI fired. On #108 this took a17feb5→f7d80f7 (rebase onto merged-#100 main); 0 checks → 10 checks attached. Rebuild+test locally first (CI builds the PR merged with main). Links [[trailer-verify-remote-after-push]], [[trailer-remerge-main-before-final-verify]], [[trailer-ci-on-k8s-runners]].
