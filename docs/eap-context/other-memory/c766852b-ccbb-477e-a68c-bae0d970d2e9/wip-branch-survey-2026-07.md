---
name: wip-branch-survey-2026-07
description: 2026-07-10 survey of Jeff's unmerged upstream branches (last 6 months) + candidate work items
metadata:
  type: project
---

Survey (2026-07-10, details in scratchpad branch-survey.md of session cse_018cbQdV7GwAy4nrWSZ7327M). Unmerged upstream branches by Jeff:
- programmerq/app-chart-fix (2026-03-23, 2 ahead): Helm chart handles GCP apps without uri; near-complete, was in review.
- jeff/add-regexp-not-match (2026-06-15, 1 ahead): regexp.not_match_any/all predicates + tests; WIP experiment.
- jeff/automatic-upgrades-selfhosted (2026-02-02, 1 ahead): one-liner, auto-upgrades on self-hosted; near-complete.
- jeff/ca-rotate-openssh-doc-fix (2026-02-10, 2 ahead): 2-line doc fix; near-complete.

Pet peeve (greenfield, nothing in flight): tsh login is a no-op with valid session; tsh logout logs out ALL clusters — awkward for multi-cluster users (Jeff uses 2-6/day). See [[fork-purpose-and-constraints]].
