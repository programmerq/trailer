---
id: 2026-07-24-test-image-scale-macos-failure
title: test_image_scale fails on the self-hosted macOS runner (readoutMatchesRenderAfterAsyncFit)
priority: TBD
status: open
source: nightly.yml bootstrap/proof run (PR #121), run 30099999309, macOS lane, job 89503377911
created: 2026-07-24
---

## Threshold

`ctest -C Release -L uat --output-on-failure` is unaffected (see Context —
UAT never got a chance to run this pass), but the concrete, checkable gate
for *this* item is: `test_image_scale` exits 0 with 0 failed cases when run
via `ctest` on the self-hosted macOS runner (`[self-hosted, macOS, ARM64]`),
either through `nightly.yml`'s `nightly-macos` lane or an equivalent local
run on that same machine/OS build. Close this item once that's observed
(attach the passing run/log), or once the test is deliberately restructured
and the restructured form is shown green on macOS.

## Context

First-ever execution of Trailer's unit test suite on macOS in CI —
`scripts/build-macos.sh` previously only built/bundled/packaged the `.app`;
no test binary had ever been run there before `nightly.yml`'s bootstrap
proof run (2026-07-24, PR #121, run
[30099999309](https://github.com/programmerq/trailer/actions/runs/30099999309),
job `89503377911`). Result: 61 of 62 unit tests passed; only `test_image_scale`
(test #27) failed. This is a genuine platform-specific product/test finding,
not a pipeline defect — the nightly lane's build, Configure, and every other
test binary behaved correctly.

Because `nightly-macos`'s "UAT suite" step defaults to `if: success()` and
the preceding "Unit tests" step exited 8 (ctest's failed-test-count exit
code), **the UAT suite step never ran this pass** — its status on macOS is
still unknown from this run, not a separate failure.

Failing case, quoted verbatim from the job log:

```
FAIL!  : TestImageScale::readoutMatchesRenderAfterAsyncFit() 'indicator->text() == expected' returned FALSE. (readout '35%' must match render 36% after async fit)
   Loc: [/Users/runner/actions-runner/_work/trailer/trailer/tests/test_image_scale.cpp(496)]
```

```
Totals: 21 passed, 1 failed, 0 skipped, 0 blacklisted, 2852ms
Config: Using QtTest library 6.11.0, Qt 6.11.0 (arm64-little_endian-lp64 shared (dynamic) release build; by Apple LLVM 16.0.0 (clang-1600.0.26.3)), macos 26.5.2
```

Run under `QT_QPA_PLATFORM=offscreen` (as `nightly-macos`'s "Unit tests" step
sets), on Apple Silicon, Qt 6.11.0.

For context (not a claimed root cause): this project already has one
documented instance of `test_image_scale` being sensitive to the platform's
DPR/rounding path — `ci.yml`'s `wayland-smoke` job header notes it "fails on
fractional-scaling rounding (real-compositor DPR vs offscreen dpr=1)" under a
*real Wayland compositor*. This macOS failure is under `offscreen`, same as
the passing Linux/Windows/CI runs, so it is not obviously the same mechanism
— it may be a macOS-offscreen-specific rounding/timing quirk in the
async-fit readout path (`readoutMatchesRenderAfterAsyncFit`, a 35%-vs-36%
one-tick mismatch), or an unrelated timing race. Undetermined — **out of
scope for this item to root-cause or fix**; this item exists to track and
close the finding, not to diagnose it.
