---
id: 2026-08-05-macos-hosted-uat-recognize-text-segv
title: test_uat_recognize_text SEGFAULTs on GitHub-hosted macos-14 but not on the self-hosted Mac
priority: TBD
status: open
source: nightly dry runs dispatched while migrating the nightly macOS lane to macos-14 (PR "ci: move the nightly macOS lane to a GitHub-hosted macos-14 runner")
created: 2026-08-05
---

## Threshold

Three consecutive **`macos-14`** nightly runs complete the macOS `ctest -L uat`
step with `test_uat_recognize_text` passing and `crashed=false` in the lane's
`uat-summary.json` — i.e. the macOS row of the nightly release body reads
`✅ build+unit+UAT (44/44)` rather than `🔥 macOS UAT CRASHED (43/44)`.

A single green hosted run does **not** close this: the whole point of the
observation is that the same commit is clean on one Mac and crashes on
another, so intermittency is the expected shape. Reproduce with a dispatched
dry run against the branch under test:

```
workflow_dispatch nightly.yml (ref=<branch>) with dry_run=true force=true
```

then read the macOS lane's `The following tests FAILED:` block.

## Context

Measured back-to-back on the **same commit** while moving `nightly-macos` off
the owner's laptop VM:

| macOS runner | macOS `ctest -L uat` | crashed |
|---|---|---|
| `[self-hosted, macOS, ARM64]` (M4 laptop VM), run [31018077661](https://github.com/programmerq/trailer/actions/runs/31018077661) | **44/44** | `false` |
| `macos-14` (GitHub-hosted, 3-core M1 / 7 GB), run [31018708642](https://github.com/programmerq/trailer/actions/runs/31018708642) | **43/44** | `true` |
| `macos-14`, repeat run [31020325093](https://github.com/programmerq/trailer/actions/runs/31020325093) | — | `true` |

**Reproduced 2/2 on hosted**, 0/1 on the laptop VM. Both hosted runs' UAT
ratchet verdict is `macOS UAT: worse (crash-appeared)`. The first run's ctest
summary names the test:

```
The following tests FAILED:
	 81 - test_uat_recognize_text (SEGFAULT)                uat
```

Notes that shape the diagnosis:

- The **unit** tier is unaffected on the same hosted run — `ctest -LE uat`
  passed in 19 s, so `test_ocr_window` (the subject of
  `2026-08-03-macos-nightly-ocr-window-segv-confirm`) did **not** crash here.
  This is a different binary in the same OCR/ML neighbourhood.
- The DMG is **not** withheld by this. The macOS lane's UAT step is
  `continue-on-error: true` by owner decision (2026-07-26), so the artifact
  staged and uploaded normally; what the crash moves is the UAT ratchet's
  verdict (`worse (crash-appeared)`), which reds the nightly RUN's signal for
  one night and then reads `same` once the baseline records `crashed: true`.
- **Most likely a latent race that slower hardware exposes, not a
  hosted-runner-specific bug.** `macos-14` is 3-core M1 / 7 GB against the
  laptop VM's M4; every other item in this family
  (`2026-08-03-load-sensitive-offscreen-test-races`,
  `2026-08-03-macos-nightly-ocr-window-segv-confirm`) is a worker→GUI teardown
  or timing race that only fires under contention. If that is what this is,
  the hosted lane is *better* signal, not worse — it reproduces something the
  fast laptop hides.
- That interpretation is **unconfirmed**. It needs the faulting thread's stack
  (`~/Library/Logs/DiagnosticReports/`, or an `lldb`/core-dump step added to
  the macOS lane) to say whether this is the same `QPointer`-after-teardown
  window `MlScheduler::postResultToGuiThread()` was introduced to close, or
  something else entirely.

## Why this is filed rather than fixed in the migrating PR

The migration PR changes no source and cannot fix a memory-safety bug in the
OCR path; blocking a runner move on an unrelated crash fix would be the wrong
trade. The crash is also **not a regression the migration introduces** — it is
an existing condition of the code that a different machine reveals. Filed so
it cannot silently become "how the macOS lane always looks."

## Related

- `2026-08-03-macos-nightly-ocr-window-segv-confirm` — the unit-tier
  `test_ocr_window` SIGSEGV on macOS. Same OCR/ML worker→GUI teardown
  neighbourhood, different binary and different tier. Close them together if
  one stack turns out to explain both.
- `2026-08-03-load-sensitive-offscreen-test-races` — the load-sensitive
  offscreen flakes (timing assertions, not memory safety). Same
  "only-under-contention" shape.
- `2026-07-26-macos-uat-triage` — the macOS UAT triage item that made this
  lane non-gating in the first place. Its file is no longer in
  `docs/backlog/` (closed), but `nightly.yml`'s "UAT suite" step and two
  sibling items still cite it by id; named here for the same reason.
