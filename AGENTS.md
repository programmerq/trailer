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
  scoped. Linux DEB / RPM, Windows MSI, macOS DMG pipelines all exist;
  macOS notarisation is **off the table by project policy** (Trailer is
  not enrolled in the Apple Developer Program); an **ed25519-signed
  auto-update channel** (Sparkle 2 + WinSparkle is the leading candidate
  — the *requirement* is the signed channel, not the specific library;
  see [`ROADMAP.md`](ROADMAP.md)) is the next planned distribution work.
  Velopack does NOT qualify under the no-Apple-Dev policy because it
  expects Developer ID / Authenticode trust.

For the tactical Now / Next / Later view of what's between releases,
read [`ROADMAP.md`](ROADMAP.md). For the live pickable punch list,
read [`TODO.md`](TODO.md).

## Hard constraints — non-negotiable

From [`PHILOSOPHY.md`](PHILOSOPHY.md). Any PR that violates these will be
rejected, regardless of how cleanly it implements its stated feature.

- **No ads, ever.** No promotional surface anywhere in the app.
- **No telemetry.** No analytics, no crash phone-home, no "anonymous usage
  statistics." No new outbound network calls without an explicit, off-by-
  default user toggle. Operationally: `QNetworkAccessManager` /
  `QHttpClient` / any other outbound-capable Qt class should not appear
  outside `src/ml/ModelDownloader.cpp` and the test code that exercises it.
  A PR that introduces one elsewhere is a stop-and-discuss change, not a
  drive-by review-and-merge. The build does not lint for this — code
  review is the entire enforcement mechanism. (Audit: `docs/audit-2026-05-19.md`
  §1 P-WC-1.)
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

## Hard gates — what "Done" requires

The constraints above say what Trailer *won't* do. These gates say what a
PR must *carry with it* before it can be marked Done. They are pass/fail:
each has a one-line rule, an objective test the reviewer (or you) can run,
and the evidence artifact the PR must contain. A PR missing the required
artifact is Not Done, regardless of how well the code works. These encode
decisions the maintainer has ratified; the reasoning lives in
[`PHILOSOPHY.md`](PHILOSOPHY.md) and [`DESIGN.md`](DESIGN.md), and
value/default changes are backed by records in
[`docs/decision-records/`](docs/decision-records/).

> These gates apply to the work in front of you — a PR that only touches
> internals and reshapes no user-visible surface trips none of the UX
> gates. Read each gate's test; if it doesn't apply, say so in the PR and
> move on. Don't manufacture stub UI or screenshots to satisfy a gate that
> isn't triggered.

### G1 — Threshold declared before work begins

- **Rule:** Every work item states a checkable pass/fail threshold before
  implementation starts. "Make scrolling smooth" is not a threshold;
  "scroll-step paints within the budget in `docs/performance-budgets.md`"
  is.
- **Test:** The PR description (or the linked TODO/issue) contains a
  measurable acceptance line written *before* the first code commit —
  a number, a concrete behaviour, or a named budget/spec row.
- **Evidence:** The threshold text, in the PR description, phrased so a
  reviewer can independently declare pass or fail. See PHILOSOPHY →
  *Every work item carries a checkable threshold*.

### G2 — UX-Done: screenshots of every affected state

- **Rule:** No PR that adds, removes, or reshapes a user-visible state
  merges without screenshots of each affected state, compared against the
  threshold declared under G1.
- **Test:** For each UI state the diff touches (empty state, document
  open, the feature in use, each dialog/confirmation, each reachable
  error state) there is a screenshot in the PR body, and a one-line
  verdict tying it to the declared threshold or the relevant persona
  lens.
- **Evidence:** The screenshots themselves, in the PR body (drag-and-drop
  upload or `gh pr edit --body-file`; do not commit them to the repo).
  This is the per-item roll-up of the milestone audit in DESIGN §2.5.3 —
  satisfying G2 per PR satisfies that audit; there is not a second
  screenshot regime.
- **Open question (owner sign-off):** whether an offscreen
  `widget->grab()` capture (see *Screenshots for UI-visible changes*
  below) counts as "the running app," or whether UX-Done requires a
  real event-loop/window render, is not yet ruled. Until the maintainer
  rules, offscreen grabs are accepted but the PR should say which method
  it used.

