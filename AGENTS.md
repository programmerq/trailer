# AGENTS.md: working on Trailer

How to work on Trailer, for coding agents and people. `CLAUDE.md` is just `@AGENTS.md`.
What the product is and the rules that decide what it does: [`PRODUCT.md`](PRODUCT.md).
All open work: [`TODO.md`](TODO.md). Release runbook: [`RELEASING.md`](RELEASING.md).

## What Trailer is

macOS Preview, cross-platform: open, read, mark up and lightly edit PDFs and images on macOS,
Windows and Linux with Preview's friction-free feel, so one set of habits works everywhere.
Qt 6, C++20. Primary user today: the maintainer. Later: a wide audience that must not go "oof".

## Priorities, in order (the higher one wins)

1. **Files are safe and the basics work.** Save writes what the user sees, once, and only when
   asked. Undo reverts the last action, also across a Save. Marks and selections land where the
   user pointed. Controls never jump. No status-bar cruft, no popup that only says "no".
2. **Performance by construction.** Page one costs the same for a 100,000-page file as for a
   1-page file, apart from the PDF engine's cross-reference parse. Before the first frame only
   the visible pages are touched; the GUI thread never calls the PDF engine or reads files; each
   scroll, zoom or resize does O(visible pages + log N) work. Idle means idle: no polling, no
   background work the user didn't cause. No millisecond budgets: count the work (see Test).
3. **Platform-native shape.** Each OS gets its own menus, dialogs and shortcuts, and every
   feature ships on every OS.

ML (OCR, background removal, Smart Lasso) is progressive enhancement: the app is fully usable
without any model.

## Hard constraints

- No ads, telemetry, crash upload, accounts, cloud sync, premium tier or DRM. Never train on
  user content; all inference runs locally.
- Exactly two outbound network paths exist; a third needs the owner's sign-off.
  `src/ml/ModelDownloader.cpp` downloads a model once, after consent (HTTPS, URL shown,
  SHA-256 checked). `src/update/UpdateChecker.cpp` reads an ed25519-signed feed: auto-check off
  by default, compiled out without `TRAILER_UPDATE_PUBKEY`. Review: `grep -rn QNetworkAccessManager src`.
- MIT licence; Qt is linked dynamically (LGPL). New dependencies go in `THIRD_PARTY_LICENSES.md`
  and `licenses/third-party/`.
- A control that can't act now is disabled, with a tooltip saying why and where to go. Menus keep
  every item in place across documents and modes (a window can hold a PDF tab and an image tab).
  Never quietly do a nearest-equivalent of the request. No disabled stubs for features that
  don't exist.
- "The document is the subject; the furniture doesn't move." No permanent chrome repeating what
  the document shows. No control moves because unrelated state changed.

## The tracker is TODO.md, and nothing else

All open work lives in [`TODO.md`](TODO.md); its header says how entries are written, added and
closed. Don't create other tracking files: no backlog folders, roadmaps, per-feature todo lists,
decision-record folders or audit dumps in the repo. CI fails if one appears. A decision worth
keeping is a short comment next to the code it governs, pinned by a test.

Open bugs have failing-first tests: `tests/test_known_bugs_*.cpp`, ctest label `known-bug`.
Each test asserts the correct behaviour and marks today's failure with
`QEXPECT_FAIL("", "T-NN: ...", Abort)`. When a change fixes T-NN, that check passes unexpectedly
and the test fails, so the fixing PR deletes the `QEXPECT_FAIL` line and the TODO.md entry.
Every id cited outside TODO.md (a `// T-NN:` comment, a test marker) must be an open entry, and
CI checks this, so the PR that closes an entry also updates what cites it.

## Done means a check someone else can run

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
  GUI thread, bytes read before the first frame. Report wall time; never gate on it.

Cover each of these, or say why it doesn't apply:
- **Every view:** Continuous, Single Page, Two Pages, and image documents.
- **HiDPI:** dpr 1 and 2 (`trailer_register_uat_dpr_matrix()`), and logical DPI 72 (macOS) and
  96 (Windows, Linux; set with `QT_FONT_DPI`). Sharpness depends on dpr; geometry depends on DPI
  and zoom mode.
- **Text:** native and OCR text can be selected, copied and searched in every view.
- **Data and undo:** what is on disk after Save, Close, Quit, ⌥⌘Q and a crash. ⌘Z reverts
  exactly that operation.
- **Furniture:** a test compares neighbouring controls' `geometry()` before and after (the
  constancy sweep has a row per toggle).
- **States and OS:** the empty, loading and error states touched, and where the feature lives
  on macOS, Windows and Linux.

For a visible change, put before/after screenshots in the PR description when you can attach
images (an offscreen `QWidget::grab()` is fine). Don't commit screenshots to the repo.

## Build

Needs CMake ≥ 3.24, a C++20 compiler, qpdf ≥ 11, ONNX Runtime 1.25, and Qt ≥ 6.6 with Core,
Concurrent, Gui, Widgets, Network, Svg, Test, Pdf, PdfWidgets and PrintSupport. Many distros
package QtPdf separately.

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="<qt>;<ort>"
    cmake --build build --parallel 4      # always pass a number: bare -j/--parallel is unbounded

