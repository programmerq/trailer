---
id: 2026-07-12-ci-artifact-reuse-github-only
title: Cross-run artifact reuse when a re-run touches only .github/ (not source)
priority: TBD
status: wont-do
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

## Assessment 2026-07-15

Verdict: **WON'T-DO** as specified (a source-tree-hash cache, keyed on
everything except `.github/`, that restores prior per-OS artifacts on a
`.github`-only re-run). Disproportionate complexity and a real correctness
hazard for a rare case, when a zero-risk operational alternative already
exists. Evidence:

1. **The core premise is false: `.github/` contains build logic, so a
   "`.github`-only" diff CAN change the artifact.** A cache key that hashes
   the source tree but excludes `.github/` would serve a *stale* artifact
   whenever a CI change actually altered the build — the worst failure mode
   for a release pipeline (wrong binary shipped). Concrete counterexamples in
   this repo:
   - `macos-build` pins the Qt version *inline* in `release.yml`
     (`jurplel/install-qt-action` `version: '6.11.0'`). A Qt bump is a
     `.github`-only diff that changes the artifact.
   - The toolchain is pinned inside `.github/actions/setup-linux-build` and
     `.github/actions/setup-windows-cross` (Qt / qpdf / mingw-w64 / ORT).
     Editing either is a `.github`-only diff that changes the artifact.
   - The cmake flags / build commands themselves live in `release.yml` steps.
   These are not mechanically separable from "trivial" trigger/ordering edits
   by path, so the `.github`-exclusion heuristic is unsound.

2. **A zero-correctness-risk mechanism for the stated goal already exists.**
   `release-publish.yml` decouples build from publish: it finds a prior
   *successful* `Release` run for a commit
   (`gh run list --workflow=Release --commit=$SHA --status=success`) and
   downloads that run's artifacts (`actions/download-artifact --run-id=…`)
   rather than rebuilding. If a maintainer's only change is CI config, the
   source-identical commit that already has a green Release build can simply
   be the one that gets tagged/published — the CI-config change does not need
   to ship inside the release artifact anyway. This is the correct answer to
   "don't rebuild the mac app for a CI-only change," with no cache and no
   staleness window.

3. **`actions/cache` is not configured for artifacts today, and the budget is
   contended.** The only caching in `ci.yml` / `release.yml` is
   `hendrikmuhs/ccache-action` (compiler-object cache) plus
   `install-qt-action`'s Qt-download cache — no `actions/cache` of build
   outputs anywhere. GitHub's cache is ~10 GB/repo, LRU-evicted, and ccache
   already spends several 500 MB keys of it. Bundled per-OS artifacts (Qt
   frameworks inside the `.dmg` / `.zip`) are tens–hundreds of MB each;
   caching them would risk evicting ccache entries and slowing the *common*
   case (ordinary source changes) to accelerate the *rare* one.

4. **ccache already covers linux/windows; the one job that would benefit has
   the correctness hole.** `linux-build` and `windows-cross` use
   `ccache-action`, so a source-unchanged re-run is already cheap there
   (object-cache hits → relink only) — an artifact cache adds little. Only
   `macos-build` is a full cold rebuild (it has *no* ccache; it builds
   universal static qpdf + universal trailer + macdeployqt from scratch),
   which is exactly the "expensive to rebuild the mac app" the owner cited —
   but macOS is also where the inline-Qt-version staleness hole (point 1)
   bites hardest.

5. **Wiring cost.** A correct implementation would add, to all three build
   jobs, a source-hash compute step, a cache restore, a conditional
   "skip-build-and-re-emit-artifact" branch, and a guarantee that
   `upload-artifact` still runs so `release-publish.yml` can find the output —
   meaningful conditional complexity in a release-critical workflow, for the
   rare-case payoff.

### Cheaper, correct alternative (if the macOS cost is still felt)

Add `hendrikmuhs/ccache-action` to `macos-build` (the actually-expensive,
currently-uncached job), reusing the pattern already proven green on
`linux-build` / `windows-cross`. That speeds **every** macOS build — including
ordinary source changes, not just `.github`-only re-runs — stays fully
correct (ccache is content-addressed, never stale), and needs no
skip-logic or artifact-cache plumbing. Recommend filing that as its own
backlog item if the owner wants to pursue the cost reduction; it directly
addresses the stated motivation without the staleness risk of the
artifact-reuse approach.

## Provenance

Owner message, 2026-07-12.