### G3 — No lying controls

- **Rule:** A control that is present in the surface but can't act right
  now is disabled with a `setToolTip(...)` that says why and where to go.
  Substituting a *different* behaviour and presenting it as the one the
  user asked for is forbidden — permanently.
- **Test:** No code path silently swaps a requested action for a
  nearest-equivalent. Every surfaced-but-inert control is
  `setEnabled(false)` + tooltip. (Scope: this applies to controls that
  *exist* in the surface. It is **not** a mandate to add disabled stubs
  for roadmap features that have no UI yet.)
- **Evidence:** For a PR that adds or gates such a control, a screenshot
  of the disabled state showing the tooltip (rolls into G2). The word
  "silently" is disambiguated in PHILOSOPHY → *No lying controls*:
  dropping a failed/low-quality result the user can retry, and the
  documented PDF round-trip subtype drop (DESIGN §6.3.1), are *drops*,
  not substitutions, and remain allowed.

### G4 — Platform-native shape, no feature dropped

- **Rule:** Adapt a feature's *shape* to each OS's native command surface;
  never drop a feature on one OS because that OS shapes it differently.
- **Test:** The feature is reachable on macOS, Windows, and Linux. If its
  surface differs per OS (global menu bar vs in-window menu bar vs header
  bar), that is expected and fine; a feature reachable on one OS and
  absent on another fails the gate. See DESIGN §2.1 goal 3 and
  PHILOSOPHY → *Platform-native per OS*.
- **Evidence:** The PR states, per platform, where the feature lives.
  Where only one host is available to the author, note which platforms
  were verified and which are asserted from the shared code path.

### G5 — Empty state is platform-correct

- **Rule:** First-run / no-document state follows the platform contract
  in DESIGN §2.4.2: macOS shows **no window** (dock icon + menu bar; open
  panel on activation); Windows/Linux show an empty window with
  Open/Recent and a centered drop-target.
- **Test:** Launch with no file argument on each platform; observe the
  state matches the §2.4.2 contract for that OS.
- **Evidence:** Screenshot (or, on macOS, a note that no window appears
  plus a menu-bar capture) of the empty state per available platform
  (rolls into G2).

### G6 — Behaviour/threshold changes carry a decision record

- **Rule:** A change to a user-visible default, or to a threshold that
  changes behaviour, references a record in `docs/decision-records/`.
  Internal tuning with no user-visible effect needs only the in-code
  rationale comment required by PHILOSOPHY → *Hand-tuned values stay
  hand-tuned*.
- **Test:** The magic constant still carries its in-code comment (what it
  represents, range tried, symptom to change); if the change is
  user-visible, an ADR exists and cites the constant's `file:line`
  rather than duplicating the comment.
- **Evidence:** Link to the ADR in the PR, and the updated in-code
  rationale in the diff.

### G7 — Preferences pane is a 1.0 gate

- **Rule:** A GUI Preferences/Settings pane (DESIGN §6.13) that a
  non-technical user can reach and operate is a hard requirement for 1.0.
- **Test:** Before 1.0 is declared, the settings window in DESIGN §6.13
  exists and is reachable from the standard platform location (⌘, on
  macOS; Edit/Tools → Preferences on Windows/Linux).
- **Evidence:** At the 1.0 milestone, a screenshot of the running
  Preferences window. Tracked as a release blocker, not a per-PR gate.

### G8 — Accessibility at the dogfood-default milestone

- **Rule:** The accessibility surface (DESIGN §6.12) is scheduled to be
  in place by the dogfood-default milestone — the point at which Trailer
  becomes the maintainer's own default app for these files.
- **Test:** At that milestone, keyboard-only operability of every command,
  screen-reader labels, configurable text size, high-contrast theme, and
  reduce-motion are all present and verified.
- **Evidence:** An accessibility checklist run against the running app,
  attached at the milestone. Not a per-PR gate before then, but no PR may
  *regress* an already-shipped accessibility affordance.

### G9 — Frugality budget (proposed)

