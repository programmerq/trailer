---
name: trailer-cross-container-persistence
description: Only memory-service .md files replicate across session containers — patch files under team/patches/ did NOT sync; unpushed branch work dies with its container
metadata:
  type: project
---

Learned 2026-07-09 the hard way: sessions preserved finished branches as `git format-patch` files under `/tmp/claude/memory/team/patches/` believing team memory is a shared mount. It is not — only markdown memory files registered with the memory service replicate to other containers. A later session found NO `patches/` dir and no `*.patch` anywhere; the branch commits existed only in the (possibly reclaimed) original containers.

**Why:** the memory dirs are synced per-file by the memory service (.md memories + index), not a network filesystem.

**How to apply:** the ONLY durable cross-session carriers for code are: (a) pushing a branch to origin, or (b) inlining a small diff INSIDE a .md memory file body. Never park work as loose files in the memory tree. If work must stay unpushed, keep the owning session's container warm or accept rebuild-from-report.

Related: [[trailer-inflight-work-persistence]] — the sequel/correction: the "lost" branch was in fact on origin all along; always `git fetch` fresh before declaring work lost.
