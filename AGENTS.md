# Agent Guide — Trailer

This file is for AI coding agents (Claude Code, Copilot SWE Agent, Cursor,
and any future tool) working in this repository. Read this first; the
specifics below save you from rediscovering them every session.

`CLAUDE.md` at the repo root is a symlink to this file.

---

## What Trailer is

Trailer is a cross-platform Qt 6 / C++20 desktop app for opening, viewing,
marking up, signing, redacting, and exporting PDFs and images. See
[`DESIGN.md`](DESIGN.md) for the full spec and [`PHILOSOPHY.md`](PHILOSOPHY.md)
for the hard constraints (read both before making non-trivial changes).

## Phase status (2026-05)

- **Phases 0–5 done.** Foundations, viewer, PDF page operations, image
  editing, markup/annotations, forms / signatures / password / redaction.
- **Phase 6 in flight.** ML core landed (U²-Net background removal,
  MobileSAM Smart Lasso / Instant Alpha, PP-OCRv3 OCR). The format and
  colour half (HEIC, OpenEXR, RAW, lcms2 colour management, OCR-embed-on-
  export, alt text) is unstarted.
- **Phase 7 (stretch)** and **Phase 8 (distribution polish)** are partially
  scoped. Linux DEB pipeline exists; macOS notarised .dmg and Windows
  MSIX/NSIS do not.

The README still mentions "Phase 0" in places; trust this file and
[`TODO.md`](TODO.md) over the README's status framing.

## Hard constraints — non-negotiable

From [`PHILOSOPHY.md`](PHILOSOPHY.md). Any PR that violates these will be
rejected, regardless of how cleanly it implements its stated feature.

- **No ads, ever.** No promotional surface anywhere in the app.
- **No telemetry.** No analytics, no crash phone-home, no "anonymous usage
  statistics." No new outbound network calls without an explicit, off-by-
  default user toggle.
- **No accounts.** Trailer never asks the user to sign in.
- **No cloud sync.** Files stay on the user's device.
- **No premium / pro tier.** All features ship to everyone.
- **No model training on user content.** All ML runs locally via ONNX.
  Model weights are downloaded once on first use with explicit consent.
- **No popup that just says "no".** If a menu item or button can't act
  in the current context — wrong document type, ML policy says Never
  Download, format doesn't support the operation — disable the control
  and set a `setToolTip(...)` explaining where to go. Never let the
  user click and hit a popup that exists only to say the feature isn't
  available right now. See PHILOSOPHY.md → *How Trailer reduces friction*.

If a feature you're asked to implement seems to brush against any of
these, stop and ask in the PR description before writing code.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Requirements: CMake 3.24+, Qt 6.5+ with `Core Gui Widgets Test Pdf
PdfWidgets PrintSupport`, qpdf 11+, a C++20 compiler. `qtpdf` is a
separate module in many Qt distributions — install it explicitly if
`find_package(Qt6 COMPONENTS Pdf)` fails (see README).

The build treats warnings as errors (`-Werror` / `/WX`). CI will fail on
any new warning; fix them at the source rather than disabling the flag.

## Test

Unit + UAT split. Unit tests run on every PR; UAT runs only on tag pushes
and locally.

```sh
# Unit tests only (what CI runs on PRs)
ctest --test-dir build -LE uat --output-on-failure

# UAT suite (offscreen Qt, no display server needed)
QT_QPA_PLATFORM=offscreen ctest --test-dir build -L uat --output-on-failure

# Or the wrapper, which can also run UAT inside Docker
scripts/run-uat.sh           # docker
scripts/run-uat.sh --host    # native
```

UAT specs live in [`docs/uat/`](docs/uat/); the harness is
[`tests/uat/`](tests/uat/). Each harness slot is named `uat_<area>_<NNN>_*`
and maps to a `UAT-<AREA>-<NNN>` case in the spec docs. The harness binary
must be labelled `uat` in `tests/uat/CMakeLists.txt` via the
`trailer_add_uat_test()` helper. See
[`.claude/agents/uat-author.md`](.claude/agents/uat-author.md) for the
template.

## Layout

```
src/
  app/             # Application lifecycle, QApplication subclass, file-open routing
  document/        # IDocument, adapters (PdfAdapter, ImageAdapter, StubAdapter),
                   # PdfEditor + PdfCommand pattern, qpdf wrapper
  ui/              # MainWindow, DocumentView, Sidebar, AnnotationOverlay,
                   # Inspector, MarkupToolbar, SignatureCaptureDialog, etc.
  ml/              # OnnxSession, ModelRegistry, ModelDownloader,
                   # BackgroundRemover, SamSession, OcrEngine
  settings/        # Persistence (QSettings + a toml++ store for richer state)
  recent/          # RecentFiles model
tests/             # Unit tests, one file per src/ class roughly
tests/uat/         # UAT harness (offscreen, label=uat)
docs/uat/          # UAT specs — source of truth for end-user behaviour
icon/              # App icon pipeline (Python; see icon/README.md)
docker/            # Cross-compile + UAT runner images
packaging/         # Linux DEB scaffolding
platform/          # OS-specific bits (e.g. screenshot pickers)
scripts/           # build-linux-deb.sh, build-windows.sh, run-uat.sh
```

