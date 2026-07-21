---
name: trailer-030-release-pr50
description: 0.3.0 release SHIPPED — PR #50 (branch release/0.3.0, HEAD f48f99c) MERGED 2026-07-13 ~08:32Z as merge commit 84efa437 (head f48f99c preserved), merged_by programmerq; release-autotag tagged v0.3.0 at f48f99c, release-publish PUBLISHED the GitHub Release (draft=false, 3 assets, [0.3.0] CHANGELOG in body). Release COMPLETE. FOLLOW-UP: release-body/CHANGELOG install-notes reference a Windows .msi and Linux .deb/.rpm that are NOT built/attached (only tar.gz/dmg/zip) — reconcile packagers-vs-notes (owner asked re: reconcile PR, pending)
metadata:
  type: project
---

# Trailer 0.3.0 release — PR #50 (release candidate)

**PR #50** (https://github.com/programmerq/trailer/pull/50), branch `release/0.3.0`, is the 0.3.0 release candidate. Head SHA `f48f99c`. All commits authored `Jeff Anderson <jefferya@programmerq.net>`.

## Commits on the branch (HEAD-last)
- `01db70a` **Release 0.3.0** — VERSION 0.2.0→0.3.0 + dated CHANGELOG reconciled against the 97 commits since `v0.2.0`; packaging metadata (metainfo / .wxs / .deb / .rpm) all brought to 0.3.0.
- `104ec3d` **-j2 parallelism cap** — `CMAKE_BUILD_PARALLEL_LEVEL=2` in `release.yml`/`ci.yml` (did NOT fix the pod-death; see [[trailer-ci-on-k8s-runners]]).
- `e180629` Inspector scroll-arrow min-size.
- `f48f99c` (**HEAD**) gate `macos-build` behind linux+windows succeeding + exempt Qt `QTabBar` scroller chrome from the UAT layout sweep.

## Release flow (label-driven — how a release ships)
- PR carries the **`release-candidate` label** → Release workflow matrix fires:
  - `precheck` gates on VERSION being release-ready + **no existing `v0.3.0` tag**.
  - then `linux-build` / `windows-cross` / `uat` on **trailer-k8s** + `macos-build` on **macos-14** (now gated behind linux+windows).
- On merge (**MUST preserve PR HEAD — NO squash**), `release-autotag` tags PR-HEAD as **`v0.3.0`** and dispatches `release-publish`, which attaches the matrix artifacts + splices the CHANGELOG `[0.3.0]` section.

## Current state (2026-07-13 ~08:32Z) — RELEASED / SHIPPED ✅
- **PR #50 MERGED** at ~08:32Z as a **merge commit `84efa437`** (head **`f48f99c` preserved in main** — no squash/rebase, as required), **merged_by programmerq**.
- **`release-autotag` SUCCEEDED** → tag **`v0.3.0` created at `f48f99c`**.
- **`release-publish` SUCCEEDED** → **GitHub Release PUBLISHED** (draft=false) at https://github.com/programmerq/trailer/releases/tag/v0.3.0 with **3 assets**:
  - `trailer-0.3.0-linux-x86_64.tar.gz`
  - `trailer-0.3.0-macos-arm64.dmg`
  - `trailer-0.3.0-windows-x86_64.zip`
  - and the **`[0.3.0]` CHANGELOG** spliced into the release body.
- **STATUS: 0.3.0 release is COMPLETE / SHIPPED.**

## Follow-up (NEW) — reconcile install-notes vs. actual artifacts
- The release body install notes reference a Windows **`.msi`** and Linux **`.deb`/`.rpm`** that are **NOT built/attached** — only `tar.gz` / `dmg` / `zip` ship.
- **Fix options:** either (a) wire those packagers (`.msi`, `.deb`, `.rpm`) into the release matrix / `release-publish` upload, or (b) trim the release-body / CHANGELOG install-notes text to match the 3 actual artifacts.
- **Owner was asked whether to open a reconcile PR — his answer is pending.**
- Belongs on [[trailer-followup-docket]].

### Update 2026-07-13 18:35Z — reconcile PR #52 MERGED
- **PR #52 MERGED into `main`** via a **SHA-preserving merge commit `a4abbcf`**, **merged_by programmerq**.
- Landed on main: the **`release-publish.yml` honest install-notes fix** (release body now describes **only** the real `tar.gz` / `dmg` / `zip` assets) + the **`docs/backlog/2026-07-13-wire-msi-deb-rpm-packagers.md`** backlog item (option (a) deferred, tracked).
- **REMOVED from #52** per owner: the trailer-k8s pod-kill backlog item ("cruft, don't persist in repo").
- **REMAINING open item:** the **already-published v0.3.0 GitHub Release BODY** still over-promises `.msi`/`.deb`/`.rpm` (only **3 real assets** attached). Needs a **manual owner edit** — this session has no release-edit tool. **Future releases are already correct** via the merged source fix, so this is a one-time live-body cleanup.

## How it shipped (historical — for reference)
1. Merged with a **MERGE COMMIT (NOT squash, NOT rebase)** so `release-autotag` could tag the PR **head SHA `f48f99c`** (the built artifacts' SHA).
2. On merge, `release-autotag` tagged **`v0.3.0` at `f48f99c`** and dispatched `release-publish`, which attached the artifacts + spliced the CHANGELOG `[0.3.0]` section into the GitHub Release.

## Related
- Backlog item `2026-07-12-release-uat-before-build` (full UAT-first, fail-cheap job ordering) remains **open**; the macOS gate added here partially implements its spirit.
- [[trailer-ci-on-k8s-runners]] — the pod-death diagnosis this release is blocked on.
- [[trailer-verify-remote-after-push]] — verify remote CI after pushing.
- [[trailer-review-before-push-policy]] — pre-push local review gate.
