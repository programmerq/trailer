---
name: trailer-windows-cross-wine-ci
description: Dockerless Windows cross-compile + Wine unit-test CI job proven GREEN on trailer-k8s (PR #43, 2026-07-11) — replaces the skipped native-Windows coverage; recipe + runner-image notes; follow-up (070e89a) folded release.yml onto the dockerless path, added auto-detect setup actions + custom trailer-runner image; PR #44 baked ccache + node24 action bumps + apt-index fix + Wine perf-exclusion
metadata:
  type: project
---

# Windows cross-compile + Wine tests on trailer-k8s — WORKS (PR #43)

On 2026-07-11 a Dockerless Windows cross-build + Wine unit-test tier was proven end-to-end and went **green in live CI** on the self-hosted `trailer-k8s` runners. Branch `ci/windows-cross-wine`, draft **PR #43** (https://github.com/programmerq/trailer/pull/43), base `main`. This replaces the "completely skipped" Windows coverage (the native MSVC `build-and-test-windows` job stays `if: false`, untouched, as the eventual native path).

## What was added (2 new files, purely additive)
- `.github/actions/setup-windows-cross/action.yml` — composite action, NO Docker.
- `windows-cross-build` job in `.github/workflows/ci.yml` (`runs-on: trailer-k8s`), `permissions: contents: read`, ccache with a **separate** key (`windows-cross`).

## Proven recipe (all Dockerless — the k8s pods have NO Docker daemon)
- **mingw:** apt `gcc/g++-mingw-w64-x86-64-posix` (GCC 13.2); select the `-posix` alternative.
- **Qt for Windows:** aqtinstall arch **`win64_mingw`**, version **6.10.3** (NOT 6.11 — aqt can't do 6.11's Windows layout; Windows 6.10.3 vs Linux 6.11.0 skew is expected). Also install host **`linux_gcc_64`** for moc/rcc/uic.
- **ONNX Runtime:** NuGet `Microsoft.ML.OnnxRuntime` **1.25.0** -> assemble `lib/onnxruntime.{lib,dll}` + `include/`, fed via `-DFETCHCONTENT_SOURCE_DIR_ONNXRUNTIME_PREBUILT=`. DLL sha256-pinned (`581b3c87f7944a3cdeeaa3dd048e3f4261aa4dc777a57ea154674ece1d04c575`) since the FetchContent override bypasses `cmake/OnnxRuntime.cmake`'s hash.
- **qpdf 12.3.2 + libjpeg-turbo 3.0.3:** build from **git-clone of the tag** (GitHub release-asset downloads 403 through the egress proxy; `git clone` from github.com works). Pin `PKG_CONFIG_LIBDIR` to the mingw sysroot; keep the Dockerfile's `CRT_glob.o` + `if(MINGW)` handling.
- **trailer build:** reuses the repo's existing `cmake/toolchain-mingw-w64.cmake` + `scripts/build-windows.sh` (taught to also bundle `onnxruntime.dll` from `ORT_WIN_DIR`/exe-dir — the previous bundle silently dropped it).
- **Wine tests:** `ctest -C Release --label-exclude uat` under `CMAKE_CROSSCOMPILING_EMULATOR=wine` + `QT_QPA_PLATFORM=offscreen`; stage runtime DLLs + `imageformats/`+`platforms/` plugins + `Qt6Test.dll` **next to the test exes** (WINEPATH is NOT honored on this runner). Exclude UAT (window-geometry asserts need a real windowed platform; the Linux fast loop excludes it too).

## Live CI result (run 29151902215, head 001e707)
- `windows-cross-build`: **SUCCESS**, ~24m wall on cold cache (ccache 0/134 — first run of the key; warm runs faster). Setup step ~19m (Qt download + qpdf/libjpeg from-source). Wine: **`100% tests passed, 0 tests failed out of 26`** (~169 s). Artifact `trailer-windows-x86_64` (25 files, 15 DLLs incl. onnxruntime.dll, ~37 MB) uploaded. No OOM, no egress failure.
- `build-and-test` (Linux): SUCCESS ~6m45s, unaffected.

## Runner-image / owner notes
- **Wine + i386 multiarch** installed fine via apt-per-run but is the heaviest per-run cost — the action exposes an `install-wine` input (default true) so a runner image that bakes Wine in can flip it false. Recommend preinstalling Wine (+i386) on the trailer-k8s image if this runs often.
- Qt (~2.6 GB), the assembled ONNX dir, and the qpdf/libjpeg built prefixes are cache-keyed (deps key includes the mingw compiler version) so warm runs skip the slow setup.
- **Open follow-ups (owner calls, noted in PR):** (b) This ~24m-cold job runs on every PR + push to main — consider gating to nightly/label before making it a required check. (c) Whether to bake Wine into the image (above).

## Follow-up (2026-07-11, PR #43 head 070e89a — GREEN)
- `release.yml`'s `windows-cross` job was **FOLDED** onto the dockerless `setup-windows-cross` action (no more `docker build`); it now also runs the Wine tests. Artifact hand-off to `release-publish.yml` (`dist/trailer-windows-x86_64.zip`, `trailer.exe` at zip root) verified intact. Release Stage step uses `7z a -tzip` (NOT `zip`, which isn't installed).
- Both `setup-windows-cross` and `setup-linux-build` now **AUTO-DETECT** preinstalled tools and skip apt when present; `install-wine` input default is now `auto`. `libqpdf-dev` detection is version-guarded (skip only if >= 11, else install — jammy's qpdf 10.x lacks `QPDFFormFieldObjectHelper::isChecked()`).
- New custom runner image: `docker/runner/Dockerfile` `FROM ghcr.io/actions/actions-runner:2.335.1@sha256:08c30b0a7105f64bddfc485d2487a22aa03932a791402393352fdf674bda2c29` (VERIFIED Ubuntu 24.04 noble → qpdf 11.9, so the Linux link is safe; base pinned to avoid an unreviewed `:latest` regression). Bakes the full Linux+Windows-cross apt toolchain (cmake, ninja, mold, build-essential, mesa/OpenGL dev, libqpdf-dev, mingw-w64-x86-64-posix, wine64+wine+i386, python3-pip, curl, etc.). Published by `.github/workflows/build-runner-image.yml` to `ghcr.io/programmerq/trailer-runner:{latest,YYYYMMDD}` on `ubuntu-latest` (GitHub-hosted — building an image needs a Docker daemon the k8s pods lack), triggers push-to-main/dispatch/weekly, `permissions: contents:read + packages:write`.
- Owner adoption = one-line ARC change: `image: ghcr.io/programmerq/trailer-runner:latest` (documented in `docs/ci/custom-runner-image.md`). NOT yet deployed — image publishes when the Dockerfile lands on main or via manual dispatch.
- CI proof: PR #43 run 29160608020 (head 070e89a) **GREEN** — windows-cross-build success ~15m (Wine 26/26, ~35 MB artifact), Linux build-and-test success ~7m41s with the edited setup action clean.
- Owner also decided: keep the windows-cross job on **EVERY PR/push** (no nightly/label gating).

## Custom runner image adopted + PR #44 hardening (2026-07-11, GREEN)

The owner switched the trailer-k8s ARC runner-set to the custom `ghcr.io/programmerq/trailer-runner` image. First live exercise surfaced fixes, all landed on PR #44 (branch `ci/runner-image-ccache`, head f654208, CI run 29166427585 green — windows-cross ~8m with ccache 133/134 hits, Wine 26/26, Linux green):

- **Custom-image apt-index HAZARD (important):** the Dockerfile does `rm -rf /var/lib/apt/lists/*` (standard hygiene), so the deployed image has an EMPTY apt index. Any step that runs `apt-get install` WITHOUT a preceding `apt-get update` fails with `E: Unable to locate package <x>`. Our composite setup actions are safe (they apt-update-first or auto-detect+skip). The offender was the third-party `hendrikmuhs/ccache-action`, which defaults `update-package-index: false` → it flaked on a pod that lacked baked ccache. FIX: set `update-package-index: true` on all four ccache-action steps (ci.yml x2, release.yml x2). GENERAL RULE for this image: any third-party action that apt-installs at runtime must be given its update-index option, or the tool must be baked into the image.
- **Passwordless sudo WORKS** on the actions-runner base image (`$(which sudo) apt-get ...` ran fine) — refuted as a failure cause.
- **ccache baked** into `docker/runner/Dockerfile` (4.9.1); once the image is republished the ccache-action auto-detects `/usr/bin/ccache` and skips apt entirely (proven on PR #40's pod, which had the baked image: `ccache 4.9.1` detected, apt skipped).
- **Rollout consistency:** during the swap, pods were MIXED (some on the ccache-baked image, some not) — a job could pass or fail depending on which pod it landed on. After merging the image change, confirm the whole ARC set rolls onto the new image.
- **node20 -> node24 action bumps** (Node 20 runtime deprecation): `hendrikmuhs/ccache-action@v1.2.23`, `actions/cache@v5`, `docker/login-action@v4`, `docker/setup-buildx-action@v4`, `docker/build-push-action@v7`. Left `ilammy/msvc-dev-cmd@v1` (no node24 release exists; only in the disabled MSVC job). checkout@v6 / upload-artifact@v7 were already node24. `cache@v5` needs Actions Runner >= 2.327.1 (satisfied — runs report node24 default).
- **Wine tier excludes `perf` label:** the windows-cross Wine ctest steps now use `--label-exclude 'uat|perf'` (was `uat`) in BOTH ci.yml and release.yml. Wall-time perf tests (e.g. `test_perf_paint_budget`) are noise under the Wine emulator on the slow Xeons; they still run on the native Linux job. Aligns with [[trailer-perf-measurement-ruling]] (no CI wall-time gate). The `perf` ctest label was added by the #40 batch (commit e0bed9c); the exclusion is inert until those tests reach main.

Followed [[trailer-review-before-push-policy]] (2 variant-persona local review rounds caught the missing-onnxruntime.dll artifact bug before push). Related: [[trailer-remote-build-recipe]] (Linux recipe), [[trailer-ci-on-k8s-runners]] (runner facts).