## Conventions

**Branches.** Agent branches follow `<tool>/<slug>` — `claude/<name>-<hash>`,
`copilot/<feature>`, `cursor/<feature>`. The `worktree-agent-<hash>` prefix
is reserved for the spawner that manages parallel agent sessions in
`.claude/worktrees/`.

**Commits.** Conventional-style prefixes when they fit (`feat:`, `fix:`,
`docs:`, `ci:`, `deb:`), but a clear English subject line beats a forced
prefix. Subject lines stay under ~72 chars. Body explains *why* if not
obvious.

**HITL (human-in-the-loop) reviews.** The maintainer runs periodic review
passes against the live app — see the `HITL round N` commits and the
`2026-04-30 HITL pass` section in `TODO.md`. Agents should:

1. Batch related work into a single PR rather than micro-PRs.
2. Mark the PR ready for HITL review when build + unit tests pass.
3. Wait for the maintainer's review pass; don't auto-merge.
4. Add UAT cases for any user-visible change (see `docs/uat/README.md`).

**Screenshots for UI-visible changes.** When a PR adds, removes, or
visibly reshapes a dialog, menu, toolbar, table, or other on-screen
element, include screenshots (and/or short captures) in the PR body so
reviewers can see the change without building. Skip screenshots for
non-visual changes (build, refactors with identical UI, internals).
Capture options:

- Build the app and grab the running window (preferred when the
  change is reachable from a normal flow).
- Drive an offscreen QWidget under `QT_QPA_PLATFORM=offscreen` and
  call `widget->grab().save("…png")` — works for isolated dialogs
  without a real display. The `tests/uat/` harness uses the same
  pattern.

Drop the PNGs into the PR body via GitHub's drag-and-drop upload (or
attach via `gh pr edit --body-file`); do not commit screenshots into
the repo unless they are reference/design artefacts.

**Undo.** Two undo stacks coexist:
- `AnnotationStore` for annotation create/modify/delete.
- `PdfCommand` (in `src/document/PdfCommands.h`) for page-level qpdf
  mutations.

The unified `IDocument::undo` / `redo` routes to the most-recently-touched
stack via an `m_lastUndoSource` heuristic. This is fragile — see TODO.md
"PDF undo/redo — rotate done, others scoped."

**Networking.** Trailer makes exactly one kind of outbound call: ONNX
model downloads via `ModelDownloader`, gated on explicit user consent
with the URL displayed before download. Do not add network code in any
other path without raising it in the PR first.

## Where to look

| You need to… | Read… |
|---|---|
| Understand the product end-to-end | `DESIGN.md` |
| Know what's off-limits | `PHILOSOPHY.md` |
| Pick up open work | `TODO.md` (HITL section is the live sprint) |
| Write a UAT case | `docs/uat/README.md` + a sibling spec file |
| Write a UAT harness slot | `tests/uat/test_uat_foundations.cpp` is the template |
| Add a PDF command | `src/document/PdfCommands.h` + `RotatePageCommand` impl |
| Add an annotation type | `src/ui/AnnotationOverlay.cpp`, `Annotation.h`, `AnnotationStore` |
| Add a Qt-PDF rendering path | `src/document/PdfAdapter.cpp`, the QPdfView wiring |
| Touch ML | `src/ml/` — keep all inference local, no remote calls |

## Slash commands & subagents

If you are running inside Claude Code:

- `/check-build` — runs the four pre-approved cmake/ctest commands.
- `/run-uat` — wraps `scripts/run-uat.sh`.
- `/hitl-review` — produces a HITL-style review checklist for the current
  diff (build, ctest, UAT delta, philosophy check).

Subagents live in [`.claude/agents/`](.claude/agents/). Currently defined:

- `uat-author` — turns a `UAT-<area>-<NNN>` spec case into a harness slot.
- `qpdf-binding-author` — implements `PdfCommand` subclasses for qpdf-
  level page mutations (delete / move / insert / crop), following the
  `RotatePageCommand` template.
- `annotation-overlay-fixer` — owns `AnnotationOverlay` rendering,
  hit-testing, focus-loss handling, and the `/AP` appearance stream
  pipeline.

Other agent products (Copilot SWE Agent, Cursor) should rely on this file
plus the slash commands' descriptions; the workflows they encode are
tool-agnostic.

## When in doubt

Open a draft PR with the diff and a question in the description. The
maintainer's HITL cadence catches most issues; agents should optimize for
"the next reviewer can tell what I was trying to do" rather than "this
merges itself."