- **Rule:** Binary size and resident memory stay inside the envelopes in
  [`docs/performance-budgets.md`](docs/performance-budgets.md); the ML
  runtime (ONNX + downloaded-on-first-use weights) is the one standing
  exception, because the weights are not bundled.
- **Test:** Measured binary size and RSS for the standard flows sit under
  the budgeted values once those values are ratified.
- **Evidence:** The measured numbers in the PR when a change plausibly
  moves them (a new dependency, a bundled asset). **This gate is
  PROPOSED** — the budget numbers await owner ratification; until then it
  is advisory, not blocking.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Requirements: CMake 3.24+, Qt 6.5+ with `Core Gui Widgets Test Pdf
PdfWidgets PrintSupport`, qpdf 11+, a C++20 compiler. `qtpdf` is a
separate module in many Qt distributions — install it explicitly if
`find_package(Qt6 COMPONENTS Pdf)` fails (see README).

CI builds with `-Werror` **off** by default — Qt / libstdc++ / qpdf
system-header noise (false-positive `-Wnull-dereference`,
`POINTERHOLDER_TRANSITION`, etc.) makes strict `-Werror` unworkable
in CI. Trailer's own source is kept clean by review; the
`claude/ci-harden-dev-phase` branch is converting the strict
`-Werror` job into an advisory non-blocking comment. To opt into the
strict build locally:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTRAILER_WERROR=ON
```

Run this when chasing a regression or before landing a refactor that
touches a lot of templated code.

**Windows native.** `scripts/install-windows-deps.ps1` then
`scripts/build-windows-native.ps1` — full path documented in
README's "Windows" section. Two Windows-specific gotchas to know
when touching PDF I/O or UAT code:
- `QTemporaryFile` keeps the OS handle alive past `close()` on
  Windows; qpdf's subsequent `fopen("wb")` then fails with
  `Permission denied`. Use `trailer::ScopedTempFile` /
  `trailer::makeUniqueTempPath` from `src/util/TempPath.h` for any
  temp file handed off to a non-Qt writer.
- Under `QT_QPA_PLATFORM=offscreen`, Qt 6 on Windows has no font
  directory unless `QT_QPA_FONTDIR=%SystemRoot%\Fonts` is set, so
  `QPdfWriter` produces unsearchable PDFs (text rendered as filled
  paths). `tests/CMakeLists.txt` and `tests/uat/CMakeLists.txt` set
  the env var per-test on Windows; do the same for any new fixture
  helper that runs outside ctest.

**Windows agent gotchas (git / gh / PowerShell).** Operating from an
agent on a Windows host, two things bite when pushing branches and
talking to GitHub:
- **SSH to GitHub can hang non-interactively.** A first-connection
  host-key prompt (or a key-passphrase prompt) has no TTY to answer, so
  `git fetch` / `git push` over a `git@github.com:` remote hangs
  silently with no output. Route network git through the `gh` HTTPS
  credential helper instead:
  ```powershell
  $h = 'credential.helper=!gh auth git-credential'
  git -c credential.helper= -c $h push https://github.com/<owner>/trailer.git <branch>:refs/heads/<branch>
  ```
  `gh` itself (`gh pr create`, `gh api`) talks to the API over HTTPS with
  the stored token and is unaffected. One-time SSH fix: seed the host key
  (`ssh-keyscan github.com >> ~/.ssh/known_hosts`) and load your key into
  an agent.
- **PowerShell 5.1 `Set-Content -Encoding utf8` writes a BOM**, which
  makes `gh api --input body.json` fail with `Problems parsing JSON
  (HTTP 400)`. Write JSON request bodies as plain UTF-8 *without* a BOM —
  e.g. with a tool/editor that emits no BOM, or
  `[IO.File]::WriteAllText($p, $s, [Text.UTF8Encoding]::new($false))`.

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

### CI cadence — what runs where, and what may block

Tests are tiered by cost and by how trustworthy their oracle is. The
governing rule: **only deterministic checks with a hard oracle may block
a merge or a release; speculative / advisory checks never gate.**

| Tier | Trigger | Runs | Selector | May block? |
|---|---|---|---|---|
| **PR / push** | every PR (`ci.yml`, Linux + Windows) | unit tests (+ any non-`uat` deterministic checks) | `ctest --label-exclude uat` | **blocks merge** |
| **Release-candidate** | `release-candidate` label (`release.yml`) | full UAT — regression guards **and** the Layer-1 layout sweep | `ctest -L uat` | **blocks the release** |
| **Nightly / advisory** *(planned, not yet wired)* | scheduled workflow | persona Monte-Carlo + vision (Set-of-Mark) judge + HITL-recall backtest | a label with **no** `uat` in it | **never blocks** — emits a digest artifact |

- The Layer-1 robustness sweep (`tests/uat/test_uat_sweep.cpp`) is
  deterministic (no clipped/collapsed controls), so it carries the plain
  `uat` label and gates at release like the rest of UAT — it is *not*
  advisory.
- **Label gotcha:** `ctest -L` / `-LE` match labels by regex, so a label
  like `uat-sweep` is still matched by `-L uat` and would silently join
  the release gate. Give advisory tests a label with no `uat` substring
  (e.g. `advisory`) and exclude it from PR CI explicitly.
- Every confirmed defect (HITL finding, bug report, or sweep result)
  becomes a regression guard in the `uat` suite so it can't silently
  return — the ratchet only tightens.

## Layout

```
src/
  annotation/      # Annotation data classes + AnnotationStore snapshot undo
  app/             # Application lifecycle, QApplication subclass, file-open routing
  cards/           # Trim My Card and related card-detection helpers
  document/        # IDocument, adapters (PdfAdapter, ImageAdapter, StubAdapter),
                   # PdfEditor + PdfCommand pattern, qpdf wrapper
  filters/         # ImageFilter (sepia / B&W / brightness etc.)
  ml/              # OnnxSession, ModelRegistry, ModelDownloader,
                   # BackgroundRemover, SamSession, OcrEngine
  platform/        # OS-specific bits (screenshot pickers, power source, etc.)
  recent/          # RecentFiles model
  settings/        # Persistence (QSettings + a toml++ store for richer state)
  signatures/      # SignatureStore + signature-capture pipeline
  ui/              # MainWindow, DocumentView, Sidebar, AnnotationOverlay,
                   # Inspector, MarkupToolbar, SignatureCaptureDialog, etc.
  util/            # Small cross-cutting helpers
