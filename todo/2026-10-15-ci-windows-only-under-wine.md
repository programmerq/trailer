# CI tests Windows only under Wine, and most of CI guards process, not the product

Priority: P1. Tags: `packaging`, `infra`.

- Done when:
  - every PR builds and runs the unit tests natively on Linux, Windows (MSVC) and macOS, and
    UAT on Linux; release UAT runs on all three;
  - the Wine and mingw lanes, the unused runner image, `dev-build.yml`, the Docker UAT, the UAT
    ratchet and the release-script self-tests are gone, along with their workarounds
    (`kWineSkip`, the configure-time font directory);
  - the gating lanes build the shipped configuration (recorder off);
  - main stays green on 10 consecutive pushes, and a re-run never flips a result.
- Src: about 4,400 lines of workflow YAML for about 2,300 lines of logic. The disabled MSVC job
  (`ci.yml:506-596`) passed 36 of 37 runs before it was turned off; the Wine lane fails about 1
  run in 3 and Wine UAT passes about half its cases. As of 2026-10-15, main's last CI run is
  red from a Wine flake. `tests/test_ml_callback_lifetime.cpp:271`; `tests/CMakeLists.txt:25`;
  `tests/uat/CMakeLists.txt:19`.
