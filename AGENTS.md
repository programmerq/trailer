# AGENTS.md: working on Trailer

How to work on Trailer, for coding agents and people. `CLAUDE.md` is just `@AGENTS.md`.
What the product is and the rules that decide what it does: [`PRODUCT.md`](PRODUCT.md).
All open work: [`TODO.md`](TODO.md). Release runbook: [`RELEASING.md`](RELEASING.md).

## What Trailer is

macOS Preview, cross-platform: open, read, mark up and lightly edit PDFs and images on macOS,
Windows and Linux with Preview's friction-free feel, so one set of habits works everywhere.
Qt 6, C++20. The owner, @programmerq, is the maintainer and today's primary user. Later: a wide
audience that must not go "oof".

## What wins, in order

The hard constraints below are never traded away. Within them:

- **First, files are safe and the basics work.** Save writes what the user sees, once, and only
  when asked. Undo reverts the last action, also across a Save. Marks and selections land where
  the user pointed. Controls never jump. No status-bar cruft, no popup that only says "no".
- **Then, performance by construction.** Page one costs the same for a 100,000-page file as for
  a 1-page file, apart from the PDF engine's cross-reference parse (off the GUI thread) and a few
  instructions of bookkeeping per page. Before the first frame only the visible pages and a fixed
  prefetch are touched. While opening, painting, scrolling, zooming, resizing or switching modes,
  the GUI thread never calls the PDF engine or reads files, and each scroll, zoom or resize does
  O(visible pages + log N) work. Idle means idle: no polling, no background work the user didn't
  cause. No millisecond budgets: count the work (see Test).
- **Then, platform-native shape.** Each OS gets its own menus, dialogs and shortcuts, and every
  feature ships on every OS.

ML (OCR, background removal, Smart Lasso) is progressive enhancement: the app is fully usable
without any model.

## Hard constraints

- No ads, telemetry, crash upload, accounts, cloud sync, premium tier or DRM. Never train on
  user content; all inference runs locally.
- Exactly two outbound network paths exist. A third needs the owner's sign-off, and like these
  two it stays off until the user opts in.
  - `src/ml/ModelDownloader.cpp` downloads a model once, after consent (HTTPS, URL shown, SHA-256
    checked).
  - `src/update/` reads an ed25519-signed feed and downloads the update it names. Auto-check is
    off by default; a build without `TRAILER_UPDATE_PUBKEY` never connects, and its Check for
    Updates is disabled.
  - Review: `grep -rnE 'QNetwork|QTcpSocket|QUdpSocket|QSslSocket|QWebSocket|QDnsLookup' src`
    lists only those (T-85 will make CI check it).
- MIT licence; Qt is linked dynamically (LGPL). New dependencies go in `THIRD_PARTY_LICENSES.md`
  and `licenses/third-party/`.
- A control that can't act now is disabled, with a tooltip saying why, and where to go when
  there's somewhere to go. Standard edit commands (Undo, Redo, Cut, Copy, Paste) just dim. Menus
  keep every item in place across documents and modes; a window can hold a PDF tab and an image
  tab. Never quietly do a nearest-equivalent of the request. No disabled stubs for features that
  don't exist.
- "The document is the subject; the furniture doesn't move." No permanent chrome repeating what
  the document shows. No control moves because unrelated state changed.

## The tracker is TODO.md, and nothing else

All open work lives in [`TODO.md`](TODO.md); its header says how entries are written, added and
closed. Don't create other tracking files: no backlog folders, roadmaps, per-feature todo lists,
decision-record folders or audit dumps in the repo. CI fails if one appears.

Decisions live where they apply: a decision about code is a short comment next to that code,
pinned by a test; a product decision goes in PRODUCT.md. A "Decided:" note in a TODO.md entry
moves to one of those when the entry closes.

Open bugs get failing-first tests: `tests/test_known_bugs_*.cpp`, ctest label `known-bug`. File
one with the entry whenever the bug can be reproduced offscreen; otherwise the entry says why
not. Each test asserts the correct behaviour and marks today's failure with
`QEXPECT_FAIL("", "T-NN: ...", Abort)`. When a change fixes T-NN, that check passes unexpectedly
and the test fails, so the fixing PR deletes the `QEXPECT_FAIL` line and the TODO.md entry; the
check stays where it is as the regression test.

