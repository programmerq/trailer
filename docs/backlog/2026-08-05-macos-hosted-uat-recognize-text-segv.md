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

A single green hosted run does **not** close this. Three is the same bar
`2026-08-03-macos-nightly-ocr-window-segv-confirm` sets, for the same reason:
this whole family of crashes is contention-sensitive, so one quiet run proves
only that the machine was quiet.

**Before fixing, capture the stack.** The crash is currently classified only
by ctest (`SEGFAULT`); nothing has read the faulting thread. Add an `lldb` /
core-dump step to the macOS lane, or pull
`~/Library/Logs/DiagnosticReports/`, and record whether the fault address is a
null-page offset (`SEGV_ACCERR` at a small address ⇒ null pointer at +offset)
or a recycled-garbage pointer — that is the distinction that says whether this
is the same `QPointer`-after-teardown window
`MlScheduler::postResultToGuiThread()` was introduced to close.

Reproduce with a dispatched dry run against the branch under test:

```
workflow_dispatch nightly.yml (ref=<branch>) with dry_run=true force=true
```

then read the macOS lane's `The following tests FAILED:` block.

## Context

### What was measured

Two dispatched dry runs on `macos-14`, against the self-hosted lane on the
same commit:

| macOS runner | macOS `ctest -L uat` | crashed | failing test |
|---|---|---|---|
| `[self-hosted, macOS, ARM64]` (M4 laptop VM), run [31018077661](https://github.com/programmerq/trailer/actions/runs/31018077661) | 44/44 | `false` | — |
| `macos-14` (hosted, 3-core M1 / 7 GB), run [31018708642](https://github.com/programmerq/trailer/actions/runs/31018708642) | **43/44** | `true` | `test_uat_recognize_text` |
| `macos-14`, repeat run [31020325093](https://github.com/programmerq/trailer/actions/runs/31020325093) | **43/44** | `true` | `test_uat_recognize_text` |

Both hosted runs produced the identical ctest line:

```
The following tests FAILED:
	 81 - test_uat_recognize_text (SEGFAULT)                uat
macOS UAT: 43/44 passed, crashed=true (non-gating)
```

Same test, same count, same signal, twice. `(SEGFAULT)` is ctest's
classification, which is what `nightly.yml`'s crash grep
(`***Exception: SegFault` / `Received signal N (SIG…)`) keys on — so this is
**reproducible on hosted, not an intermittent flake**. The faulting thread's
stack has NOT been captured; that is what the threshold above asks for.

### The history that stops this being "hosted is broken"

The crash flag on this lane is **not new, and not hosted-specific.** Read from
each nightly release's `uat-summary.json` asset — every one of these ran on
the **self-hosted laptop VM**:

| tag | macOS UAT | crashed |
|---|---|---|
| `nightly-20260801` | 40/42 | **`true`** |
| `nightly-20260802` | 41/43 | **`true`** |
| `nightly-20260803` | lane failed | — |
| `nightly-20260804` | lane failed | — |
| `nightly-20260805` | 44/44 | `false` |

The laptop was crashing on this lane as recently as **2026-08-02**, and
`nightly-20260805` is the *first* clean macOS UAT this lane has ever recorded
— one day before these measurements. The self-hosted "clean" baseline is two
samples old (that nightly plus the dry run above).

So the honest framing is **not** "hosted macOS is broken and the laptop is
fine." It is:

> A crash on this lane is a recurring, pre-existing condition that had *just*
> stopped appearing on the fast M4. The slower 3-core hosted box still
> reproduces it, deterministically.

That is consistent with — and is further evidence for — the
contention-sensitivity already filed for this code:
`2026-08-03-load-sensitive-offscreen-test-races` records tests that are 0/30 on
an idle machine and only fail under load, and
`2026-08-03-macos-nightly-ocr-window-segv-confirm` records a SIGSEGV in the
same OCR neighbourhood that Linux could not reproduce at all.

**If that reading is right, the hosted lane is BETTER signal, not worse:** it
reproduces on demand something the fast laptop hides, which is exactly what a
CI tier is for. That reading is still **unconfirmed** — it needs the stack.

### What it costs either way

- The DMG is **not** withheld. This lane's UAT step is `continue-on-error:
  true` by owner decision (2026-07-26); both hosted runs staged and uploaded a
  47.6 MB DMG normally.
- The UAT ratchet reads `worse (crash-appeared)` on the first hosted night,
  reddening that run's signal once; from the second night the baseline records
  `crashed: true` and it reads `same`.
- The real cost is **UAT signal fidelity on the macOS lane**: 44/44 clean on
  the laptop today versus a reproducible 43/44 + crash on hosted.

## Why this is filed rather than fixed in the migrating PR

The migration PR changes no source and cannot fix a memory-safety bug in the
OCR path; blocking a runner move on an unrelated crash fix would be the wrong
trade. Filed so it cannot silently become "how the macOS lane always looks."

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

## Stack captured, root cause found (2026-08-06, self-hosted Mac session)

The Threshold's "**Before fixing, capture the stack**" clause is now
satisfied, and it points somewhere the item did not expect.

**It is not an ML/OCR bug.** The `Related` section pairs this with
`2026-08-03-macos-nightly-ocr-window-segv-confirm` and asks whether it is the
same `MlScheduler::postResultToGuiThread()` window. It is not: no worker
thread, no ML code, and no OCR code appears anywhere on the faulting stack.

**Faulting thread** (`~/Library/Logs/DiagnosticReports/`, reproduced locally):

```
 0  TestUatRecognizeText::uat_ocr_067_noticeAndProbeCachesPurgedOnClose()
 1  QMetaMethodInvoker::invokeImpl(...)            [QtCore]
 ...
 6  QTest::qRun()                                  [QtTest]
 8  main
```

Frame 0 is the test slot itself, with no production frame beneath it —
`test_uat_recognize_text.cpp:643`, `QCOMPARE(dv->documentCount(), 0)`, the
first dereference after the close.

**(c) — the classification the Threshold asks for: recycled garbage
pointer, NOT a null dereference.** The hosted run's own log (run
[31020325093](https://github.com/programmerq/trailer/actions/runs/31020325093))
records `accessing address 0x0e1dfcf0da7d1784` — non-canonical, freed memory
that has been reused. So this is *not* the `0x28`/`0x50` null-page shape of
the two related items; "small fault address" is not a reliable signature for
this family.

**Why hosted reproduces and the laptop does not.** The use-after-free happens
on every run, everywhere. Whether it *faults* depends only on what the
allocator did with the freed page: the M4 leaves it mapped and silently reads
garbage (**0 failures / 60 runs at load average 21** — contention alone does
not force it), while the 3-core hosted box recycles it. Guard Malloc
(`DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib`) unmaps freed pages and
reproduces it **10/10, idle, on any Mac** — the tool this family should be
hunted with, since `-fsanitize=address` hangs at startup on macOS 26.

This confirms the item's own reading that **the hosted lane is better signal,
not worse.**

**Two distinct bugs, both fixed together** (see the implementing PR):

1. The test dereferenced `dv`/`mw` after the close, which on macOS destroys
   the window (`onAllTabsClosed()`'s `Q_OS_MACOS` branch + `WA_DeleteOnClose`).
2. Fixing (1) exposed a **production** bug, reproducible on pristine `main`
   with a minimal case and **not macOS-specific**:
   `DocumentView::onTabCloseRequested()` emitted `currentDocumentChanged`
   from `removeTab()` *before* `m_documents.erase()`, so listeners resynced
   against a stale vector and nothing corrected them afterwards. The sidebar
   kept a raw `IDocument *` to the closed document — a dangling-pointer
   SIGSEGV reachable by **closing one tab of two**.

**Status stays `open`:** this Threshold requires three consecutive green
`macos-14` nightlies, which no code change can satisfy on its own.
