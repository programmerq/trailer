---
name: trailer-ci-on-k8s-runners
description: Trailer CI on self-hosted trailer-k8s runners (2026-07-09): migrated + validated GREEN (26/26 tests) after adding cmake + resizing pods; UPDATE 2026-07-13 — cold-cache Linux build now exercised (PR #50) and POD-DIES ~11min into Build (cluster memory-limit/eviction, NOT repo-fixable)
metadata:
  type: project
---

# Trailer CI on self-hosted `trailer-k8s` runners

On 2026-07-09 all 9 Linux CI/release jobs across the 5 `.github/workflows/*` were switched to `runs-on: trailer-k8s` (self-hosted, Linux-only k8s runner set); the Windows MSVC job (`ci.yml` `build-and-test-windows`) was disabled with `if: false` + a dated note; the macOS job (`release.yml` `macos-build`, `macos-14`, 10x multiplier) was left untouched. Pushed directly to `main` (owner-authorized live test): commits `aa4a87c` (migration), `c384b8e` (add cmake), `c3bdc72` (timeout 18->40m).

## Runner validation results
- `trailer-k8s` runners ARE online; jobs get picked up within ~1-2s (no label mismatch). Runner names look like `trailer-k8s-gg4rs-runner-XXXXX`.
- `clang-format` job: passes reliably (~1-2 min).
- `build-and-test` (Linux build + unit tests): setup-linux-build (Qt 6.11 install ~5-6 min), ccache, and CMake **Configure all pass**. Build + unit tests have NOT yet completed on the runner.

## Runner-image gaps
- **cmake was MISSING** from the trailer-k8s image (`cmake: command not found`, exit 127). Fixed in-workflow by adding `cmake` to the `apt-get install` in `.github/actions/setup-linux-build/action.yml` (`mold` + `ninja-build` were already there). See [[trailer-remote-build-recipe]] for the full toolchain the build needs.
- **Pod dies during the cold-cache Build step — RESOLVED** — On 2026-07-09 the runner pod died mid-Build (OOM/eviction: "lost communication", `BlobNotFound` logs) during the cold `cmake --build --parallel`. The owner then **INCREASED the trailer-k8s pod resource sizes**, and a re-run (run `29044461861` attempt 3) on commit `c3bdc72` then **PASSED**: build-and-test SUCCESS with 26/26 ctest unit tests passing (0 failed), clang-format SUCCESS, both on trailer-k8s runners (e.g. `trailer-k8s-8nzw2-runner-z4qzd`), no OOM. **CAVEAT:** that green Build hit a 99% warm ccache (133/134 hits) so it was effectively a warm build — a genuine cold-cache full compile (ccache cleared, or a Qt/dependency bump) has not been re-exercised on the resized pod, though the extra RAM should now cover it. Hardware: old 12+yr Xeon, lots of RAM, slow shared low-power cores.

## Not-yet-exercised gaps (release/dispatch workflows only)
- `release.yml` `windows-cross` + `uat` and `uat-dispatch.yml` `uat` all assume a **Docker daemon** the ephemeral k8s pod likely lacks (no DinD). Also verify `gh` CLI, passwordless sudo/apt (Ubuntu-noble base), and a C++20 gcc on the image.

## Cold-cache Linux build POD-DIES — hard data (2026-07-13, PR #50) — SUPERSEDES the "cold-build not re-exercised" caveat above
The 0.2.0→0.3.0 release matrix (PR #50, branch `release/0.3.0`) **exercised the cold-cache Linux build on the resized `trailer-k8s` pods for the first time**, and it **POD-DIED 4 consecutive times**: Release runs #84 / #85 / #86, plus run `29214602715` attempts 1 & 2. Identical signature every time:
- `linux-build` "Build" step stuck `in_progress`; job `conclusion: failure`.
- `get_job_logs` → **HTTP 404** (runner pod killed before flushing logs).
- runner `trailer-k8s-k8vnf-runner-*` **vanishes ~11–11.6 min into the Build step** (~14–15 min total wall).

**Key diagnostic:** the ~11-min death wall is **CONSTANT and independent of ccache warmth** — warming the cache across attempts did NOT move it — and there is **NO OOM log / no exit 137** (the *pod* is killed, not the compiler). So this is a **pod memory-LIMIT or node eviction (cluster/ARC config), NOT a build-parallelism/OOM issue fixable in the repo.** Evidence the repo can't fix it:
- A `-j2` parallelism cap (`CMAKE_BUILD_PARALLEL_LEVEL=2`, added to `release.yml`/`ci.yml`) did **NOT** help.
- Run #85 pod-died during the `cmake` **Configure** step (near-zero memory), reinforcing node-level eviction over compiler OOM.

**Same cold SHA, all other release-matrix jobs PASS on healthy runners:** `windows-cross` (Dockerless mingw + Wine), `uat` (docker build + `ctest -L uat`), `precheck`, and `macos-build` (GitHub-hosted macos-14). So the death is **specific to the linux-build pod under cold full-compile load**, not a workflow/SHA problem.

**Fix direction:** raise the trailer-k8s pod memory **request/limit** (owner is provisioning capacity as of 2026-07-13) and/or check pod-lifetime / eviction (MemoryPressure) settings. Confirming the exact kill reason (OOMKilled=137 vs Evicted) needs **cluster-side** `kubectl get events` / pod status — it is NOT visible from the GitHub Actions API (logs 404). Once headroom lands, re-run ONLY the failed `linux-build` job (no new commit). See [[trailer-030-release-pr50]] for the release-work state blocked on this.

**RESOLVED (2026-07-13):** the **owner added a larger k8s node** — a Hyper-V VM on his gaming PC with enough RAM/CPU — and the cold `linux-build` then **PASSED** (which un-gated + passed `macos-build`; release run `29214602715` attempt 6 fully green). **Root cause: node resource capacity for the cold full-compile** — NOT a time-based/pod-lifetime kill (the "constant ~11-min wall" was a symptom of the undersized node, not a hard eviction timer).

## Trigger facts (verified 2026-07-10)
- `ci.yml` fires ONLY on `push: branches [main]` and `pull_request: branches [main]`. A plain push to any other branch triggers ZERO workflow runs (verified: pushed branch commit b9fd757 produced no run). To exercise CI from a feature branch you must open a PR targeting main.
- Consequence for handoffs: "push the branch as a CI test" does not work on this repo.
