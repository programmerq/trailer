---
id: 2026-07-12-release-uat-before-build
title: Release fail-fast ordering — run UAT before the per-OS artifact builds
priority: TBD
status: open
source: owner message 2026-07-12 (process improvement)
created: 2026-07-12
---

## Threshold

`.github/workflows/release.yml` orders the UAT job as a **prerequisite gate**
before the per-OS build matrix, so a UAT failure short-circuits the release
before any expensive per-OS artifact (notably the macOS build) is built.

## Context / Body

Process improvement. Today the release pipeline can burn a ~10x-cost macOS
build even when UAT would have failed the release anyway. Reorder so the UAT
tier runs first and gates the build matrix. Owner: "maybe that UAT should run
before we attempt to build a release artifact across all three OS targets?"

Priority: owner stated none — recorded as `TBD` (process improvement) per the
"don't invent a priority" rule.

## Provenance

Owner message, 2026-07-12.