Every id cited anywhere outside TODO.md and CHANGELOG.md (a `// T-NN:` comment, a test marker,
a doc) must be an open entry, so the PR that closes an entry also updates what cites it.
`scripts/check-one-tracker.sh` checks this, and runs in CI on every PR.

## Every change carries a check someone else can run

Before coding, the TODO.md entry or the PR description states the pass/fail line: a state a
reviewer can reach and what they observe there. Never an adjective like "smooth".

Measure the real thing through the real entry point:
- Drive the app the way a user does: the MainWindow action or menu item, and real key and mouse
  events at positions found from rendered pixels. `tests/test_known_bugs_window.cpp` and
  `tests/uat/` show how to do this offscreen.
- Check real output: rendered pixels, clipboard text, and the saved file's bytes and annotations
  re-read from disk. A re-implemented mapping, a stubbed provider or the app's own bookkeeping is
  not an oracle. Tests built that way passed while users hit the bugs.
- For performance, count instead of timing: pages touched, engine calls and file reads on the
  GUI thread, bytes read before the first frame. Put the counts in the PR description, with wall
  time and the machine it came from for information; never gate on wall time.

Cover each of these, or say in the PR description why it doesn't apply:
- **Every view:** Continuous, Single Page, Two Pages, and image documents.
- **HiDPI:** dpr 1 and 2 (`trailer_register_uat_dpr_matrix()`), and logical DPI 72 (macOS) and
  96 (Windows, Linux; set with `QT_FONT_DPI`). Sharpness depends on dpr; geometry depends on DPI
  and zoom mode.
- **Text:** native and OCR text can be selected, copied and searched in every view.
- **Data and undo:** what is on disk after Save, Close, Quit, Quit and Keep Windows, and a crash.
  ⌘Z reverts exactly that operation.
- **Furniture:** a test compares neighbouring controls' `geometry()` before and after (the
  constancy sweep has a row per toggle).
- **States and OS:** the empty, loading and error states touched, and where the feature lives
  on macOS, Windows and Linux.

For a visible change, put before/after screenshots in the PR description when you can attach
images (an offscreen `QWidget::grab()` is fine). Don't commit screenshots to the repo.

## Build

Needs CMake ≥ 3.24, a C++20 compiler, qpdf ≥ 11, ONNX Runtime 1.25, and Qt ≥ 6.6 with Core,
Concurrent, Gui, Widgets, Network, Svg, Test, Pdf, PdfWidgets and PrintSupport (and DBus on
Linux). Many distros package QtPdf separately, and Ubuntu 24.04's Qt (6.4) is too old.

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="<qt>;<ort>"
    cmake --build build --parallel 4      # always pass a number: bare -j/--parallel is unbounded

- **Cloud sessions (Claude Code on the web):** the SessionStart hook runs
  `scripts/session-setup.sh`. It installs Qt 6.11 with QtPdf, qpdf 11.9, GCC, CMake and Ninja from
  conda-forge, and ONNX Runtime from NuGet, into `/opt`, then writes
  `/etc/profile.d/trailer-toolchain.sh`. If CMake can't find Qt, `source` that file. Build with
  its compiler: the conda Qt needs its own libstdc++.
- **ONNX Runtime:** found via `CMAKE_PREFIX_PATH` or `TRAILER_ORT_ROOT`; otherwise configure
  downloads it (SHA-pinned). There is no upstream macOS x86_64 build, and no ML-off option yet.
