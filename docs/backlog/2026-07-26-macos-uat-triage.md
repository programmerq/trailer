---
id: 2026-07-26-macos-uat-triage
title: Triage the first-ever macOS UAT run (1 crash, 1 stale assertion, 39/40 clean)
priority: TBD
status: open
source: nightly.yml bootstrap run 30207813058 (dispatched on claude/fix-capture-dpr-zoom-mode, merged to main as PR #123 / 0b008fe), macOS lane, job 89808952242, step "UAT suite" — first-ever macOS UAT execution in this project's history
created: 2026-07-26
---

## Threshold

Each of the two failures below either (a) passes on macOS, or (b) carries a
documented per-test macOS `QSKIP`/fix with a stated rationale, mirroring how
`docs/backlog/2026-07-24-wine-uat-failures-triage.md` treats the Wine list.
The crash (Finding 1) additionally requires a **symbolicated backtrace or
ASan report confirming (or refuting) the root-cause hypothesis below** before
it can be marked understood — an assertion-level fix without that
confirmation is not sufficient to close this item, because a fix for the
wrong cause would leave a live crash unexplained. Once both are closed,
`nightly.yml`'s macOS UAT step is flipped back from `continue-on-error: true`
to gating (see "Non-gating in the nightly pipeline" below).

## Context

`nightly.yml` (PR #121, 2026-07-24) added the first-ever macOS UAT execution
in this project. Getting here took two intermediate fixes: PR #122 (a
zoom-readout lying-control bug) and PR #123 (capture-origin zoom bugs + a
macOS `QSettings` test-isolation defect that was blocking the mac lane's
*unit* tests from passing at all). With both merged, mac unit tests passed
for the first time and the job progressed into UAT — which promptly
crashed. That is expected-ish for genuinely new ground, not a regression.

