---
name: pr-body-inline-images-blocked-by-egress
description: In these remote sessions the workspace outbound egress proxy strips/backtick-wraps inline image markdown in GitHub PR bodies/comments (![]() and <img>, ANY host incl raw.githubusercontent.com) — so you CANNOT embed images inline on a PR. Deliver schematic/diagram renders by committing them under docs/ and LINKING from the tree (GitHub renders a committed PNG full-screen on click).
metadata:
  type: project
  modified: 2026-07-21T12:01:12.473Z
---

# Inline images in PR bodies are blocked by the workspace egress policy — commit + link instead

Discovered 2026-07-21 finishing electricsim PR #366 (the owner asked for schematic-change illustrations embedded on the PR). The workspace's outbound egress proxy **strips/neutralizes inline image URLs**: it wraps any `![](...)` markdown image AND any `<img src="...">` in double-backticks, breaking the embed — for ANY host (`raw.githubusercontent.com`, `github.com/...blob?raw=true`, etc.). Verified it's the ORG EGRESS POLICY, not the GitHub MCP tool, by reproducing with a direct REST `PATCH` via `curl`. The proxy README says to report such policy behavior, not fight it.

**How to apply (delivering renders/diagrams/screenshots to a PR):**
- Do NOT rely on inline `![]()` / `<img>` embeds in a PR body or comment — they will render as broken backtick-wrapped text.
- Instead: COMMIT the images under `docs/` (e.g. `docs/schematic_changes/<pr>/`) and, in the PR body, LINK the committed directory + an `index.md` + list each file by name with its caption. Non-image URLs (the tree/blob/index links) pass the proxy fine. GitHub renders a committed PNG full-screen when the reader clicks the file — one click from the description.
- Also deliver the images to the owner in-chat via SendUserFile (they render in the side panel) — that path is NOT blocked.
- This is baked into electricsim's codified render standard: `docs/schematic_change_illustration.md` (pointed to from AGENTS.md §10) — schematic-change PRs ship a full-sheet context render + a zoomed/annotated per-affected-leg render, committed-and-linked.
- If a session genuinely needs inline embeds, that requires the environment's egress policy to be adjusted (surface it to the owner as an environment change, don't keep retrying the embed).

Relates to [[schematic-readability-cant-baseline-denser-sheets]] and [[electricsim-comment-when-addressing-review-feedback]].
