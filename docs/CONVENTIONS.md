# Trailer Conventions

Patterns the code already follows that aren't enforced by the
compiler. New contributions are expected to match these; existing
code that violates them is on the maintenance list, not the
"intentional precedent" list.

This doc covers what is stable on `main` today. Patterns 11–13 came
in with the PR #24 merge (`4dba247 HITL waves 1-4`) on 2026-05-20 —
they cover three-tier view-state persistence, the `MlScheduler`
contract, and the raw-`IDocument*` cache flush via
`documentAboutToBeRemoved`.

For each pattern: what it looks like, where to find the canonical
example, the recipe for adding code that fits it, and the symptom
that means someone violated it.

---

## 1. Document types are adapters around `IDocument`

Trailer is a viewer for *some* file types and a noop for others.
The set of supported types is data, registered at startup.

**Anchor files:** `src/document/IDocument.h`,
`src/document/IFormatAdapter.h`, `src/document/DocumentRegistry.h`,
`src/document/PdfAdapter.{h,cpp}`, `src/document/ImageAdapter.{h,cpp}`,
`src/document/StubAdapter.{h,cpp}`.

**Pattern.** `IDocument` is the contract every opened file is a
subclass of (`PdfDocument`, `ImageDocument`, `StubDocument`).
`IFormatAdapter` is the per-format factory that says "I handle these
extensions, here's how to open one." Adapters are added to
`DocumentRegistry` once, during `Application` startup; from then on,
file opens dispatch by extension. Every `IDocument` method that a
specific format doesn't support has a no-op default in the base
(`supportsZoom`, `supportsEditing`, etc.), so a format only
implements what it actually supports.

**Recipe.** Adding a new document type means:

1. Write `FooAdapter : IFormatAdapter` declaring extensions and a
   factory.
2. Write `FooDocument : IDocument` overriding only the capability
   methods that are true for this format.
3. Register the adapter in `DocumentRegistry` in one place. Nothing
   else changes.

**Broken if.** A feature you'd think every document should expose
turns out to require `dynamic_cast<PdfDocument*>` to reach. The base
should have grown a `supportsX()` capability bit + a virtual hook.

---

## 2. qpdf mutations are `PdfCommand` subclasses

Anything that mutates the on-disk PDF — page rotate, delete, move,
insert, crop — is a `PdfCommand` subclass with symmetric `apply` /
`revert` methods.

**Anchor files:** `src/document/PdfCommands.{h,cpp}` (which now
defines `RotatePageCommand`, `DeletePagesCommand`, `MovePageCommand`,
`InsertPagesCommand`, `CropPageCommand`),
`src/document/PdfEditor.{h,cpp}`. `RotatePageCommand` is the
template the others followed.

**Pattern.** Each command captures enough state at construction to
make both directions invertible. The document owns two stacks of
`std::unique_ptr<PdfCommand>`; commands move between them on
undo/redo. The hard contract is **symmetry**: `apply → revert → apply`
must equal a single `apply`. Asymmetric commands break navigation in
ways that aren't caught at compile time.

**Recipe.** Add `FooPageCommand : PdfCommand`, override:

- ctor: capture the inputs you need to reverse later (often: a copy
  of the old state, the parameters of the change).
- `apply(editor)`: perform the mutation.
- `revert(editor)`: restore the old state.
- `description()`: one short imperative phrase shown in the undo
  menu.

Wiring to the undo stacks is automatic.

**Broken if.** `apply` and `revert` aren't a clean pair. The
qpdf-binding-author agent description has more on the template; if
you're adding a new lossless page op, it owns that surface.

**Known follow-up.** PdfCommand and AnnotationStore (next section)
maintain separate undo stacks. Unified chronological ordering is a
follow-up tracked in `TODO.md`; don't add a third stack — extend one
of the existing two.

---

## 3. Annotations: snapshot undo + `AnnotationStore::changed()`

Annotation undo is whole-store snapshots, not per-mutation commands.

**Anchor files:** `src/annotation/AnnotationStore.{h,cpp}`.

**Pattern.** Every mutation (add / remove / update) takes a full
snapshot of `m_annotations` before applying the change and pushes it
to `m_undoStack`. Redo maintains a parallel stack. Both stacks are
capped at `kMaxUndo = 64`; the oldest snapshot is discarded on
overflow. Mutations emit `changed()`; the UI re-renders.

