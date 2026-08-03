---
id: 2026-07-24-wine-uat-failures-triage
title: Triage the 17 Wine UAT failures surfaced by nightly.yml's Windows lane
priority: TBD
status: open
source: nightly.yml bootstrap run 30104846942 (PR #121), Windows lane, job 89523510316 — first-ever UAT-under-Wine execution
created: 2026-07-24
---

## Threshold

Each of the 17 tests listed below either (a) passes under Wine, or (b) carries
a documented per-test Wine `QSKIP` with a stated rationale (the pattern
`docs/backlog/2026-07-19-wine-cross-thread-editor-save.md` and
`2026-07-21-wine-keep-restore-file-move-open-handle.md` already use for
Wine-only unit-test artifacts). Once every item is in one of those two
states, the nightly release table's Wine UAT count (`✅`/`⚠️ N/40`) reflects
that outcome and this item closes.

## Context

`nightly.yml` (PR #121) is the first place Trailer's UAT suite has ever run
under Wine — `ci.yml` and `release.yml`'s existing Wine lane explicitly
excludes the `uat` label. Bootstrap run
[30104846942](https://github.com/programmerq/trailer/actions/runs/30104846942),
Windows lane, job
[`89523510316`](https://github.com/programmerq/trailer/actions/runs/30104846942/job/89523510316):

- **Wine unit tests: 57/57 passed (100%), ~57s total.** Strong signal Wine
  itself is not fundamentally broken for Trailer's harness.
- **Wine UAT: 23/40 passed (58%), 17 failed.**

Per the owner's 2026-07-24 decision, Wine UAT is **non-gating** for
`nightly.yml` — a Windows lane with a green build + Wine unit pass still
stages and publishes its artifact regardless of the Wine UAT count. It's
surfaced instead as visible signal in the nightly release body's per-OS
table (e.g. `⚠️ Wine UAT 23/40`) so the count is trackable night to night.
Wine UAT becomes gating once a real Windows CI runner exists — Wine is a
stand-in for Windows here, not the platform Trailer ships to (see
`nightly.yml`'s "UAT suite (Wine)" step comment).

The 17 failing tests, from `ctest`'s summary in `build-win/`:

```
test_uat_foundations
test_uat_search_and_markup
test_uat_password
test_uat_autofill
test_uat_background_removal
test_uat_recognize_text
test_uat_external_change
test_uat_ml_affordances
test_uat_pdf_pages
test_uat_page_change_signal
test_uat_two_page_dpr1
test_uat_two_page_dpr1_5
test_uat_two_page_dpr2
test_uat_empty_state
test_uat_file_menu_ia
test_uat_zoom_indicator
test_uat_empty_state_recent
```

**Worth checking first:** `test_uat_foundations` and `test_uat_search_and_markup`
both ran concurrently (`CTEST_PARALLEL_LEVEL=2`) and both failed at almost
exactly the same wall-clock — 92.98s and 93.00s respectively — with **no**
captured output despite `--output-on-failure`. That near-identical duration
smells like a shared timeout/watchdog under Wine rather than two independent
assertion failures, and might explain a cluster of the 17 rather than each
needing separate diagnosis. The other 15 failed at a spread of individual
durations (1-32s), which reads more like genuine per-test issues — possibly
related to, but not necessarily the same mechanism as, the two already-
tracked Wine cross-thread-handle artifacts in
`docs/backlog/2026-07-19-wine-cross-thread-editor-save.md` and
`docs/backlog/2026-07-21-wine-keep-restore-file-move-open-handle.md` (those
are unit-test-specific findings, not UAT, so the connection is unconfirmed).

No root-causing or fixing was attempted here — this item exists to track the
finding for the owner to work through directly, per their stated intent
("happy digging on the failures when I'm back at my laptop").

## Triage pass (2026-08-02) — no code changed

Owner asked for a triage-only pass: categorise the failures, file what's
real, fix nothing. Source: `nightly-20260802`, run
[30745321546](https://github.com/programmerq/trailer/actions/runs/30745321546),
Windows lane job `91490600530` — **Wine UAT 22/43, 21 failed**.

### Headline: 20 of the 21 cannot be categorised yet, and that is the finding

**Every single failing test produced ZERO captured output**, despite the
step running `ctest --output-on-failure`. Not a truncated log, not a
missing assertion message — an empty line where the captured output
should be, for all 21, at durations from 1.0s to 251s.

That is not what an assertion failure looks like. A UAT binary prints its
QtTest banner (`********* Start testing of TestUatEmptyState *********`)
before running a single case — verified by running one locally — so any
test that reaches `QTest::qExec` and then fails an assertion emits at
minimum that banner plus a `FAIL!` line. Zero bytes means these processes
are dying without flushing, or their stdout is not reaching ctest at all
under Wine.

**Ruled out while chasing that** (recorded so nobody re-runs these):

- *GUI-subsystem executables have no console, so stdout goes nowhere.*
  No — `qt_add_executable` only sets `WIN32_EXECUTABLE` when passed
  `GUI` (Qt's `QtExecutableHelpers.cmake`), and the only
  `WIN32_EXECUTABLE ON` in this repo is on the `trailer` app target
  (`CMakeLists.txt:674`). The test binaries are console subsystem.
- *The shared sandbox `main()` bails early via its silent `return 1`.*
  No — `QTemporaryDir fakeHome; if (!fakeHome.isValid()) return 1;` would
  match the signature exactly, but the **passing** Wine tests
  (`test_uat_forms`, `test_uat_preferences`, `test_uat_signature`, …)
  carry byte-identical scaffolding. `QTemporaryDir` works under Wine.

What remains consistent with the evidence is abnormal termination losing
block-buffered stdout: a process that writes to a pipe and then aborts
loses **100%** of what it wrote (demonstrated: `printf(...)` + `abort()`
piped to `cat` captures 0 bytes). Not proven for these binaries — proving
it needs a Wine host.

**So the first action is not to fix 21 tests; it is to make them
legible.** Cheapest options, roughly in order:

1. Add `ctest --output-junit` to the Wine UAT step. Per-test status and
   timing land in a structured file independent of whether the child
   flushed stdout, and it costs one flag.
2. `setvbuf(stdout, nullptr, _IONBF, 0)` at the top of the UAT `main()`s
   (or `QT_LOGGING_TO_CONSOLE=1` / `-o -,txt`) so partial output survives
   an abnormal exit.
3. Run one failing binary by hand under Wine with output redirected to a
   file, which sidesteps ctest's pipe entirely.

Categorising the 20 before one of those lands would be guessing.

### The 1 that could be categorised: a real, verified environment defect

`QT_QPA_FONTDIR` is **broken on the Wine lane** and has been since it was
introduced. Both `tests/CMakeLists.txt:25` and `tests/uat/CMakeLists.txt:19`
set it from CMake's *configure-time host* environment:

```cmake
list(APPEND _trailer_uat_env "QT_QPA_FONTDIR=$ENV{SystemRoot}/Fonts")
```

The Wine lane cross-compiles **on Linux**, where `SystemRoot` is unset —
nothing in `.github/`, `scripts/`, or `docker/` sets it — so the variable
resolves to `QT_QPA_FONTDIR=/Fonts`, a path that does not exist. Verified
by evaluating the same expression under CMake on a Linux host:

```
-- SystemRoot=[] -> [QT_QPA_FONTDIR=/Fonts]
```

`WIN32` is true for the cross-build, so the branch is taken; the value is
just wrong. Per that block's own comment the variable exists so
`QPdfWriter` emits real PDF fonts instead of filled vector paths —
without it "searchable-text UATs (UAT-VWR-061..066) all fail because
there is nothing to find". Three of the 21 failures
(`search_and_markup`, `recognize_text`, `ocr_evidence`) are exactly
searchable-text tests.

Fix direction (not applied): resolve the font directory at **test run**
time rather than CMake configure time — e.g. point at
`$WINEPREFIX/drive_c/windows/Fonts` in the workflow step, or export
`SystemRoot` before `cmake` configures. Note this also affects the Wine
**unit** lane, which is currently green — so it is a latent problem
there, not a visible one.

### Stability map (what's flaky vs. what's stuck)

Comparing the bootstrap run (2026-07-24, 17 failures / 40) against
2026-08-02 (21 / 43), and reading the intervening nightly release bodies:

| Class | Count | Tests |
|---|---|---|
| **Stuck** — failing every night for 9 days | 17 | the original bootstrap list above |
| **Regressed** — passed at bootstrap, fail now | 3 | `viewer`, `ocr_evidence`, `thumbnail_sidebar_dpr1` |
| **Born failing** — added after bootstrap, never passed under Wine | 1 | `dock_recents` (registered 2026-07-31) |

None are flaky: the 17 are deterministic across every nightly. The three
regressions landed in the 07-31 → 08-01 window (Wine went 23/41 → 20/42),
alongside the toolbar-position, View-menu-order, search-scroll, text-
selection and zoom-readout commits — worth checking first when the output
problem is fixed. `thumbnail_sidebar_dpr1` is a particularly clean lead:
the *same binary* passes at `dpr1_5` and `dpr2` and fails only at `dpr1`.

(Registration dates show 4 tests added since bootstrap — `feedback`,
`deference_evidence`, `dock_recents`, `preferences` — against a net total
of +3, so one entry was removed or renamed in that window. Not chased;
immaterial to the above.)

### `foundations` is its own signature

`test_uat_foundations` reported **251.14s of a 251.15s total run** — it
started first (with `CTEST_PARALLEL_LEVEL=2`) and finished last, having
occupied one of the two slots for the entire suite while everything else
ran serially in the other. It is not a ctest timeout (default 1500s). The
2026-07-24 note guessed a shared watchdog explained a cluster; that guess
is superseded — `search_and_markup` failed at 88.64s, and the other 19
spread from 1.0s to 18.8s. Whatever `foundations` does, it is one test
hanging until something releases it, not a cluster-wide timeout.

### One correlation, deliberately down-weighted

All 21 failures are among the tests using the shared HOME/XDG sandbox
`main()`; the 7 registered entries that don't use it
(`capture_permission_evidence`, `staged_image_open_dpr*`,
`offthread_open_dpr*`) all pass. Suggestive but **confounded**: the
sandboxed tests are precisely the ones that construct `Application` +
`MainWindow`, so "drives the full app" explains the split at least as
well as "uses the sandbox". Worth remembering, not worth acting on.

Note for whoever picks this up: on Windows `QStandardPaths` reads
`APPDATA`/`LOCALAPPDATA`, not `HOME`/`XDG_*`, so that sandbox does not
actually redirect anything under Wine — every test shares the real
prefix's `%APPDATA%`, two at a time under `CTEST_PARALLEL_LEVEL=2`. That
is a genuine latent defect regardless of whether it causes these
failures; `QStandardPaths::setTestModeEnabled(true)` is Qt's supported
cross-platform primitive for it.

### Threshold status

Unchanged and **not met** — this pass categorised, it did not fix. The
item's original threshold (every test either passes under Wine or carries
a documented per-test Wine `QSKIP`) still stands. Recommended next step
is the observability fix above, which is a prerequisite for the rest of
it rather than part of it.
