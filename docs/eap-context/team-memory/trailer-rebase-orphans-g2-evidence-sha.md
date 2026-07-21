---
name: trailer-rebase-orphans-g2-evidence-sha
description: Process gotcha (2026-07-21) — rebasing/force-pushing a UX PR orphans the commit SHA its G2 before/after evidence URLs are pinned to; the images stop rendering on the PR page even though files survive. After any force-push, re-pin the body's raw.githubusercontent.com URLs to the new head SHA and re-verify inline render.
metadata:
  type: feedback
  modified: 2026-07-21T05:19:37.745Z
---

G2 evidence is committed under `docs/uat/images/` and referenced INLINE in the PR body via **commit-SHA-pinned** `raw.githubusercontent.com/programmerq/trailer/<SHA>/...` URLs (and DR links via `github.com/.../blob/<SHA>/...`). When a UX PR is later **rebased and force-pushed** (e.g. to resolve a conflict after another PR merges to main), the old head SHA becomes dangling and is eventually GC'd — the pinned evidence URLs 404 and the merge decision is no longer evidence-visible, even though the image FILES survive in the new head commit.

Concretely (2026-07-21, PR #105 theme-live-wire): after PR #101 merged, #105 was rebased onto new main and force-pushed `70ac9fd` → `cda9993`; all 6 SHA-pinned references (5 `docs/uat/images/theme-*.png` embeds + 1 DR blob link) still pointed at the orphaned `70ac9fd`.

**Why:** the SHA pin is what makes the image render "what the owner merges is what the owner saw" — a dangling SHA breaks exactly the guarantee G2 exists for.

**How to apply:** After ANY force-push/rebase of a UX PR, re-pin every `<SHA>` in the body's raw image URLs and blob links to the NEW head SHA, verify each file exists at that SHA before pinning, then re-fetch the body and confirm the images actually render inline (bare src, no backtick-defang — see [[trailer-pr-inline-image-proxy-defang]]; HTTP 200 is NOT sufficient proof of inline render). Pull the raw body via the GitHub API, not the MCP `get` (which HTML-escapes `&`/quotes and will corrupt the body on write-back). Related: [[trailer-ux-evidence-ruling]], [[trailer-verify-remote-after-push]], [[trailer-remerge-main-before-final-verify]].
