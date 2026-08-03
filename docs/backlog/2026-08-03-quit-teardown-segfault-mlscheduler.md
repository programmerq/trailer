---
id: 2026-08-03-quit-teardown-segfault-mlscheduler
title: test_quit_and_keep_windows segfaults at process exit with MlScheduler threads still parked
priority: TBD
status: open
source: flaky-test hunt during the 2026-08-02 update-pubkey branch review
created: 2026-08-03
---

## Threshold

`tests/test_quit_and_keep_windows` exits 0 on **200 consecutive**
offscreen runs on an otherwise-loaded machine:

```sh
for i in $(seq 1 200); do QT_QPA_PLATFORM=offscreen ./build/tests/test_quit_and_keep_windows || echo "FAIL $i"; done
```

Today it fails roughly 1-2 times in 30 under load, and the failure is a
`SIGSEGV` (ctest reports `SEGFAULT`), not a `FAIL!` assertion.

## Context

**Pre-existing, not introduced by any current branch** — reproduced at
comparable rates on `origin/main` (1/30) and on
`claude/trailer-coordinator-elk85x` (2/30), measured side by side from
two build trees.

The crash lands **after every test function has already reported PASS**:

```
PASS   : TestQuitAndKeepWindows::initTestCase()
PASS   : TestQuitAndKeepWindows::keepWindowsQuitWritesStoreWithoutPrompt()
PASS   : TestQuitAndKeepWindows::restoreRehydratesUntitledDraftByteIdentical()
PASS   : TestQuitAndKeepWindows::restorePreservesDevicePixelRatioAndCaptureOrigin()
<segfault>
```

so it is a **teardown/exit race, not a test-logic failure** — the assertions
under test all hold.

The crash-handler backtrace shows two `MlScheduler` threads still parked on
a condition variable when the process is being torn down:

- `trailer::MlScheduler::workerLoop()` blocked in `___pthread_cond_wait`
- `MlScheduler::MlScheduler(...)::{lambda()#2}` (the power watcher) blocked
  in `___pthread_cond_clockwait64`

Both frames' `mutex` / `cond` arguments are **stack addresses**
(`0x7fff5180aa50`), i.e. the threads are waiting on synchronisation objects
whose storage is going away underneath them.

`src/ml/MlScheduler.cpp:222-245` already has the correct shutdown sequence —
set `m_stopping`, `m_cv.notify_all()` / `m_powerCv.notify_all()`, then
`join()` both threads. The bug is therefore that some exit path reaches
process teardown **without** running that sequence to completion (or races
it), leaving the workers parked on a destroyed condvar. Likely suspects: the
`m_stopping && !m_worker.joinable()` early-out at line 222, and any
`qApp`-quit path that tears the scheduler down from a static/atexit context
rather than through `~MlScheduler()`.

Worth fixing rather than muting: this is the app's real quit path, so a
shutdown race that only *usually* wins is a crash-on-quit users could hit,
not merely test flake.

## Related

- `2026-08-03-load-sensitive-offscreen-test-races` — the other two
  load-sensitive offscreen tests found in the same hunt. Distinct: those are
  timing assertions that fail cleanly, this one is a memory-safety crash.
