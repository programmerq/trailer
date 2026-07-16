---
id: 2026-07-12-release-uat-before-build
title: Release fail-fast ordering — run UAT before the per-OS artifact builds
priority: TBD
status: done
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

## Resolved 2026-07-15

Made the `uat` job a prerequisite gate for the entire per-OS build matrix
in `.github/workflows/release.yml`. Previously `uat` had `needs: [precheck]`
and ran in parallel with the build jobs, so a UAT failure could not stop an
in-flight macOS build. Now:

- `linux-build`: `needs: [precheck, uat]` (was `[precheck]`)
- `windows-cross`: `needs: [precheck, uat]` (was `[precheck]`)
- `macos-build`: `needs: [precheck, uat, linux-build, windows-cross]`
  (was `[precheck, linux-build, windows-cross]`)
- `uat`: unchanged (`needs: [precheck]`) — it now runs first and gates all
  three artifact builds.

`precheck` is preserved in every `needs:` list (it supplies the
`is-release-ready` / `version` outputs) and the existing
`if: needs.precheck.outputs.is-release-ready == 'true'` guards are
unchanged. Because those are plain (non-`always()`) `if:` conditions, GitHub
requires every listed need to succeed before the job runs — so a UAT failure
short-circuits the release before any per-OS artifact (notably the ~10x-cost
macOS build) is built, which is the intended fail-fast behavior.

## Provenance

Owner message, 2026-07-12.
