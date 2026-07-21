---
name: chrono-no-prebuilt-dist
description: No usable prebuilt Project Chrono C++ Linux distribution with Vehicle+Irrlicht exists (verified 2026-07-12); plan is to publish our own pinned GHCR image
metadata:
  type: reference
---

Project Chrono (C++ multibody physics engine) is built from source in CI (`ev1sim/scripts/build_chrono.sh`, pins `release/9.0`, enables Vehicle+Irrlicht, ~20-40 min cold; consumed by `electricsim/.github/workflows/vat-nightly.yml` and `ev1sim/.github/workflows/ci.yml`, cached via actions/cache `~/chrono-install`). ev1sim links Chrono core + Vehicle + Irrlicht + chrono_models (Sedan/HMMWV) via `find_package(Chrono ... COMPONENTS Vehicle Irrlicht CONFIG)`. Irrlicht is skipped at runtime headless but is a compile-time HARD dep (no #ifdef guard in SimApp.h/Telemetry.h/FloatingUiPanel.h).

**Verified 2026-07-12: there is NO maintained, version-pinnable, Vehicle+Irrlicht C++ Linux Chrono distribution to consume as-is.**
- Official `uwsbel/projectchrono` Docker image HAS Vehicle+Irrlicht but is UNMAINTAINED (newest push Oct 2023), tops out at Chrono 8.0 (no 9.0/10.0 tag), Ubuntu 20.04 + CUDA base (~8 GB).
- `conda-forge/chrono` is maintained + pinnable (9.0.1, 10.0.0) but built with Irrlicht/VSG OFF (PyChrono-oriented core) — disqualified.
- Official Linux distribution is source-only (no .deb/tarball/GHCR). Release cadence ~1/year (9.0.0 May-2024, 9.0.1 Jul-2024, 10.0.0 Apr-2026).

**Owner's chosen direction (2026-07-12):** publish our own pinned Chrono build. Full hand-off plan (thin public repo → GHCR image + optional release tarball, container:-consumed by both CIs, ABI caveat, Dockerfile/publish.yml stubs) was delivered as a claude.ai artifact. Two quick wins identified: (1) build_chrono.sh does a bare `cmake --build` with BUILD_DEMOS defaulting ON — adding `-DBUILD_DEMOS=OFF -DBUILD_TESTING=OFF -DBUILD_BENCHMARKING=OFF` trims the unused demo compilation (zero code change); (2) dropping Irrlicht from CI needs ev1sim's viz path #ifdef-guarded first. Runners (electricsim-k8s / electricsim-mighty) have Docker/dind available, so a GHA `container:` job is viable.
