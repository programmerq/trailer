---
name: trailer-ux-evidence-ruling
description: Owner ruling 2026-07-10 — offscreen grab() screenshots satisfy the UX-Done gate for most changes; real-display/real-Mac smoke passes batched at milestones (hybrid)
metadata:
  type: project
---

Owner, 2026-07-10: "grab will work for _most_ items, and the hybrid approach is good. That'll let us use the more expensive approach only when grab() doesn't cut it!"

**How to apply (gate G2 ruling):** per-change UX-Done evidence = offscreen QWidget::grab() screenshots of every affected state, checked against the declared threshold. A real-display smoke pass (and a real-Mac pass for macOS-only paths like the no-window reopen behavior) is batched at each milestone/dogfood checkpoint, and additionally required per-change ONLY when the change plausibly depends on window-manager/native-chrome/compositor behavior that grab() cannot capture (native menus, dialogs, drag-drop visuals, multi-window management). Resolves open decision #3 from [[trailer-followup-docket]].

Related: [[trailer-perf-measurement-ruling]], [[trailer-review-before-push-policy]].

**Merge-visibility refinement (owner, 2026-07-15):** owner: "I've been burned before by merging items that seem like they're doing the right thing, including the previous PR that was just the backlog item" (the #37 diagnosis-only near-miss). Therefore: UX-affecting PRs must embed their before/after grab() evidence INLINE in the PR body (images committed to the branch or otherwise rendered on the PR page, raw URLs pinned to a commit SHA) — not merely produced in the session or attached to chat. The merge decision must be evidence-visible: what the owner merges is what the owner saw. First applied to PR #55 on request.