This is simpler than command-pattern undo but less granular — there
is no concept of "the difference between snapshot N and N+1." That's
fine because annotations are small and the user thinks of them as
discrete objects, not as deltas.

**Recipe.** New annotation mutation = one method on `AnnotationStore`
that calls `pushHistory()` first, mutates `m_annotations`, then emits
`changed()`. Don't expose a way to mutate without `pushHistory`.

**Compound gestures.** A drag-resize or drag-move emits dozens of
intermediate `update()` calls; each would push its own snapshot
without coordination, so a single Ctrl-Z would undo only the last
frame. To collapse a gesture into one undo step, wrap it in
`beginCompound()` / `endCompound()` (PR #24, `AnnotationStore.h`):
`beginCompound()` increments `m_compoundDepth`; subsequent
mutations skip `pushHistory()` so only the first call in the
compound captures the pre-gesture state (lazily, via the
`m_compoundSnapshotPushed` flag); `endCompound()` returns the
depth to 0 and — if any mutation occurred inside — finalises the
single history frame. Mismatched depths are diagnosable via the
unit tests in `tests/test_annotation_store.cpp`. The
`AnnotationOverlay` drag handlers are the canonical caller.

**Broken if.** Memory grows unboundedly during a session (someone
mutated without `pushHistory` — the cap doesn't help if it's never
called) or undo "skips" mutations (someone batched several mutations
under one `pushHistory` without using `beginCompound` / `endCompound`).

**Tuning.** `kMaxUndo` is a hand-tuned magic number. Per
[PHILOSOPHY.md](../PHILOSOPHY.md) it stays hand-tuned; if you change
it, update the comment next to the constant with the reason and the
range you considered.

---

## 4. `AnnotationOverlay` is coordinate-callback-driven

The overlay knows nothing about which viewer it sits on top of. The
viewer supplies coordinate-mapping callbacks at construction; the
overlay calls them.

**Anchor files:** `src/ui/AnnotationOverlay.{h,cpp}`,
`src/document/PdfAdapter.cpp`, `src/document/ImageAdapter.cpp` (the
two current callsites).

**Pattern.** The overlay accepts three callbacks: `DocToView`,
`ViewToDoc`, `PageAtViewPoint`. Single-page viewers supply trivial
ones; continuous-mode viewers supply page-aware ones. Text selection
for Highlight/Underline annotations comes from a fourth callback,
`TextSelectionProvider`, returning rects for runs on a page; absent
the callback, markup falls back to the drag bounding box. Hit-testing
runs in document space, not view pixels.

**Recipe.** A new viewer wanting annotation support implements the
four callbacks. Rendering, event handling, and hit-testing in the
overlay stay unchanged.

**Broken if.** Annotations land on the wrong page in continuous
mode, drift on zoom, or feel laggy under drag. The overlay should
never know about zoom factor, scroll offset, or page geometry directly
— those flow through the callbacks. The annotation-overlay-fixer
agent owns this surface; bugs here go through it.

---

## 5. Weak references are `QPointer<T>`, never raw pointers

Anywhere a tracked object's lifetime can end before the holder's,
use `QPointer<T>`.

**Anchor files:** `src/app/Application.h` (`m_windows`),
`src/ui/AnnotationOverlay.h` (`m_inlineEditor`),
`src/document/PdfAdapter.h`, `src/document/ImageAdapter.h`.

**Pattern.** `QPointer<T>` auto-nulls when the pointee is destroyed
by Qt's event loop. Bare null checks before dereference are standard.
`Application::onWindowDestroyed()` explicitly removes stale entries
from `m_windows` rather than waiting for QPointer to silently null
them mid-iteration.

**Recipe.** If a pointer crosses widget-destruction boundaries (a
window list, an inline editor, a worker writing back to a doc that
could close), wrap in `QPointer`. Check for null on every use. Don't
keep the bare `T*` for "performance."

**Broken if.** A crash reproduces by closing a tab/window while a
background operation is in flight. Almost always: a raw pointer
captured into a lambda / queued connection / worker that outlived
its target.

---

## 6. Event filters are how widgets observe their children

Where one widget needs to react to events that another widget would
normally consume, install an event filter.

**Anchor files:** `src/ui/AnnotationOverlay.cpp` (filters the inline
text editor for focus-out), `src/ui/Sidebar.cpp` (filters the
thumbnail list for drag-reorder).

**Pattern.** Override `eventFilter(QObject*, QEvent*)`, call
`installEventFilter(this)` on the child during construction or
wiring. Dispatch on event type; chain to the base implementation
when not handling. Don't subclass the child just to observe — the
filter pattern keeps the child reusable.

**Recipe.** Same shape every time: override + install + dispatch +
chain.

**Broken if.** A widget hierarchy ends up with three layers of
subclasses just to capture a click. That's a filter that should
have been written instead.

---

## 7. UAT cases and test slot names — paired where possible, topical where not

The UAT spec is canonical for case definitions; the test harness
mirrors them as `QTest` slots.

**Anchor files:** `docs/uat/README.md`, the seven
`docs/uat/0?-*.md` spec files, the matching
`tests/uat/test_uat_*.cpp` files.

**Pattern (the easy half).** Every case in the spec has an ID of
the form `UAT-FND-001`, `UAT-VWR-060`, etc. For the four area
codes whose tests are grouped by spec area — `FND`, `VWR`, `ANN`,
`SEC` — the test slot is named `uat_fnd_001_shortTitle()`: same
ID, lowercased, underscored, plus a snake_case summary. A failing
slot points straight at the spec case.

**Pattern (the other half).** Three spec areas (`PDF`, `IMG`,
`XCT`) are grouped topically in the test harness instead. A spec
case like `UAT-PDF-050` (a form-fill workflow) lives in the spec
under PDF but is implemented as `uat_frm_010_*` in
`test_uat_forms.cpp`. The topical prefixes are: `uat_af_*`
(autofill), `uat_bgr_*` (background removal), `uat_frm_*` (forms),
`uat_hn_*` (highlights & notes), `uat_ocr_*` (recognize text),
`uat_red_*` (redaction), `uat_sam_*` (Smart Lasso + Instant
Alpha), `uat_sig_*` (signatures), `uat_toc_*` (table of contents).
This split is documented (not enforced) and tracked as a backfill
item in `TODO.md ## 2026-05-19 HITL pass` (audit ref
DOC-FOLLOWUP-1) — either renaming slots to spec IDs or extending
the spec to cover the topical codes is a future call.

Fixtures are generated inline (`QPdfWriter`, `QImage`) rather than
checked-in files. All tests run under
`QT_QPA_PLATFORM=offscreen` — no display server required.

**Recipe.** A new behaviour gets:

1. A new case in the right spec file with the next free ID.
2. A new slot in either (a) the matching `test_uat_<area>.cpp` if
   the spec area is `FND` / `VWR` / `ANN` / `SEC` and the slot
   name mirrors the spec ID, or (b) the topical
   `test_uat_<topic>.cpp` (forms, signatures, OCR, etc.) for spec
   cases in `PDF` / `IMG` / `XCT`, with the slot named after the
   topical prefix.
3. Steps + assertions in the slot.

Don't invent a third naming axis (a slot prefix that isn't a spec
area code *and* isn't already a topical one).

The uat-author agent owns this translation; if you're adding a UAT
case end-to-end, route through it.

**Broken if.** A slot name doesn't trace back to either a spec ID
(area-matched case) or a documented topical prefix; fixtures
appear as checked-in files; or a test starts requiring a real
display server.

---

## 8. Settings are flat keys plus a few topical tables

`settings.toml` is meant to be hand-editable. The schema reflects
that.

**Anchor files:** `src/settings/Settings.{h,cpp}`,
`src/settings/AppPaths.{h,cpp}`.

**Pattern.** Top-level keys for real settings, with enum values
where appropriate (`theme`, `open_files_in`). A small set of
topical tables collects related state under a shared prefix:

- `[first_use]` — boolean acknowledgements for one-time prompts
  (redaction warning, model-download dialogs).
- `[session]` — window-list restore state captured at
  `aboutToQuit` (`open_files`, `restore_previous_windows`).
- `[ml.scheduler]` — ML governance knobs the scheduler reads
  (`recognize_text_in_background`,
  `preload_segmentation_on_tool_activation`, `run_on_battery`).

Settings load on `Application` startup. `Settings` is a plain value
type: setters mutate cached `m_` members and do **not** auto-`save()`.
Persistence is the caller's job — `PreferencesDialog::accept()` batches
every changed control and issues a single `save()`, and one-off writers
(`aboutToQuit` session capture, last-save-dir) call `save()` explicitly.
Don't introduce a nested table just to group an unrelated pair of keys —
the tables above each have a clear topical scope and a single subsystem
that owns them.

**Recipe.** New setting = new top-level key (or new entry under
the topical table that owns its concern, if one exists), backing
field on `Settings`, getter + setter (the setter does not `save()`;
a caller batches the write). Register the key's volatility (see §15).
Grep the header to discover all keys; the volatility registry in
`Settings::volatilityOf` is the one place that must list every key.

**Broken if.** A future setting feels like it needs a *fourth*
nested table that doesn't fit any of the three existing ones. That's
the moment to ask whether the concern is well-named, not to add
another `[section.something]` to learn.

---

## 9. Magic constants live next to a reason

The companion to PHILOSOPHY's *Hand-tuned values stay hand-tuned*
rule, applied in code.

**Anchor files:** `src/ui/Magnifier.cpp` (`kSize`, `kTickMs`),
`src/filters/ImageFilter.cpp` (`kBoost`, the mid-grey threshold).

**Pattern.** A hand-tuned value lives in an anonymous namespace or
as a `constexpr` near where it's used, with a brief comment
explaining what it represents and what tradeoff the chosen value
encodes. Examples on this branch are sparse but consistent (the
codebase prefers methods over constants in most places).

**Recipe.** When introducing a constant whose value was chosen by
hand:

1. Give it a `constexpr` name. Don't inline a literal.
2. Add a comment with: what it controls, what range was tried, what
   symptom would justify changing it.
3. If the value is exposed to users via a setting, add a default in
   `Settings` and reference the constant.

**Broken if.** A future contributor finds a `0.5`, `220`, or `64`
in code with no nearby comment, doesn't know whether it's load-
bearing, and changes it on intuition. The whole point of writing
the reason is to make the next negotiation explicit.

---

## 10. Vendored CMake deps are `IMPORTED GLOBAL` targets

Trailer ships qpdf, libjpeg, ONNX Runtime, and a few smaller libs.
The discovery pattern is consistent.

**Anchor files:** `cmake/OnnxRuntime.cmake` (canonical),
`cmake/CompilerWarnings.cmake`, `cmake/toolchain-mingw-w64.cmake`.

**Pattern.** A dep's cmake file:

1. Prefers `find_package()` against a system-installed copy.
2. Falls back to downloading a pre-built tarball with SHA256
   verification.
3. Includes per-platform asset logic (Darwin arm64, Linux x86_64,
   Windows MSVC).
4. Declares an `IMPORTED GLOBAL` target (e.g.
   `onnxruntime::onnxruntime`) that downstream `CMakeLists.txt`
   files link against.
5. Centralises the version at the top of the file as one constant
   for point-updates.
6. Provides a `trailer_deploy_<name>(target)` helper for Windows
   post-build DLL copying where relevant.

**Recipe.** A new vendored dep follows the `OnnxRuntime.cmake`
template top-to-bottom. Don't reach into the dep's CMake files
directly; consumers link against the `IMPORTED` target.

**Broken if.** Two `CMakeLists.txt` files reference different
versions or paths for the same dep. Both should resolve through
the same `IMPORTED` target.

---

## 11. Three-tier view-state persistence

When the user reopens a document, view state is restored in priority
order: **per-file → per-type → hardcoded defaults**.

**Anchor files:** `src/recent/RecentFiles.{h,cpp}` (per-file +
`RecentEntry::hasViewState`), `src/settings/DocumentTypeDefaults.{h,cpp}`
(per-type, persisted under `QSettings`), `src/settings/Settings.h`
(`[session]` block for window-list restore),
`src/ui/MainWindow.cpp` (`onCurrentDocumentChanged` restore path,
`closeEvent` capture path).

**Pattern.** Each viewer-relevant field (zoom mode + factor, scroll
Y, current page, sidebar mode, markup-toolbar visibility, window
`saveGeometry` + `saveState` blobs) is captured on `closeEvent` to
the user's `RecentEntry` keyed by canonical file path, and *also* to
the `DocumentTypeDefault` slot for that file's `DocumentType`
(last-closed-of-type wins). On reopen, `onCurrentDocumentChanged`
runs once per document pointer (gated by `m_restoredViewStateDocs`):
if the file's `RecentEntry::hasViewState()` is true, apply per-file;
else if the doc's `DocumentType` is recognised and that type's
default has state, apply per-type; else leave the constructor
defaults alone. Window-list restore on launch reads
`Settings::sessionOpenFiles()` (captured at `aboutToQuit`); explicit
CLI args override the session list.

**Recipe.** New view-state field gets:

1. A field on `RecentEntry` with a sentinel default (`-1`, `0.0`,
   empty `QByteArray`) and an entry in `hasViewState()`.
2. A mirror field on `DocumentTypeDefault` with the same sentinel.
3. Capture in `MainWindow::closeEvent` (write to both `RecentEntry`
   and the `typeSnapshot`).
4. Apply in the per-file *and* per-type restore branches of
   `onCurrentDocumentChanged`.
5. Round-trip test in `tests/test_recent.cpp` covering both legacy
   load (sentinel default, no apply) and full-state load.

**Broken if.** A user reports that closing a file and reopening it
lost the page / zoom / sidebar mode — the close-time capture missed
that field, or the restore branch did not apply it. Or: tab-
switching between two docs in the same window bounces one of them
back to a saved page (the `m_restoredViewStateDocs` one-shot guard
got bypassed).

---

## 12. `MlScheduler` is the canonical ML runner

Every ML invocation in the app goes through
`Application::mlScheduler().submit(...)`. No worker is spawned
directly; no modal `QProgressDialog` gates an ML call.

**Anchor files:** `src/ml/MlScheduler.{h,cpp}`,
`src/ml/CancellationToken.h`, `src/platform/PowerSource.{h,cpp}`.
Canonical use-cases: `src/ui/OcrController.{h,cpp}`,
`src/ui/SamController.{h,cpp}`, the background-removal path in
`src/ui/MainWindow.cpp` (`onRemoveBackground`), and the scorer
submit in `MainWindow::scheduleBackgroundCandidateScore`.

**Pattern.** `submit(priority, label, work)` takes ownership of a
`std::function<void(CancellationToken &)>` and returns a `Handle`
(task id + shared cancellation token). Priority is one of
`UserAction > VisiblePage > Prefetch > Idle`; higher priorities
preempt lower-priority *queued* work (the running task drains to
its next checkpoint — no thread-killing). On battery with
`Settings::mlRunOnBattery() == false`, Prefetch and Idle
submissions return a pre-cancelled token from `submit()` itself.
Workers poll `CancellationToken::isCancelled()` between major
stages (e.g. between OCR detect and per-box recognise) and bail
early. The single worker thread serialises all ML work — by
design, until a future change adds concurrency caps.

A status-bar `QLabel` (`MainWindow::m_mlIndicator`) shows whenever
the scheduler is non-idle; tooltip is the running task's label.
This is the **only** affordance the user sees for background ML —
modals are off the table per PHILOSOPHY.

**Recipe.** A new ML feature looks like:

1. Pick the priority: `UserAction` for an explicit click,
   `VisiblePage` for "current page is the user's focus", `Prefetch`
   for "likely useful soon", `Idle` for true best-effort.
2. Translate the `label` (shown in the tooltip) at the call site.
3. Capture by value into the lambda anything the worker needs;
   never dereference an `IDocument*` from inside the lambda body
   without a re-check against the active doc on the GUI thread
   (`QMetaObject::invokeMethod` back to the UI for the apply step).
4. Poll the `CancellationToken &` between heavy steps; return
   early on `isCancelled()`.
5. Store the `Handle::id` on whatever cache keys by `IDocument*`
   so `documentAboutToBeRemoved` can `cancel(id)` cleanly.

**Broken if.** A modal `QProgressDialog` appears for an ML
operation, or closing a tab mid-OCR / mid-SAM / mid-removal causes
a crash or stale-pointer dereference. Or: a Prefetch task survives
unplugging the AC adapter and the laptop overheats — the
`PowerSource` reactor didn't run, or `cancelMatching` missed the
speculative tag.

---

## 13. Raw `IDocument*` caches flush via `documentAboutToBeRemoved`

`IDocument` is not a `QObject`, so `QPointer<IDocument>` is not
available. The pattern: hold raw pointers as cache keys *only* if
you also subscribe to `DocumentView::documentAboutToBeRemoved` and
flush on it.

**Anchor files:** `src/ui/DocumentView.{h,cpp}` (the signal +
`onTabCloseRequested` emission), `src/ui/MainWindow.cpp` (the
ctor-time subscription that calls into every cache it owns +
`SamController::purgeDocument` for the encoder LRU + the
`OcrController::setDocument(nullptr)` path before destruction).

**Pattern.** `DocumentView` emits `documentAboutToBeRemoved(doc)`
*after* the tab is removed and *before* the `unique_ptr<IDocument>`
is erased — so the pointer is still valid for cache-lookup, but no
new work can be enqueued against it. Every cache that uses the raw
pointer as a key (`MainWindow::m_backgroundCandidateDocs`,
`m_pendingCandidateJobs`, `m_autoEnabledFormDocs`,
`m_restoredViewStateDocs`, the SamController's encoder LRU, the
OcrController's pending-keys map) subscribes to that signal and
removes the entry / cancels the `MlTaskId` it holds. A recycled
allocator address can therefore never inherit a stale cached
verdict from a previous document at the same heap location.

**Recipe.** A new cache or worker that keys by `IDocument*`:

1. Hold the pointer as `IDocument *` (not `QPointer`, which would
   not compile — `IDocument` is non-`QObject`).
2. Subscribe to `DocumentView::documentAboutToBeRemoved` during
   MainWindow construction (or, for controllers, accept a
   `setDocument(nullptr)` call before destruction).
3. On the signal: remove the entry from your map; if it carries
   an `MlTaskId`, call `Application::mlScheduler().cancel(id)`.
4. For lambdas posted to the GUI thread from a worker: re-check
   `dvPtr->currentDocument() == expectedDoc` before touching
   member state. The pointer is *not* QPointer-safe across the
   `Qt::QueuedConnection` round-trip.

**Broken if.** Closing a tab mid-ML-task leaves a stale entry in a
cache that fires later against a recycled address, or against
`nullptr`, and crashes; or: a verdict / badge / handler from a
previous document at the same heap address fires against a fresh
one (manifests as "wrong sparkle on Remove Background after a
fast close-and-reopen"). A `DocumentLifecycle` service that
generalises this is roadmap-tracked; until it lands, every
new raw-pointer-keyed cache must hand-subscribe.

---

## 14. Enum switches: no catch-all `default` for domain/mode enums

A `switch` over one of our domain enums (`ViewMode`, `ZoomMode`,
`DocumentType`, and any future mode/state enum defined in our code)
MUST enumerate every enumerator explicitly and MUST NOT carry a
`default:` catch-all. Adding a new enumerator is then a compile
error at every switch that forgot to handle it — the compiler, not
code review, keeps the switches exhaustive.

**Anchor files:** `cmake/CompilerWarnings.cmake` (the
`trailer_set_warnings` function that promotes `-Wswitch` to an
error), `src/document/IDocument.h` (the `ViewMode` / `ZoomMode` /
`DocumentType` definitions), `src/document/PdfAdapter.cpp`
(`applyViewMode`, `applyZoomState`), `src/ui/MainWindow.cpp`
(`syncViewModeActions`), `src/settings/DocumentTypeDefaults.cpp` and
`src/recent/RecentFiles.cpp` (`zoomModeKey`),
`src/document/ImageAdapter.cpp` (`applyZoomState`).

**Pattern.** `trailer_set_warnings` appends `-Werror=switch`
unconditionally (GCC/Clang; `/we4062` for MSVC) — *not* gated behind
`TRAILER_WERROR`, so it binds even in the default/CI build where full
`-Werror` is off. `-Wswitch` (part of `-Wall`) fires only when a
switch over an enum both omits an enumerator AND has no `default:`.
Promoting just this one warning to an error makes exhaustive,
default-less mode switches compiler-enforced without turning on full
`-Werror`, which stays off because Qt and other third-party headers
are warning-noisy. A `default:` (or `default: break;`) silences
`-Wswitch` entirely — which is exactly why it is forbidden for our
domain enums: it re-opens the silent-swallow hole. Reserve `default:`
for switches over a genuinely open or non-enum value (a raw `int`, a
third-party enum you don't own, a bitmask), where a fallback branch
is legitimate.

**Recipe.** A new switch over one of our enums:

1. Write one `case` per enumerator; no `default:`.
2. If some cases share behavior, group them
   (`case A: case B: ...`) rather than reaching for `default:`.
3. If you need a post-switch fallback (e.g. an unreachable
   sentinel `return`), put it *after* the closing brace of the
   switch, not in a `default:` label — the trailing statement does
   not suppress `-Wswitch`.
4. Adding an enumerator to the enum: rebuild; the compiler lists
   every switch that now needs a case.

**Broken if.** A `default:` on a mode/domain-enum switch lets a
newly added enumerator fall through silently — this is exactly the
Two-Pages regression that motivated the rule: `applyViewMode` once
had a catch-all that aliased `ViewMode::TwoPages` onto
`Continuous`, so the view silently showed a different layout than
the label promised. With `-Werror=switch` and no `default:`, that
class of bug is unrepresentable — the build fails until every
enumerator is handled on purpose.

---

## 15. Every persisted setting is registered live-vs-restart

`Settings` is a plain value type with no signals (§8). A setting
applies live only because its consumers re-read the getter at use
time (`Application::openFiles` re-reads `openFilesIn`; auto-save
re-reads `autoSave`) or because `PreferencesDialog::settingsApplied`
re-applies it on OK (`recent_max`). A *future* key whose consumer
reads it once at startup would silently become restart-only, with
nothing telling the user a restart is needed. This is the
restart-surprise trap the registry closes.

**Anchor files:** `src/settings/Settings.{h,cpp}`
(`SettingsKeys`, `Settings::Volatility`, `Settings::volatilityOf`),
`src/ui/PreferencesDialog.{h,cpp}` (`makeRestartHint`),
`tests/test_settings_volatility.cpp`.

**Pattern.** Every persisted key has a canonical dotted-path constant
in the `SettingsKeys` namespace (e.g. `files.recent_max`) and a
`Volatility` classification (`Live` or `RestartRequired`) in the
registry backing `Settings::volatilityOf`. Dynamic key groups
(`first_use.*`) are classified by prefix. `volatilityOf` returns
`std::nullopt` — and logs — for a key nobody registered. Preferences
builds every editable row through `PreferencesDialog::makeRestartHint`,
which appends a muted "Requires restart to take effect" label iff the
key is `RestartRequired`. Today **every** key is `Live`, so the hint
never renders and behaviour is unchanged; the machinery is dormant
until the first `RestartRequired` key.

**Recipe.** Adding a persisted key:

1. Add a `SettingsKeys` constant for its dotted path. Add the section
   and leaf under its owning TOML table in `load()`/`save()` (these
   build nested tables from raw literals section-by-section, not from a
   flat dotted constant), and keep the `SettingsKeys` dotted-path
   constant byte-identical to that persisted path — `registryCoversEveryPersistedKey`
   in `tests/test_settings_volatility.cpp` fails if they diverge.
2. Add a registry entry classifying it `Live` (the default — the
   consumer re-reads the getter, or OK re-applies it) or
   `RestartRequired` (the consumer caches it at startup and cannot be
   re-applied cheaply).
3. If `RestartRequired`, wire the control's row through
   `makeRestartHint` (all rows already are) so the hint appears.

**Broken if.** A persisted key is not in the registry.
`tests/test_settings_volatility.cpp` saves a fully-populated
`settings.toml`, walks every leaf key it contains, and asserts each
resolves in `volatilityOf` — so an unregistered key fails the build's
test stage loudly rather than shipping a silently restart-only
setting.

One intentional exception: `Settings::load()` still reads the legacy
`redaction.warning_acknowledged` key, which is deliberately *not* in the
registry — it is a read-only migration key rewritten under `first_use.`
on the next `save()` and never queried via `volatilityOf` in production.
