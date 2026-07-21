---
name: trailer-inflight-work-persistence
description: In-flight Trailer work must be pushed to an origin branch to survive; patches left only in the container-local memory dir do not replicate — but verify against a FRESH fetch before declaring work lost
metadata:
  type: project
---

# Persist in-flight Trailer work by pushing a git branch, not a local patch file

CORRECTION (2026-07-10): the empty-state window-model work was **NOT lost**. The original session pushed it to origin as `feat/empty-state-window-model` @ `f9dfd59` (tip: "Hide document-only toolbars over the empty state; add empty-state UATs", on top of `8499345` "Add empty-state / first-run window model") at ~2026-07-09T23:54Z. The session that declared it "permanently lost" had fetched *before* that push and was reasoning from a stale view of origin — its local checks (no patch file, no dangling objects, "no origin branch") were accurate for its snapshot but obsolete. **Lesson: `git fetch origin` immediately before concluding anything about what exists on the remote, and re-fetch before writing a loss/salvage memory.**

The still-valid lesson stands: the `/tmp/claude/memory/` tree is NOT a reliable carrier for arbitrary side files. A patch parked at `/tmp/claude/memory/team/patches/feat-empty-state-window-model.patch` in one container never appeared in the next; only registered .md memory files replicate (see [[trailer-cross-container-persistence]]). The remote-execution container is ephemeral — the repo is cloned fresh at start and the container is reclaimed after inactivity.

**How to apply:** to hand finished work between sessions, `git push -u origin <branch>` (a real branch on `programmerq/trailer`) or attach it to a PR — do NOT rely on a patch file dropped in the local memory dir. When a memory points at a side file, that file is only safe if it lives in git or on the remote.

Related: [[trailer-ci-on-k8s-runners]], [[trailer-remote-build-recipe]]. The separately-salvaged TODO.md "designer review" doc fold also lives on origin now as `docs/todo-designer-review-fold` @ `b9fd757` (single TODO.md-only commit, author Jeff Anderson).