**Run:** [30207813058](https://github.com/programmerq/trailer/actions/runs/30207813058),
job [89808952242](https://github.com/programmerq/trailer/actions/runs/30207813058/job/89808952242).
Dispatched on `claude/fix-capture-dpr-zoom-mode`, whose content is now on
`main` (commit `0b008fe`, PR #123), so this result applies to current `main`.

**Full picture:**
- **macOS unit tests: 62/62 passed (100%).**
- **macOS UAT: 39/40 ctest "tests" passed (97.5%), 1 failed** —
  `test_uat_foundations`, reported by ctest as `SEGFAULT`. That one ctest
  entry is a QtTest binary containing many sub-cases; two of them failed
  for two *unrelated* reasons (a crash and a stale assertion), detailed
  below. No other macOS-specific issue was found anywhere else in the
  62 + 40 test run.

Full per-binary list (all "Passed" except test_uat_foundations):
`test_uat_foundations` (**SEGFAULT**), `test_uat_search_and_markup`,
`test_uat_password`, `test_uat_forms`, `test_uat_autofill`,
`test_uat_signature`, `test_uat_redaction`, `test_uat_background_removal`,
`test_uat_instant_alpha_and_smart_lasso`, `test_uat_recognize_text`,
`test_uat_ocr_disk_cache`, `test_uat_external_change`,
`test_uat_ocr_evidence`, `test_uat_capture_permission_evidence`,
`test_uat_ml_affordances`, `test_uat_pdf_pages`,
`test_uat_page_change_signal`, `test_uat_thumbnail_sidebar_dpr{1,1.5,2}`,
`test_uat_undo_interleaved_cap`, `test_uat_viewer`,
`test_uat_two_page_dpr{1,1.5,2}`, `test_uat_sweep_dpr{1,1.5,2}`,
`test_uat_staged_image_open_dpr{1,1.5,2}`, `test_uat_empty_state`,
`test_uat_file_menu_ia`, `test_uat_zoom_indicator`,
`test_uat_offthread_open_dpr{1,1.5,2}`, `test_uat_empty_state_recent`,
`test_uat_window_menu_maximize`, `test_uat_preferences`.

---

## Finding 1 (top billing) — SEGFAULT in `test_uat_foundations`, sub-case `uat_fnd_014_closeDirtyTabDiscardDropsDoc`

A crash is categorically more serious than an assertion failure — it is a
candidate for something that could crash Trailer on a real Mac user's
machine, not just a test expectation being out of date. This gets first
billing and the most scrutiny in this item, per explicit owner direction.

**Verbatim ctest/QtTest output:**

```
 1/40 Test  #63: test_uat_foundations .....................***Exception: SegFault  0.87 sec
********* Start testing of TestUatFoundations *********
Config: Using QtTest library 6.11.0, Qt 6.11.0 (arm64-little_endian-lp64 shared (dynamic) release build; by Apple LLVM 16.0.0 (clang-1600.0.26.3)), macos 26.5.2
[... 13 earlier sub-cases PASS, see Finding 2 for the one FAIL among them ...]
QWARN  : TestUatFoundations::uat_fnd_014_closeDirtyTabDiscardDropsDoc() This plugin does not support propagateSizeHints()
Received signal 11 (SIGSEGV), code SEGV_ACCERR, at instruction address 0x000000010023950c, accessing address 0x0000000000000028
         uat_fnd_014_closeDirtyTabDiscardDropsDoc function time: 197ms, total time: 631ms
```

```
98% tests passed, 1 tests failed out of 40
...
The following tests FAILED:
Errors while running CTest
	 63 - test_uat_foundations (SEGFAULT)                   uat
```

That is the **entirety** of the diagnostic detail the CI log carries: a bare
instruction address, an access address of `0x28` (40 bytes — a small-offset
member/vtable read, not a wild pointer), and `code SEGV_ACCERR` (the page is
mapped but access is disallowed — consistent with a near-null read through a
freed/zeroed object, distinct from a plain unmapped-address `SEGV_MAPERR`).
No symbols, no backtrace, no core file. That is not enough to *confirm* a
cause — only to form a hypothesis. `0x28` off null/freed is the textbook
shape of dereferencing a member at a small offset through a dangling
pointer — consistent with, though not proof of, the use-after-free
hypothesis below.

**Why this matters more than a typical crash finding:** the hypothesis below
concludes the fault is in the *test's* own dereference, not in product code.
**If that conclusion is wrong** — if the fault instead lies in
`onAllTabsClosed()`, `closeEvent()`, or anything in the production
teardown path itself, rather than in the test harness's post-`requestCloseTab`
assertions — **this would be a real crash a Mac user could hit by closing
their last dirty tab and choosing Discard**, which materially raises this
item's priority above "test hygiene." Nothing found here rules that out;
it is exactly what the symbolicated backtrace / ASan run below would
settle. Keep this labeled **unconfirmed** either way — do not patch blind.

### Leading hypothesis (code-reading only — NOT confirmed via a debugger)

The crashing sub-case runs immediately after
`uat_fnd_014_closeDirtyTabCancelKeepsDocAndEdits`, which deliberately leaves
a dirty `FakeDoc` open (Cancel aborts the close) in the shared `MainWindow`.
QtTest's `init()` (`tests/uat/test_uat_foundations.cpp:292-300`) then calls
`w->close()` on every leftover `MainWindow` before the crashing test starts.

The crashing test itself
(`tests/uat/test_uat_foundations.cpp:757-797`) does:
```cpp
MainWindow *mw = app->ensureWindow();       // may reuse or recreate
auto *dv = mw->findChild<DocumentView *>();
FakeDoc *doc = addFakeDoc(mw, file, ..., dirty=true);
...
requestCloseTab(dv, 0);                     // drives the close + pumps events
QCOMPARE(dv->documentCount(), 0);           // <- dereferences dv again
QCOMPARE(mw->documentCount(), 0);           // <- dereferences mw again
QCOMPARE(*saveSink, 0);
grabTo(mw, QStringLiteral("fnd014_empty_state_after_discard.png"));  // <- mw->grab()
```
`requestCloseTab` (`test_uat_foundations.cpp:174-178`) invokes
`DocumentView::onTabCloseRequested` directly and then calls
`QApplication::processEvents()`.

**The macOS-specific branch that Linux/Windows never take:**
`MainWindow::onAllTabsClosed()` (`src/ui/MainWindow.cpp:4696-4721`):
```cpp
void MainWindow::onAllTabsClosed() {
#ifdef Q_OS_MACOS
    // macOS: there is no persistent empty window. Closing the last
    // document closes the window; the global menu bar persists...
    close();
#else
    if (m_app && m_app->windowCount() > 1) {
        close();
    } else {
        updateEmptyState();   // <- window survives on Linux/Windows here
    }
#endif
}
```
Discarding the last (and only) tab fires `DocumentView::allTabsClosed()`
synchronously (same-thread direct connection), which on **macOS only**
calls `MainWindow::close()` unconditionally — regardless of window count.
`MainWindow` is constructed with `Qt::WA_DeleteOnClose`
(`src/app/Application.cpp:286-292`), so `close()` schedules `deleteLater()`.
`closeEvent()` itself (`src/ui/MainWindow.cpp:4949-4958`) early-returns
under `QT_QPA_PLATFORM=offscreen` (`event->accept(); return;`) specifically
*because* "UAT init slots routinely call `w->close()` to clean up dirty
state between cases" — i.e. the codebase already knows offscreen-mode
`close()` is a common test idiom, just not this particular knock-on effect.

If the `QApplication::processEvents()` inside `requestCloseTab` (called
while still inside the *same* test function, at loop-nesting level 0, same
pattern PR #123's `test_image_scale.cpp::cleanup()` comment discusses for a
related but different macOS-only leftover-window class of bug) ends up
flushing that just-posted `DeferredDelete` event, `mw` and `dv` are dangling
for the rest of the test — the very next lines dereference both, and
`grabTo(mw, ...)` calls `mw->grab()`, a virtual call on a (possibly)
freed `QWidget`. The 197ms function time and the single `QWARN` before the
crash (consistent with window construction/teardown machinery, not an
instant crash) are both consistent with this sequence completing several
steps in before faulting on one of the post-`requestCloseTab` dereferences.

**Linux does not reproduce this** — confirmed locally (this PR's branch,
`QT_QPA_PLATFORM=offscreen`, `ctest -L uat`, run twice): `test_uat_foundations`
passes cleanly both times. This is consistent with the hypothesis: Linux's
`onAllTabsClosed()` takes the `updateEmptyState()` branch (window persists,
`windowCount() == 1`), so `mw`/`dv` are never scheduled for deletion in the
first place — not a race, a straight `#ifdef Q_OS_MACOS` divergence. If the
hypothesis is right, this crash is **deterministic on macOS**, not flaky —
consistent with it crashing on this run and (per the log) on every attempt
within the run.

**Why this reads as a test-code gap rather than a defect a real user would
hit through the GUI:** in real (non-offscreen) usage, `deleteLater()` is
asynchronous — the window disappears on the *next* event-loop turn, and no
production code path synchronously dereferences `mw`/`dv` after triggering
the close the way this test's assertions do. The `closeEvent()` comment at
`src/ui/MainWindow.cpp:4983-4988` describes exactly this macOS last-tab-close
routing and treats it as working as intended for a real user. **This claim
is asserted, not verified with a debugger** — see below for what would
verify it.

### What would confirm or refute this (not attempted here — no mac access)

- A **symbolicated backtrace**: run the failing binary directly under
  `lldb` on the runner (`lldb -o run -o bt -o quit --batch -- ./test_uat_foundations`)
  or enable core dumps (`ulimit -c unlimited`) before the ctest step and
  symbolicate the resulting core. The raw log's bare instruction address is
  not enough to identify which object/member is being read.
- **ASan** (`-fsanitize=address` build of the UAT binaries) would turn this
  into a precise "heap-use-after-free at file:line, freed by thread T at
  file:line" report if the hypothesis is right, or point elsewhere if not —
  the single highest-value diagnostic to run next.
- macOS malloc debugging env vars (`MallocScribble=1`, `MallocPreScribble=1`)
  on a plain (non-ASan) run would make a use-after-free crash more
  reliably and immediately, corroborating (or failing to corroborate) the
  timing-dependent part of the hypothesis.
- If confirmed, the fix belongs in the **test** (e.g., stop dereferencing
  `mw`/`dv` after driving a close that may have torn the window down —
  re-acquire via `app->ensureWindow()`, or gate the post-close assertions
  behind `#ifndef Q_OS_MACOS` the way `uat_fnd_011` already does for
  macOS-only menu behavior), not in `onAllTabsClosed()`'s product logic,
  which matches the documented, intentional macOS empty-state design
  (DESIGN §2.4.2) and is exercised safely by real (non-offscreen) usage per
  the reasoning above — but that conclusion should be treated as provisional
  until the backtrace/ASan evidence above actually lands.

**Bucket:** test-environment assumption (macOS-only window-persistence
semantics interacting with the test's synchronous continuation) —
**provisional, not confirmed**. Recommendation: get a symbolicated
backtrace or ASan report before writing the fix; do not patch this
blind.

---

## Finding 2 — stale assertion in `uat_fnd_011_macosNoWindowMenuProvidesFileActions`

**Verbatim output:**
```
FAIL!  : TestUatFoundations::uat_fnd_011_macosNoWindowMenuProvidesFileActions() 'a' returned FALSE. (Missing File menu item: &New)
   Loc: [/Users/runner/actions-runner/_work/trailer/trailer/tests/uat/test_uat_foundations.cpp(516)]
```

`uat_fnd_011` (added 2026-05-18, `tests/uat/test_uat_foundations.cpp:499-528`)
asserts the macOS no-window menu bar's File menu contains `&New`,
`&Open…`, `New from &Clipboard`, and `&Acquire…`. `Application::installNoWindowMenuBar()`
(`src/app/Application.cpp:1418-1465`) does **not** add a standalone `&New`
item — by design: the 2026-07-18 file-menu IA refactor (commit `3bca312`,
landed via PR containing `a7be701`) deliberately removed the old standalone
"New" (blank window) action everywhere and rebound `⌘N`/`Ctrl+N` to "New
from Clipboard." `tests/uat/test_uat_file_menu_ia.cpp:243-271`
(`uat_fmia_003_cmdN_boundToNewFromClipboardNotStandaloneNew`) is the
**current, passing** contract test for this — it explicitly asserts *"there
is no standalone New / &New item"* anywhere on the menu bar, and it passed
cleanly in this same macOS run (`test_uat_file_menu_ia`, test #95, Passed).

`uat_fnd_011` simply predates that IA change by two months and was never
updated — because macOS CI did not exist until 2026-07-24, nothing ever ran
it against the new IA. This is a **stale test assertion**, not a product
defect: a real Mac user's no-window File menu is correct today (Open, New
from Clipboard carrying ⌘N, Acquire) and matches the current, deliberate,
tested IA.

**Fix (not applied in this PR — scope kept to the systemic fix below; this
one is fully diagnosed and ready for pickup):** in
`tests/uat/test_uat_foundations.cpp`, drop `&New` from the expected-items
list at line 510 (leaving `&Open…`, `New from &Clipboard`, `&Acquire…`),
and drop or rewrite the `newAction`/`File > New` trigger check at lines
519-526 to target `New from &Clipboard` instead — mirroring what
`uat_fmia_003` already verifies for the per-window menu.

**Bucket:** test-authoring drift (confirmed via git history + the sibling
contract test, not an offscreen/DPI/timing assumption).

---

## Fix applied in this PR — QSettings NativeFormat gap, UAT harness family

PR #123 (merged, `0b008fe`) fixed a macOS-only test-isolation defect in six
**unit** test `main()`s: `QSettings(org, app)` — the constructor
`DocumentTypeDefaults` uses (`src/settings/DocumentTypeDefaults.cpp:107,113`,
the sole `QSettings` consumer in this codebase, confirmed via
`src/app/Application.cpp:96-104`) — resolves to `QSettings::NativeFormat`
on macOS, i.e. `CFPreferences` keyed off the process's real UID, which
**ignores** the `$HOME`/`XDG_*` sandboxing every test's `main()` sets up.
The fix forces `QSettings::setDefaultFormat(QSettings::IniFormat)` before
constructing `Application`.

That audit's own PR body says "every affected test `main()`," but it only
checked `tests/*.cpp` (unit tests) — `tests/uat/*.cpp` was never audited.
Checking it here (per the owner's brief, flagged as "cheap to check" and
"a strong early suspect"): **all 27 UAT harness binaries that construct a
real `trailer::Application`** sandbox `HOME`/`XDG_CONFIG_HOME`/`XDG_DATA_HOME`
in their `main()` the identical way the six now-fixed unit tests did, and
**none of them** called `QSettings::setDefaultFormat(QSettings::IniFormat)`
before this PR. (The three UAT binaries that don't construct a
`trailer::Application` —
`test_uat_capture_permission_evidence.cpp`, `test_uat_offthread_open.cpp`,
`test_uat_staged_image_open.cpp` — don't touch `QSettings`/`MainWindow` and
correctly need no fix.)

This is a real, systemic latent gap: the self-hosted mac runner is a
**persistent** VM (not an ephemeral container), so every UAT run before this
fix was reading/writing the **same real, persistent**
`~/Library/Preferences/` `CFPreferences` domain that a real installed
Trailer.app on that machine would use — exactly the cross-run pollution
risk PR #123 closed for unit tests. This run's actual failures were **not**
attributable to it (39/40 passed; the one failure's two causes are Findings
1 and 2 above, neither QSettings-related) — this fix is preventive, not a
fix for an observed symptom in this run, and is called out as such rather
than overclaiming it explains anything seen here.

**Fix:** applied the identical mechanical change PR #123 used — add
`#include <QSettings>` and `QSettings::setDefaultFormat(QSettings::IniFormat);`
(with the same pointer-comment PR #123 established, referencing
`tests/test_image_scale.cpp`'s `main()` for the full rationale) to all 27
affected `tests/uat/*.cpp` `main()`s, before `Application app(argc, argv)`
is constructed.

**Local verification (Linux):**
- `cmake --build build` — clean build, no new warnings from the 27 changed
  files.
- `ctest --test-dir build --label-exclude uat` — **62/62 passed**.
- `ctest --test-dir build -L uat` (`QT_QPA_PLATFORM=offscreen`) — run
  twice: first run showed 1 unrelated pre-existing flake
  (`test_uat_ocr_evidence`, an `MlProgressWidget` state timing race,
  confirmed to pass in isolation 3/3 times and in a full-suite re-run
  immediately after — not caused by this change); second full-suite run:
  **40/40 passed**.

**Bucket:** test-environment assumption (the `QSettings` NativeFormat
family the owner's brief specifically flagged) — systemic, fixed here.

---

## Comparison to other-platform baselines

- **Linux UAT:** passes in CI (and locally here, 40/40 twice, modulo the
  one confirmed-unrelated pre-existing flake noted above).
- **Wine UAT** (`docs/backlog/2026-07-24-wine-uat-failures-triage.md`): 17
  failures, non-gating. `test_uat_foundations` is on **both** the Wine
  17-failure list and this macOS run's failure — but the failure *modes*
  differ: Wine's is "no captured output despite `--output-on-failure`" at
  ~93s (grouped with `test_uat_search_and_markup`'s near-identical
  duration, read by that item as a probable parallel-execution
  timeout/watchdog artifact, not a per-test assertion failure), while
  macOS's is a SEGFAULT with a clear per-sub-case failure and crash
  location. **Not the same failure, and not strong evidence of a shared
  root cause** — per the brief's own framing, a shared failure across two
  non-native environments is suggestive when the *mode* also matches; here
  it doesn't. It is worth noting as a soft signal that `test_uat_foundations`
  (the heaviest window/menu/lifecycle UAT binary) is disproportionately
  fragile across non-Linux environments in general — but that's an
  observation, not a diagnosis.
- `test_uat_file_menu_ia` is on the Wine failure list but **passed cleanly**
  on macOS in this run — further evidence against a simple "the test is
  bad everywhere" narrative; each non-Linux environment has its own,
  largely distinct failure set.
- **No macOS failure in this run matches any of the other 16 Wine
  failures** individually (list: `test_uat_search_and_markup`,
  `test_uat_password`, `test_uat_autofill`, `test_uat_background_removal`,
  `test_uat_recognize_text`, `test_uat_external_change`,
  `test_uat_ml_affordances`, `test_uat_pdf_pages`,
  `test_uat_page_change_signal`, `test_uat_two_page_dpr{1,1.5,2}`,
  `test_uat_empty_state`, `test_uat_zoom_indicator`,
  `test_uat_empty_state_recent`) — all of those passed cleanly on macOS in
  this run.

## Infrastructure/environment bucket

**Empty for this run.** No failure here is attributable to a missing tool,
an unavailable permission (e.g. macOS screen-recording/accessibility, which
a headless runner cannot grant), or any other environment-provisioning gap.
Build, bundling, and all 62 unit tests succeeded outright; the UAT suite ran
to completion (ctest itself did not crash or hang) with 39/40 sub-binaries
clean.

## Non-gating in the nightly pipeline (owner decision, 2026-07-26)

Per the owner's explicit call: macOS UAT becomes **non-gating** for
`nightly.yml`, mirroring Wine UAT's existing precedent
(`docs/backlog/2026-07-24-wine-uat-failures-triage.md`, 2026-07-24 decision).
Rationale: the macOS build+bundle+unit lane was fully green on this run, but
the UAT crash withheld an otherwise-good macOS nightly artifact — and macOS
UAT has never once passed in this project's history, so gating on it would
withhold every macOS nightly indefinitely. `nightly.yml`'s macOS "UAT suite"
step now carries `continue-on-error: true` exactly like Windows' "UAT suite
(Wine)" step, with the real pass/total counts (and an explicit crash flag,
since a SegFault doesn't reliably reduce to a clean N/M count) surfaced
non-silently in the release body's per-OS status table. **This is a change
to which nightly artifacts get published**, not a cosmetic CI tweak — see
the implementing PR body. It reverts to gating once this item's Threshold
is met.

**Parsing bug found during self-review — fixed in BOTH the macOS and the
pre-existing Wine step, not just the new one:** a straight copy of the
Wine step's original ctest-summary parse (`grep -oE '[0-9]+ tests
failed'`) would have silently misreported a clean macOS UAT pass as
"count unavailable" — this run's own unit-test step logged `100% tests
passed out of 62`, with the `"N tests failed"` clause omitted entirely
(apparently a ctest-version-dependent formatting difference; a local Linux
ctest run in this same PR printed the fuller `100% tests passed, 0 tests
failed out of 62` for the identical 0-failures case).

That is not just a new-code risk: the **pre-existing** Wine step
(`nightly-windows`' "UAT suite (Wine)" step, live on `main` since
2026-07-24) has the exact same bug, and it is *latent, not dormant* — the
explicit goal of `docs/backlog/2026-07-24-wine-uat-failures-triage.md` is
to drive Wine UAT to 100%, and the day it gets there, the old parser's
`grep -oE '[0-9]+ tests failed'` would match nothing, `FAILED` would go
empty, and the Windows row would report a broken/"unavailable" count in
every subsequent nightly release body — a silent failure mode that only
triggers on success, precisely the moment the count matters most. Fixed
here, in this same PR, because it's the same code path: both steps now
call a shared `scripts/parse-ctest-uat-summary.sh` (factored out rather
than maintaining two copies that can drift), which treats a present
`"out of T"` with the leading percentage reading exactly `100` as `F=0`,
and falls through to "unknown" for anything else where the `"tests
failed"` clause is missing (a defensive branch for a genuinely
garbled/truncated line, not a real observed ctest format). Verified
against six scenarios for each step (real captured log, full pass with
the clause, full pass without it, empty log, garbled non-100% with no
clause, and a normal partial-fail case) by extracting each step's `run:`
script and executing it directly against fixture logs.

## Recommendation summary

1. **Finding 1 (crash) — do not fix blind, and treat as potentially
   product-level.** Get a symbolicated backtrace or ASan report on the mac
   runner first; the current log evidence supports a hypothesis (see
   above) but not a diagnosis, and — per the "why this matters more"
   callout above — if the fault turns out to be in production teardown
   code rather than the test's own dereference, this is a real crash a
   Mac user could hit, not just a test-hygiene gap.
2. **Finding 2 (stale `&New` assertion) — trivial, ready for pickup.**
   One-line-ish test edit, fully diagnosed above; deliberately not bundled
   into this PR to keep it scoped to the systemic QSettings fix + the
   nightly.yml gating change.
3. **QSettings/IniFormat family — fixed in this PR**, verified on Linux,
   preventive (didn't cause any failure observed in this run, but closes a
   real persistent-runner pollution risk matching PR #123's precedent).
4. **ctest-summary parsing bug (Wine + macOS) — fixed in this PR**, in
   `scripts/parse-ctest-uat-summary.sh`, shared by both nightly lanes;
   without it, the Wine UAT count would have silently broken the day
   `docs/backlog/2026-07-24-wine-uat-failures-triage.md`'s own goal (a
   clean Wine UAT run) was reached.
