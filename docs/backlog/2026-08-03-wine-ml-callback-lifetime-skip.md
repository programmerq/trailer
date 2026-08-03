---
id: 2026-08-03-wine-ml-callback-lifetime-skip
title: Re-enable test_ml_callback_lifetime on the Wine lane
priority: TBD
status: open
source: PR #146 CI — two identical Wine failures with zero captured output
created: 2026-08-03
---

## Threshold

The Wine `QSKIP` at the top of `TestMlCallbackLifetime::init()`
(`tests/test_ml_callback_lifetime.cpp`) is deleted, and the Wine unit lane
(`ctest -C Release --label-exclude 'uat|perf'`) passes `test_ml_callback_lifetime`
on **three consecutive** runs.

Deleting the skip without a real diagnosis does not meet this — the
re-enabling PR must state what the failure actually was, from captured
output, and either fix it or show it was a Wine artifact that has since gone
away.

## Context

`tests/test_ml_callback_lifetime.cpp` is the regression guard for the
2026-08-03 macOS nightly SIGSEGV (an ML worker posting its result into a
freed `MainWindow`). It forces the post-then-destroy ordering deterministically
and, against the unfixed tree, SIGSEGVs 30/30 on Linux.

On the Wine lane it fails, and **nobody can say why**:

| run | commit | result | captured output |
|---|---|---|---|
| [30828061748](https://github.com/programmerq/trailer/actions/runs/30828061748) | `6eb71c71` | `***Failed` 1.38 s | **zero bytes** |
| [30829364568](https://github.com/programmerq/trailer/actions/runs/30829364568) | `d4031c1b` | `***Failed` 1.43 s | **zero bytes** |

It is the only failure in 65 both times; every other unit test passes,
including `test_quit_and_keep_windows`, which also builds and destroys
`MainWindow`s. The second run added unbuffered stdio (`setvbuf`) and
Wine-gated, explicitly-flushed phase markers — **the signature did not
move**: no QtTest banner, no `FAIL!`, no marker. So the failure is not merely
an assertion whose message was lost; either the process dies before `qExec`,
or this binary's stdout never reaches ctest under Wine at all.

This is the same opaque signature already recorded at scale in
[`2026-07-24-wine-uat-failures-triage`](2026-07-24-wine-uat-failures-triage.md):
**21 UAT tests** failing under Wine with zero captured output, whose own
conclusion is *"the first action is not to fix 21 tests; it is to make them
legible."* That item is therefore the blocker for this one — this test cannot
be diagnosed before Wine failures stop swallowing their output.

Reproduction is not available locally: it needs a Windows cross-build
environment (mingw toolchain, Qt-for-Windows, qpdf, ONNX Runtime, a Wine
prefix) that the dev boxes do not carry, so every attempt costs a self-hosted
CI cycle — which the project explicitly economises.

### Why skipping is acceptable here

- Wine is a **stand-in for Windows, not a platform Trailer ships to** — the
  standing reason Wine UAT is non-gating (`2026-07-24-wine-uat-failures-triage`
  §Context).
- The defect being guarded is **platform-independent**: a raw pointer posted
  to a `QObject` destroyed on another thread. The guard runs, and fails
  against unfixed code, on Linux (30/30 SIGSEGV) and is covered by the macOS
  nightly.
- It follows the established per-test Wine-skip pattern:
  [`2026-07-19-wine-cross-thread-editor-save`](2026-07-19-wine-cross-thread-editor-save.md)
  and
  [`2026-07-21-wine-keep-restore-file-move-open-handle`](2026-07-21-wine-keep-restore-file-move-open-handle.md).

### What is lost

Windows-specific coverage of the ML-callback lifetime guard. If the
use-after-free had a Windows-only variant, this lane would no longer catch it.
That is the cost being accepted, and it is why this item exists rather than
the skip being left undocumented.

### Leads for whoever picks this up

- The diagnostics are already in the file and stay there: `setvbuf` as the
  first statement of `main()`, a `[boot] main() entered` write immediately
  after, and an unconditional `qInfo()` at the top of `init()` that fires
  **before** the skip. A Wine run that shows the banner + that `qInfo` proves
  the binary starts fine and the fault is in the test body; a run that still
  shows nothing proves it dies before `qExec`. Read the next Wine log before
  theorising further.
- `2026-07-24-wine-uat-failures-triage` §"Note for whoever picks this up"
  records that the shared `HOME`/`XDG_*` sandbox does **not** redirect
  anything under Wine (Windows `QStandardPaths` reads `APPDATA`), so binaries
  share one `%APPDATA%` two-at-a-time under `CTEST_PARALLEL_LEVEL=2`. This
  test mutates a global ML setting (`setMlRunOnBattery`) and depends on
  scheduler queue state, so it is more exposed to that than most.

## Related

- `2026-07-24-wine-uat-failures-triage` — the blocker: Wine failures produce
  no output.
- `2026-08-03-macos-nightly-ocr-window-segv-confirm` — the other open
  follow-up from the same PR.
