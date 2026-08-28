---
id: 2026-08-06-wine-lane-emits-no-test-output
title: The Windows cross-build + Wine lane cannot emit test output at all, so a failure there names no assertion
priority: TBD
status: open
source: discovered while debugging a test_open_dedup failure on that lane, PR #156
created: 2026-08-06
---

## Threshold

On the *Windows cross-build + Wine unit tests* lane, a deliberately-failing
test's `--output-on-failure` block contains that test's QTest output — at
minimum its `FAIL!` line with the compared values. Checkable directly:
temporarily add a test that asserts something false, push, and read the CI
log. Today that block comes back empty.

A weaker but acceptable form: the lane captures output through some other
route the log shows (a redirected file that the workflow `cat`s, a JUnit
XML artifact via `ctest --output-junit`, etc.). What must end is "a test
fails there and no one can tell which check failed".

## Context

Discovered the hard way on PR #156. `test_open_dedup` failed only on this
lane, and its `--output-on-failure` block was **completely empty** — no
QTest `********* Start testing` banner, no `PASS`/`FAIL!` lines, no
`Totals:`. Two rounds of debugging were spent on hypotheses because the one
datum needed (which assertion failed) was unavailable.

Ruled out along the way, so nobody repeats it:

- **Not buffering.** Unbuffered `stdout`/`stderr` were added to that test's
  `main()` (`setvbuf(..., _IONBF, 0)`, the same thing `test_image_scale.cpp`
  does) and it changed nothing. The unbuffering is verifiably compiled in:
  `objdump -d` shows two `setvbuf@plt` calls as the first instructions of
  `main`, and the file has no `QTEST_MAIN` generating a competing entry
  point.
- **Not a crash swallowing the output.** ctest reports the failure as
  `(Failed)`, not `(SEGFAULT)` / `(Subprocess aborted)` — the process exits
  normally with a non-zero status, i.e. `QTest::qExec` returning a failure
  count. It ran, it decided, it just could not tell us.
- **Not specific to that one test.** ctest only prints output for *failing*
  tests, so every passing test's output on this lane is equally invisible.
  There is no evidence any test binary has ever produced capturable stdout
  here; it simply went unnoticed until something failed.

Unconfirmed lead worth checking first: `qt_add_executable` may be producing
GUI-subsystem (`WIN32_EXECUTABLE`) binaries for the test targets, which on
Windows have no attached console. `CMakeLists.txt:746` sets
`WIN32_EXECUTABLE ON` explicitly for the `trailer` app but the test helper
in `tests/CMakeLists.txt` sets nothing either way, so the default applies —
and whether that default is ON was not determinable without building for
the target. If it is ON, setting it OFF for test targets would likely
restore stdout for the whole lane in one line.

## Why it matters beyond the PR that found it

This is a standing gap in a **gating** lane: it runs on every PR
(`ci.yml`, `--label-exclude 'uat|perf'`) and can block a merge while being
constitutionally unable to say why. Every future failure there costs the
same multi-round guessing this one did.

The interim mitigation now in the tree is narrow and deliberate:
`tests/CMakeLists.txt` registers each of `test_open_dedup`'s test functions
as its own ctest entry, so ctest's *own* pass/fail reporting — which does
work on this lane — names the failing **check** rather than only the failing
binary. That is what finally located the real defect. It is worth keeping,
but it only covers one file; the underlying capture problem is what this
item tracks.

Related but distinct: `docs/backlog/2026-08-02-uat-fontdir-broken-on-cross-build.md`
covers `QT_QPA_FONTDIR` resolving to `/Fonts` on this same lane. Both are
consequences of the cross-build lane's environment being assembled on the
Linux host; neither causes the other.

## Provenance

Owner-facing symptom first seen in PR #156's CI on 2026-08-06. Full
evidence trail — the empty blocks, the `objdump` verification, the
`(Failed)`-not-crash distinction, and the per-slot bisect that finally
named the assertion — is in that PR's discussion and in the commit
messages on `claude/open-already-open-file`.