- **qpdf:** configure accepts any version, but only 11 and later are supported (CI uses Ubuntu
  24.04's 11.9). qpdf 12 made `QPDFObjectHandle`'s accessors const and 11.9's aren't, so don't
  call them through a const handle: it compiles against 12 and fails on CI.
- **Options:** `TRAILER_WERROR` (off; Qt/qpdf headers are noisy), `TRAILER_ENABLE_UX_RECORDER`
  (off; developer tool, never shipped), `TRAILER_ADAPTIVE_ICON` (on; macOS; needs full Xcode, so
  turn it off with only the Command Line Tools). `-Werror=switch` is always on.
- **Packages:** `scripts/build-macos.sh` (arm64, macOS ≥ 14.0); `scripts/build-windows-native.ps1`
  for MSVC on Windows; `scripts/build-windows.sh` cross-builds with mingw-w64 (in Docker, or
  `--in-container` on a prepared runner, as CI and releases do); `scripts/build-linux-deb.sh`
  and `packaging/rpm/`. `make help` lists the rest.

## Test

    QT_QPA_PLATFORM=offscreen ctest --test-dir build -LE 'uat|advisory' --output-on-failure  # unit + known-bug
    QT_QPA_PLATFORM=offscreen ctest --test-dir build -L uat --output-on-failure              # UAT
    QT_QPA_PLATFORM=offscreen ctest --test-dir build -L advisory --output-on-failure         # never gates

Without `offscreen`, a test that closes a dirty window waits on the unsaved-changes prompt.

- **What runs where.** PR CI (`ci.yml`) runs the unit and known-bug tests on Linux, plus the
  modal census and the constancy sweep, and the unit tests under Wine. Nightly runs the unit
  tests and UAT on all three OSes (UAT gates only on Linux); the release gate runs UAT on Linux.
  Windows and macOS lanes skip `known-bug`: those expected failures are verified on Linux only,
  and Wine also skips `perf`. Changes to only `*.md` or
  `docs/**` skip `ci.yml`, but `tracker.yml` (the one-tracker check) runs on every PR.
- **UAT** runs the real `Application` and `MainWindow` in-process under the offscreen platform.
  Run it before pushing a change to `src/ui/` or `src/app/`. The modal census fails when a modal
  call site appears or goes; the constancy sweep fails when a control moves because unrelated
  chrome toggled. After an intended change, regenerate the census golden in `tests/uat/census/`
  (`modal-census.json`, or `chrome-census.json` for the advisory chrome census) with
  `TRAILER_CENSUS_WRITE=1` and show its diff in the PR.
- **Advisory** tests never gate, and no CI lane runs them: the chrome census and the seeded
  random UI walk (`tests/pbt/`; replay a failure with `TRAILER_PBT_SEED`). What they find becomes
  a TODO.md entry, with a known-bug test once it reproduces deterministically.
- **Labels** are set where tests are registered: `trailer_add_known_bug_test` in
  `tests/CMakeLists.txt`, `trailer_add_uat_test` in `tests/uat/CMakeLists.txt`,
  `trailer_add_pbt_test` in `tests/pbt/CMakeLists.txt`. `-L`/`-LE` match labels as regexes, so
  never use a new label containing `uat` or `advisory` ("evaluation" would join UAT).
- Every confirmed bug gets a test: a known-bug test while it is open, a regression test once
  fixed.

## Code map

    src/app/         lifecycle, file-open routing, menus shared with the no-window state
    src/document/    IDocument, PdfAdapter, ImageAdapter, PdfEditor (qpdf), PdfCommand, RecoveryStore
    src/annotation/  annotation types, AnnotationStore
    src/ui/          MainWindow (5.7k lines: split it, don't grow it), DocumentView, overlays, dialogs
    src/ml/          MlScheduler (one worker, priority queue), OnnxSession, ModelRegistry/Downloader
    src/update/      signed update feed, checker, installer hand-off
    src/platform/    Foo.mm + Foo_stub.cpp per native feature;  src/settings/  settings.toml, AppPaths
    src/uxrecord/    developer-only UX session recorder (compiled out of releases)

Undo is one chronological log per document over annotation snapshots, `PdfCommand`s and image
pixel snapshots. ⌘Z pops the newest entry of any kind. Bounded stacks evict in lockstep.
Saving: only an explicit Save or Save As writes the user's file. The 30 s auto-save writes a
`RecoveryStore` sidecar. Don't add another path that writes the user's file.

## Gotchas that cost real time

- **QPDF is not thread-safe**, even for reads. Background work opens its own qpdf instance.
- **Worker → GUI:** use `MlScheduler::postResultToGuiThread()` and re-check a `QPointer`. Never
  `QMetaObject::invokeMethod(…, Qt::QueuedConnection)` from a worker (macOS crash).
- **`IDocument` is not a QObject:** drop held pointers on `documentAboutToBeRemoved`. Reach a
  tab's widgets from `DocumentView::currentWidget()`, never `MainWindow::findChild<…>()`: every tab
  has its own, and in a two-tab window that finds the hidden tab's.
- **Enum switches** have no `default:`, so `-Werror=switch` flags unhandled modes.
  `AnnotationType` and `ModelId` are persisted: append only. New setting keys go in
  `Settings::volatilityOf`.
- **Shortcuts:** `Qt::ControlModifier` is ⌘ on macOS and Ctrl elsewhere; `Qt::MetaModifier` is ⌃
  on macOS and the Windows/Super key elsewhere (T-131).
- **Status bar:** `QStatusBar` permanent widgets move as one block. A transient readout is an
  overlay (`repositionZoomIndicator`).
- **Menus:** call `QMenu::setToolTipsVisible(true)`, or disabled items can't explain themselves.
  A per-window `QAction` with `ApplicationSpecificRole` duplicates in the macOS app menu.
- **Windows:** `QTemporaryFile` holds its handle past `close()`, so qpdf can't reopen it: use
  `trailer::ScopedTempFile`. Offscreen tests need `QT_QPA_FONTDIR=%SystemRoot%\Fonts`.
- **Unsaved-changes prompt:** never shown under the offscreen or minimal platforms. Tests force
  an answer with `CloseResponse`; under any other platform plugin a dirty-close test blocks on
  the modal.
- **Other dialogs in tests** (a file dialog, Export As's options, a replace prompt) wait for an
  answer, and the test hangs until QTest's watchdog kills it after 5 minutes. Answer them from
  a timer that polls `QApplication::activeModalWidget()`, which keeps firing inside the dialog's
  event loop, like `ModalSentinel` in `tests/uat/test_uat_modal_census.cpp`.
- **Screen capture:** a macOS Screen Recording grant applies from the next launch. XWayland
  grabs come back black, so Linux capture uses the XDG portal.
- **No macOS PR lane yet (T-58):** a change to `src/platform/*.mm` isn't compiled before merge,
  so say so in the PR. The Wine lane fails about one run in three; a failure in a test your
  change doesn't touch gets one re-run and a mention of T-58.
- **Releases:** never squash a release PR, because autotag tags the PR's HEAD SHA. Never tag a
  `-dev` version.
- **History:** the decision records, gates (G1–G10), UAT spec docs and the old `docs/` tree
  (all but `docs/set-as-default-app.md`) were deleted in the October 2026 reboot, and comments
  now state their rules inline. The old files are in git history (`git show 09bc1e3:<path>`).
  Don't restore them or cite them from code; a TODO.md Src may point at them for history.

## Working rules

- One change, one PR, and each PR mergeable on its own. Don't base a PR on another unmerged
  PR; if your change needs one, wait for it to merge or fold the work into it.
- The owner reviews and merges. Don't merge your own PR.
- Before pushing: build, run the unit tests and `scripts/check-one-tracker.sh`, and UAT when
  the change touches `src/ui/` or `src/app/`.
- Name the TODO ids a PR closes in its title and its commit subjects, e.g.
  `fix(T-05, T-97): …`, so the id survives any merge method.
- A user-visible change adds a line under `[Unreleased]` in `CHANGELOG.md`.
- Open the PR description with what you need from the reviewer, or "Nothing; ready to merge".
  Then say what changed for the user, then show the evidence.
- When unsure, open a draft PR with the question at the top.
- Push back. When code, a test or a measurement contradicts PRODUCT.md, this file or the
  request, say so with the evidence instead of working around it. Where PRODUCT.md and this file
  disagree, PRODUCT.md wins; fix the other one in the same PR.
