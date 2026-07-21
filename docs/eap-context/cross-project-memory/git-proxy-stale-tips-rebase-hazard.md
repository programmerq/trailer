---
name: git-proxy-stale-tips-rebase-hazard
description: Session-env git proxy can serve stale branch tips — verify true tips via GitHub API before rebasing already-pushed branches, or rebases silently drop the newest commit.
metadata:
  type: project
---

# Git proxy stale-tips rebase hazard

Discovered 2026-07-20 during the ev1 sourcing-PR rebase pass: the session
environments' git proxy can serve **stale branch tips** — lagging the live
GitHub tip by the most recent commit, on several branches at once.

Consequences:

1. Plain `git push --force-with-lease` rejects with "stale info".
2. Far worse: a rebase based on the proxy's tip silently **drops the newest
   commit** on the branch. This nearly lost the wiper fix on ev1 #13 and the
   stale-note fix on #25.

Mitigation that worked:

- Cross-check every branch's true tip SHA against the GitHub API (MCP)
  **before** rebasing.
- Base the rebase on the verified real tip.
- Push with an explicit lease against that verified SHA:
  `--force-with-lease=<branch>:<sha>` — still a lease, never plain `--force`.

Rule: any session rebasing an already-pushed branch in this environment must
verify the remote tip via the GitHub API first, then fetch/rebase only once
local matches it.

Related: [[pr-draft-ready-merge-policy]].
