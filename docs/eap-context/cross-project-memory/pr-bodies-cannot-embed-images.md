---
name: pr-bodies-cannot-embed-images
description: Workspace egress proxy strips inline image markdown from GitHub PR bodies/comments; use commit-to-repo + link-from-tree instead.
metadata:
  type: project
  modified: 2026-07-21T12:02:17.667Z
---

Environment fact (verified 2026-07-21 by the buildable-board session via direct curl PATCH — it's the org egress-proxy policy, not an MCP tool limitation): the workspace egress proxy strips/backtick-wraps inline image markdown (`![]()` and `<img>`, any host including raw.githubusercontent.com) in GitHub PR bodies/comments posted from sessions. Inline image embeds therefore cannot be delivered from session environments.

**How to apply:** the working delivery for visual review aids (schematic renders, graphs, before/after panels) is COMMIT-TO-REPO + LINK-FROM-TREE — commit images under a docs/ path on the PR branch and link them from the PR body (GitHub renders the PNG full-screen on click). This is codified as the electricsim schematic-change illustration standard (docs/schematic_change_illustration.md via PR #366, AGENTS.md §10 pointer): every schematic-change PR ships a full-sheet context render + a zoomed/annotated render per affected leg, committed and linked. If the owner wants true inline embeds, the egress policy must be adjusted on the environment — sessions cannot work around it. Related: [[perf-tests-ship-graphs]]. One-line confirmation.