tests/             # Unit tests, one file per src/ class roughly
tests/uat/         # UAT harness (offscreen, label=uat)
docs/uat/          # UAT specs — source of truth for end-user behaviour
icon/              # App icon pipeline (Python; see icon/README.md)
docker/            # Cross-compile + UAT runner images
packaging/         # Linux DEB scaffolding
scripts/           # build-linux-deb.sh, build-windows.sh, run-uat.sh
```

## Conventions

> **Pattern catalogue.** This section covers process/workflow
> conventions (branches, commits, undo stacks, networking). For the
> code-level patterns the repo expects new contributions to follow —
> document adapters, `PdfCommand` shape, `AnnotationStore` snapshot
> undo, coordinate-callback overlay, `QPointer` use, UAT slot naming,
> magic constants — see [`docs/CONVENTIONS.md`](docs/CONVENTIONS.md).

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
| Match an existing code pattern | `docs/CONVENTIONS.md` |
| Pick up open work | `TODO.md` (HITL section is the live sprint) |
| Write a UAT case | `docs/uat/README.md` + a sibling spec file |
| Write a UAT harness slot | `tests/uat/test_uat_foundations.cpp` is the template |
| Add a PDF command | `src/document/PdfCommands.h` + `RotatePageCommand` impl |
| Add an annotation type | `src/ui/AnnotationOverlay.cpp`, `Annotation.h`, `AnnotationStore` |
| Add a Qt-PDF rendering path | `src/document/PdfAdapter.cpp`, the QPdfView wiring |
| Touch ML | `src/ml/` — keep all inference local, no remote calls |
| Run a reference-user smoke session | `docs/smoke-session.md` |
| Land cross-platform packaging fixes | `docs/cross-platform-sprint.md` |
| Read the PR #24 (HITL waves 1-4) merge retrospective | `docs/in-flight-merge-plan.md` |
| See what the eleven reviewer lenses turned up | `docs/audit-2026-05-19.md` |

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
