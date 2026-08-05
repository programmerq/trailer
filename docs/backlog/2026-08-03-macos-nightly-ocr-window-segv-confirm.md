---
id: 2026-08-03-macos-nightly-ocr-window-segv-confirm
title: Confirm on macOS that the ML-callback lifetime fix closes the test_ocr_window SIGSEGV
priority: TBD
status: open
source: nightly-20260803 run 30815465012 (macOS job 91692075830) + the fix that followed
created: 2026-08-03
---

## Threshold

Three consecutive **macOS** nightly runs complete their gating unit-test step
with `test_ocr_window` passing (equivalently: `ctest -LE uat` exits 0 on
macOS arm64 three nights running), and the nightly release carries **four**
assets including the DMG and a signed appcast.

If `test_ocr_window` SIGSEGVs again on macOS, this item is NOT closed —
capture the crash report (`~/Library/Logs/DiagnosticReports/`) or re-run the
macOS job with a core-dump/`lldb` step, and attach the faulting thread's
stack. A macOS `SEGV_ACCERR` at a small address is a **null-page** access, so
the report distinguishes "null pointer at +offset" from "recycled garbage
pointer" — which the Linux dumps could not.

## Context

The 2026-08-03 macOS nightly failed its gating unit-test step with
`test_ocr_window` SIGSEGV (`SEGV_ACCERR` at `0x50`, after every test function
had already reported PASS). The fix that followed —
`MlScheduler::postResultToGuiThread()`, see `docs/CONVENTIONS.md` §5 and the
`2026-08-03` entry in `TODO.md` — is **confirmed** for the `MainWindow` sites
(four Linux `gdb` dumps naming
`MainWindow::scheduleBackgroundCandidateScore`, plus an ASan report, plus a
deterministic regression test in `tests/test_ml_callback_lifetime.cpp`).

Its effect on `test_ocr_window` specifically is **inference, not
observation**:

- `test_ocr_window` never constructs a `MainWindow`, so the confirmed sites
  are provably not its cause.
- Its only worker→GUI hop is `OcrController::submitPage`'s result post, which
  an instrumented build shows racing document teardown constantly: **171 of
  those posts reached the invoke with an already-destroyed `SelectableTextStore`
  across 300 loaded runs**. The common outcome is safe (the `QPointer` is
  already null, Qt drops the call); the crash requires the destruction to land
  *inside* the post, which is the window the fix removes.
- It does **not** reproduce on Linux: 1200 loaded runs pre-fix, 0 failures.
  So the Linux lane cannot confirm or refute the fix for this test.

Hence: plausible, mechanically sound, unproven. Only a macOS run closes it.

## Related

- `2026-08-03-load-sensitive-offscreen-test-races` — the other load-sensitive
  offscreen flakes from the same hunt (timing assertions, not memory safety).
- `2026-08-05-macos-hosted-uat-recognize-text-segv` — **a second SIGSEGV in
  the same OCR neighbourhood, in the UAT tier rather than the unit tier**
  (`test_uat_recognize_text`), reproducible 2/2 on GitHub-hosted `macos-14`
  while the unit tier stayed green on that same run. If a captured stack
  explains both, close them together — and note that a *reproducible* hosted
  crash is a far cheaper confirmation vehicle than waiting three nights on the
  laptop, which is what this item's threshold currently requires.
- `2026-07-26-macos-uat-triage` — the standing macOS-specific triage item.
