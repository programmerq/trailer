---
name: chrono-simd-march-native-sigill
description: Project Chrono release/9.0 forces -march=native via FindSIMD.cmake when USE_SIMD=ON, which on a CPU-blind cross-runner cache causes SIGILL crashes in Chrono-linked tests; fix is USE_SIMD=OFF + -march=x86-64-v2 baseline
metadata:
  type: reference
---

Confirmed 2026-07-19 by reading Chrono `release/9.0` CMake source. Root cause of intermittent SIGILL ("ILLEGAL" in CTest) crashes in Chrono-linked unit tests on GitHub `ubuntu-latest`:

- `src/CMakeLists.txt:216` `option(USE_SIMD ... ON)` (default ON) → `:222` `find_package(SIMD)` → `cmake/FindSIMD.cmake:474-498`: on GCC/x86 (non-Apple, non-cross-compile) it UNCONDITIONALLY sets `SIMD_FLAGS = -march=native` (overriding its own SSE/AVX probes) and exports `SIMD_CXX_FLAGS`.
- `src/CMakeLists.txt:282` folds that into `CH_CXX_FLAGS`; `cmake/chrono-config.cmake.in:98` exports it as `CHRONO_CXX_FLAGS`, which ev1sim applies at `CMakeLists.txt:39` `add_compile_options(${CHRONO_CXX_FLAGS})`.
- ev1sim CI (`.github/workflows/ci.yml`) caches the Chrono install (`~/chrono-install`) and ev1sim objects (ccache) with keys on `runner.os` ONLY. So a Chrono built with `-march=native` on a wide-ISA (AVX2) runner is restored onto a narrower runner and SIGILLs. Re-runs do NOT clear it (100% ccache hits replay the same wide binary). Same `scripts/build_chrono.sh` is consumed by the electricsim VAT nightly — same exposure.

Trap: a bare `-march=x86-64-v2` does NOT fix it — with `USE_SIMD=ON`, FindSIMD appends `-march=native` AFTER it and GCC's last `-march` wins. Must set `-DUSE_SIMD=OFF`.

Fix (ev1sim PR #30, `fix/chrono-portable-isa-baseline`, 2026-07-19): `scripts/build_chrono.sh` gets `-DUSE_SIMD=OFF -DCMAKE_C_FLAGS=-march=x86-64-v2 -DCMAKE_CXX_FLAGS=-march=x86-64-v2`; ev1sim `CMakeLists.txt` mirrors `add_compile_options(-march=x86-64-v2)` placed AFTER the CHRONO_CXX_FLAGS line so it wins last. Editing build_chrono.sh auto-rotates the Chrono-install cache key (hashFiles) → one clean cold rebuild. Definitive proof = first cold chrono-smoke build shows no `-march=native`/`-mavx*` in CHRONO_CXX_FLAGS and the ~18 Scenario tests pass.

Scope honestly: this removes cross-runner ISA variance + one numeric-variance source, but does NOT fix runtime FP reduction-order variance from parallel-solver thread scheduling (that determinism question is owned by the abs_split_mu bisect harness). The eventual zero-variance answer is the pinned GHCR Chrono image — see [[chrono-no-prebuilt-dist]].
