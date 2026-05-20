# Trailer Conventions

Patterns the code already follows that aren't enforced by the
compiler. New contributions are expected to match these; existing
code that violates them is on the maintenance list, not the
"intentional precedent" list.

This doc covers what is stable on `main` today. Three larger
patterns (three-tier persistence, the MlScheduler contract, the
raw-`IDocument*` cache) come in with the
`claude/mystifying-proskuriakova-e07cb6` branch; they get their own
sections once that branch lands.

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
crop — is a `PdfCommand` subclass with symmetric `apply` / `revert`
methods.

**Anchor files:** `src/document/PdfCommands.{h,cpp}`,
`src/document/PdfEditor.{h,cpp}`. `RotatePageCommand` is the
canonical example.

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

**Broken if.** Memory grows unboundedly during a session (someone
mutated without `pushHistory` — the cap doesn't help if it's never
called) or undo "skips" mutations (someone batched several mutations
under one `pushHistory` without explicit compound semantics).

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

## 7. UAT cases are paired 1:1 with test slot names

The UAT spec is canonical; tests are generated from it.

**Anchor files:** `docs/uat/README.md`, the seven
`docs/uat/0?-*.md` spec files, the matching
`tests/uat/test_uat_*.cpp` files.

**Pattern.** Every case in the spec has an ID of the form
`UAT-FND-001`, `UAT-VWR-060`, etc. The matching test slot is named
`uat_fnd_001_shortTitle()` — same ID, lowercase, underscored,
followed by a snake_case summary. Failing tests therefore point
straight back at the spec.

Fixtures are generated inline (`QPdfWriter`, `QImage`) rather than
checked-in files. All tests run under
`QT_QPA_PLATFORM=offscreen` — no display server required.

**Recipe.** A new behaviour gets:

1. A new case in the right spec file with the next free ID.
2. A new slot in the matching test file with the matching name.
3. Steps + assertions in the slot.

The uat-author agent owns this translation; if you're adding a UAT
case end-to-end, route through it.

**Broken if.** Test names drift from case IDs, fixtures appear as
checked-in files, or a test starts requiring a real display server.

---

## 8. Settings are flat keys + a `[first_use]` table

`settings.toml` is meant to be hand-editable. The schema reflects
that.

**Anchor files:** `src/settings/Settings.{h,cpp}`,
`src/settings/AppPaths.{h,cpp}`.

**Pattern.** Top-level keys for real settings, with enum values
where appropriate (`theme`, `open_files_in`). One nested table,
`[first_use]`, holds boolean acknowledgements for one-time prompts
(redaction warning, model-download dialogs). Settings load on
`Application` startup; every mutator calls `save()`. There is no
`[section][subsection]` nesting other than `[first_use]`.

**Recipe.** New setting = new top-level key (or new entry under
`[first_use]` if it's a one-time-acknowledgement flag), backing
field on `Settings`, getter + setter that calls `save()`. Grep the
header to discover all keys; there's no other index.

**Broken if.** A future setting feels like it needs a nested table
to organise. That's the moment to split the doc into multiple files
or rename keys with prefixes — not to introduce a hierarchy the
user has to learn.

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

## Deferred — pending the in-flight branch

The `claude/mystifying-proskuriakova-e07cb6` branch establishes
three patterns that should be added to this doc once it merges:

- **Three-tier persistence model** (per-file + per-type + per-window
  view state).
- **`MlScheduler` contract** — priority bands (UserAction / VisiblePage
  / Prefetch), `CancellationToken` propagation, `PowerSource` gating,
  single-worker serialisation.
- **Raw `IDocument*` cache pattern** — the way `MainWindow` retains a
  raw pointer to the current document across change events without
  outliving it, paired with `documentAboutToBeRemoved`.

These have anchor files on the in-flight branch, not on `main`.
Don't write them down speculatively here; wait for the merge.
