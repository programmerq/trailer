---
id: 2026-07-12-ci-artifact-reuse-github-only
title: Cross-run artifact reuse when a re-run touches only .github/ (not source)
priority: TBD
status: open
source: owner message 2026-07-12 (process improvement)
created: 2026-07-12
---

## Threshold

TBD — declare before work begins. Direction: when a re-run's diff touches only
`.github/` (workflow/CI changes) and no source, the pipeline reuses the prior
per-OS artifacts instead of rebuilding — e.g. a cache keyed on a source-tree
hash so a CI-only change is a cache hit.

## Context / Body

Process improvement. Rebuilding the macOS app on every run is expensive; a run
whose only change is to GitHub Actions config should not trigger a full per-OS
rebuild. Owner: "It'd be nice to be able to use an artifact from a previous run
if the only change was to github actions and not source code. it's quite
expensive to rebuild the mac app every time."

Priority: owner stated none — recorded as `TBD` per the "don't invent a
priority" rule.

## Provenance

Owner message, 2026-07-12.
