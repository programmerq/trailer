---
name: fork-purpose-and-constraints
description: programmerq/teleport fork is a scratch space to finish Jeff's unmerged WIP; never disturb upstream gravitational/teleport
metadata:
  type: project
---

Jeff (programmerq, Teleport support engineer) uses the programmerq/teleport public fork to collect and finish his in-flight/abandoned work items. Constraints: do NOT push to or trigger anything on upstream gravitational/teleport (this Claude Code project is an early preview under a no-advertising agreement). Fork master was fast-forwarded to upstream (b4006bc → 6965b7e, 2026-07-10). Jeff intended Actions disabled on the fork (2026-07-10) but empirically PR events still fire ~26 runs each (pushes fire none); 3 checks always fail for fork-env reasons (OIDC issuer mismatch x2, missing CLA secret) and never evaluate the diff — treat the fork like a local checkout: run tests locally in-session, ignore fork CI. See [[fork-ci-noise-and-worktree-notes]]. Cross-fork PRs to upstream are disallowed by the upstream repo — Jeff hand-carries finished fork branches to upstream branches himself; draft PRs within the fork (branch → fork master) are the staging mechanism. His real WIP branches live on UPSTREAM under jeff/* and programmerq/* (he has employee push access) — the fork's own branches are 2021 relics. teleport.e private submodule is unreachable from this environment: skip e/ work. See [[wip-branch-survey-2026-07]].