- **Cloud sessions (Claude Code on the web):** the SessionStart hook runs
  `scripts/session-setup.sh`. It installs Qt 6.11 with QtPdf, qpdf, GCC, CMake and Ninja from
  conda-forge, and ONNX Runtime from NuGet, into `/opt`, then writes
  `/etc/profile.d/trailer-toolchain.sh`. If CMake can't find Qt, `source` that file. Build with
  its compiler: the conda Qt needs its own libstdc++.
- **ONNX Runtime:** found via `CMAKE_PREFIX_PATH` or `TRAILER_ORT_ROOT`; otherwise configure
  downloads it (SHA-pinned). There is no upstream macOS x86_64 build, and no ML-off option yet.
- **qpdf 10** passes configure, then fails at link (`QPDFFormFieldObjectHelper::isChecked`).
- **Options:** `TRAILER_WERROR` (off; Qt/qpdf headers are noisy), `TRAILER_ENABLE_UX_RECORDER`
  (off; developer tool, never shipped), `TRAILER_ADAPTIVE_ICON` (macOS). `-Werror=switch` is
  always on.
- **Packages:** `scripts/build-macos.sh` (arm64, macOS ≥ 14.0); `scripts/build-windows-native.ps1`
  (releases use the Docker cross-build, `scripts/build-windows.sh`); `scripts/build-linux-deb.sh`
  and `packaging/rpm/`. `make help` lists the rest.

## Test

    ctest --test-dir build -LE 'uat|advisory' --output-on-failure                     # unit + known-bug (PR CI)
    QT_QPA_PLATFORM=offscreen ctest --test-dir build -L uat --output-on-failure       # UAT (release)
    QT_QPA_PLATFORM=offscreen ctest --test-dir build -L advisory --output-on-failure  # never gates

- UAT runs the real `Application` and `MainWindow` in-process under the offscreen platform. It
  includes the modal census, which fails when a new modal appears, and the constancy sweep,
  which fails when a control moves because unrelated chrome toggled. After an intended change,
  regenerate a census golden (`tests/uat/census/`) with `TRAILER_CENSUS_WRITE=1` and show its
  diff in the PR.
- `advisory` tests never gate, and no CI lane runs them: the chrome census and the seeded random
  UI walk (`tests/pbt/`; replay a failure with `TRAILER_PBT_SEED`). What they find becomes a
  TODO.md entry and a known-bug test.
- `-L`/`-LE` match labels as regexes, so any label containing `uat` joins UAT.
- Every confirmed bug gets a test: a known-bug test while it is open, a regression test once
  fixed. Docs-only changes skip CI (`paths-ignore`).

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
- **`IDocument` is not a QObject:** drop held pointers on `documentAboutToBeRemoved`. Reach
  objects via the current document, since `MainWindow::findChild<…>()` searches every tab.
- **Enum switches** have no `default:`, so `-Werror=switch` flags unhandled modes.
  `AnnotationType` and `ModelId` are persisted: append only. New setting keys go in
  `Settings::volatilityOf`.
- **Status bar:** `QStatusBar` permanent widgets move as one block. A transient readout is an
  overlay (`repositionZoomIndicator`).
- **Menus:** call `QMenu::setToolTipsVisible(true)`, or disabled items can't explain themselves.
  A per-window `QAction` with `ApplicationSpecificRole` duplicates in the macOS app menu.
- **Windows:** `QTemporaryFile` holds its handle past `close()`, so qpdf can't reopen it: use
  `trailer::ScopedTempFile`. Offscreen tests need `QT_QPA_FONTDIR=%SystemRoot%\Fonts`.
- **Unsaved-changes prompt:** never shown under the offscreen or minimal platforms. Tests force
  an answer with `CloseResponse`; under any other platform plugin a dirty-close test blocks on
  the modal.
- **Screen capture:** a macOS Screen Recording grant applies from the next launch. XWayland
  grabs come back black, so Linux capture uses the XDG portal.
- **Releases:** never squash a release PR, because autotag tags the PR's HEAD SHA. Never tag a
  `-dev` version.
- **History:** the decision records, gates (G1–G10), UAT spec docs and old `docs/` tree were
  deleted in the October 2026 reboot, and comments now state their rules inline. The old files
  are in git history (`git show 09bc1e3:<path>`). Don't restore them or cite them.

## Working rules

- One change, one PR, and each PR mergeable on its own. Don't stack PRs.
- The owner reviews and merges. Don't merge your own PR.
- Build and run the unit tests before pushing.
- Open the PR description with what you need from the reviewer, or "Nothing; ready to merge".
  Then say what changed for the user, then show the evidence.
- When unsure, open a draft PR with the question at the top.
- Push back. When code, a test or a measurement contradicts PRODUCT.md, this file or the
  request, say so with the evidence instead of working around it.
