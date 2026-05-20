# In-Flight Merge Plan

> **STATUS: LANDED on 2026-05-20 as PR #24** (`4dba247 HITL waves 1-4`).
> The three CONVENTIONS sections drafted below have been applied
> to [`docs/CONVENTIONS.md`](CONVENTIONS.md) as §§11-13. This doc is
> retained as a historical record of the merge planning + the
> dependency / risk analysis. The "Verification checklist" at the
> bottom remains a useful post-merge walkthrough.

## Why this doc exists

The `claude/mystifying-proskuriakova-e07cb6` branch (HEAD `dbc4302`
at the time of writing) coordinated 9 workstreams across 4 waves of
substantial UX + ML governance work. The merge was sizable enough
that landing it cold would have been a surprise. This doc captured
the merge plan: branch shape, recommended order, known risks, the
three convention sections that should be appended to
`docs/CONVENTIONS.md` once it landed (now applied), and the
verification checklist the maintainer walks after the merge.
The roadmap commit `be4a0bf` on `claude/sweet-moser-66f739` did a
full survey of the branch (the *In flight* section it added to
`ROADMAP.md` is the long-form version); this doc is the operational
sibling.

## Branch shape

Four waves, nine workstreams. Each wave merged as one
`merge(wave-N): Workstream X` commit to keep the topology readable.

| Wave | Workstream | Scope | Primary file(s) |
|---|---|---|---|
| 1 | A | UX defaults: markup toolbar hidden, search collapses to icon, initial-window clamp, fit-to-content default | `src/ui/MainWindow.cpp`, `src/ui/MarkupToolbar.cpp` |
| 1 | B | Fit-mode persistence + per-page fit on arrow keys | `src/ui/MainWindow.cpp`, `src/document/IDocument.h` (`ZoomMode`, `applyZoomState`) |
| 1 | C | Thumbnail height halved with in-pixmap page-number badge | `src/ui/Sidebar.cpp`, `src/ui/ThumbnailModel.h` |
| 1 | I | Three-tier persistence: per-file + per-type + per-window state + macOS-style session restore | `src/recent/RecentFiles.{h,cpp}`, `src/settings/DocumentTypeDefaults.{h,cpp}`, `src/settings/Settings.{h,cpp}` (`[session]`), `src/main.cpp`, `src/app/Application.{h,cpp}` |
| 2 | D | Annotation hit-test fixes: hit existing before drawing, 6×6 handles, compound undo | `src/ui/AnnotationOverlay.{h,cpp}`, `src/annotation/AnnotationStore.{h,cpp}` (`beginCompound`/`endCompound`) |
| 2 | E | Perf: undo `m_nextId` cached in history frame, sidebar `changed()` debounced via 0-ms `singleShot`, highlights-count cached | `src/annotation/AnnotationStore.cpp`, `src/ui/Sidebar.{h,cpp}` |
| 2 | J | ML governance foundation: `MlScheduler`, `CancellationToken`, `PowerSource`; status-bar ML indicator; threaded into Ocr/Sam/BackgroundRemover | `src/ml/MlScheduler.{h,cpp}`, `src/ml/CancellationToken.h`, `src/platform/PowerSource.{h,cpp}`, `src/settings/Settings.h` (`[ml.scheduler]`) |
| 3 | F | OCR in-place: per-doc per-page `SelectableTextStore`, transparent `SelectableTextLayer` (cursor + drag + copy), `OcrController` pump; rebuilt Recognize Text dialog; 144 DPI render on white | `src/document/SelectableTextStore.{h,cpp}`, `src/ui/SelectableTextLayer.{h,cpp}`, `src/ui/OcrController.{h,cpp}`, `src/ui/OcrResultsDialog.{h,cpp}` |
| 3 | G | SAM in-place via `AnnotationOverlay` tool modes (`InstantAlpha`, `SmartLasso`); `SamController` owns shared session + encoder LRU | `src/ui/SamController.{h,cpp}`, `src/ui/AnnotationOverlay.{h,cpp}`, `src/ml/SamSession.{h,cpp}`; deletes `src/ui/SamSegmentDialog.{h,cpp}` |
| 4 | H | Remove Background polish: `BackgroundCandidateScorer` heuristic + sparkle badge; routed through `MlScheduler`; modal `QProgressDialog` replaced by status-bar indicator; `DocumentView::documentAboutToBeRemoved` | `src/ml/BackgroundCandidateScorer.{h,cpp}`, `src/ui/DocumentView.{h,cpp}`, `src/ui/MainWindow.cpp` |

## Merge sequence

Wave-by-wave is the natural order — the branch was assembled that
way and each wave was tested green on its own. Cross-wave dependencies
to be aware of:

- **Wave 2 J depends on wave 1 I** for `Settings::mlRunOnBattery()`
  (the `[ml.scheduler]` block lives in the same `Settings.{h,cpp}`
  that grew `[session]`).
- **Wave 3 F depends on wave 2 J** for the `MlScheduler` it submits
  OCR work to. Without J, F's `OcrController` has no scheduler to
  call. Land J first.
- **Wave 3 G depends on wave 2 J** for the scheduler and on wave 2 D
  for the `AnnotationTool::SmartLasso` / `InstantAlpha` tool modes.
- **Wave 4 H depends on wave 2 J** (routes scoring + removal through
  `MlScheduler`) and introduces `DocumentView::documentAboutToBeRemoved`
  — every wave-2/3/4 cache that keys by raw `IDocument*` (MainWindow's
  candidate cache, the SamController's encoder LRU, MlScheduler tasks
  tagged with the doc) relies on that signal to flush cleanly. The
  signal is in `DocumentView.h` from wave 4 H, so attach subscribers
  to it during the merge of H or later — earlier waves don't need it.

**Workstream G is declared-but-not-fully-delivered in one specific
respect.** The setting
`Settings::mlPreloadSegmentationOnToolActivation` (default `true`,
round-trips through `[ml.scheduler]` in `settings.toml`) has no
caller in production code — only the getter, setter, and round-trip
tests reference it. The *click-time* prepare path *is* wired (the
overlay calls `SamController::prepareForActive` on the first click of
an Instant Alpha / Smart Lasso gesture), so the in-place tools work
end-to-end. What's missing is the *eager preload* on tool activation
that the setting name promises. Land G as-is; track the preload caller
as the first follow-up after the merge.

## Risks and migration notes

The roadmap commit `be4a0bf` has a *Risks* section that adds four
entries from this branch. The most-likely-to-bite, in merge-order:

1. **Settings schema gains two new tables** (`[session]` and
   `[ml.scheduler]`). Old `settings.toml` files load fine (every new
   key has a default in `Settings.h`), and old `[redaction]` and
   `[first_use]` tables continue to be read on load. New saves drop
   the legacy `[redaction]` table in favour of the unified
   `[first_use]` bag. **Broken if** a user reports that a previously-
   acknowledged redaction warning re-appears: the legacy compat read
   path at `Settings.cpp:131` regressed.
2. **`recent.json` schema gains seven view-state fields** per entry
   plus base64 `windowGeometry`/`windowState` blobs. Legacy entries
   load cleanly via `RecentEntry::hasViewState()` returning `false` —
   the open path then falls through to per-type defaults, then to
   hardcoded defaults. A test (`tests/test_recent.cpp`) covers
   legacy-load round-trip. **Broken if** opening a file from a
   pre-branch `recent.json` re-orders the most-recent list or loses
   the `displayName`/`openedAt` fields.
3. **`m_lastUndoSource` heuristic gets brittler.** Wave 2 D added
   compound annotation undo: a 60-frame drag now collapses to one
   undo frame. PdfCommand still pushes one frame per page op. The
   `MainWindow::updateUndoRedoActions` heuristic that picks which
   stack a Ctrl-Z drains was tuned for one-frame-per-action on both
   stacks; interleaved gestures (rotate → drag → rotate) now expose
   the cross-stack ordering bug more clearly. This is roadmap-tracked
   as the unified-log work; nothing on the branch needs to change.
4. **Raw `IDocument*` lifetime contract is now load-bearing.** Five
   sites on the branch key by raw doc pointer:
   `MainWindow::m_backgroundCandidateDocs`,
   `MainWindow::m_pendingCandidateJobs`,
   `MainWindow::m_autoEnabledFormDocs`,
   `MainWindow::m_restoredViewStateDocs`,
   `SamController`'s encoder LRU, plus the OcrController's pending-
   keys map. All five flush via the `documentAboutToBeRemoved` signal
   (subscribed in `MainWindow`'s ctor at the top of the
   `DocumentView` wiring, plus `SamController::purgeDocument` in the
   same handler). **Broken if** a recycled allocator address shows up
   on a reopen and an old cached verdict / badge / handler fires
   against the new document — a missed subscription somewhere.
5. **Single-worker `MlScheduler` serialisation.** Wave 2 J runs at
   most one ML task at a time. Speculative submits (Prefetch / Idle)
   wait behind a long-running UserAction — by design (PHILOSOPHY:
   ORT session reuse + simpler debugging). A user clicking Recognize
   Text on a 200-page doc then trying Instant Alpha will see SAM
   prepare run only after the OCR pump drains. This is a deliberate
   trade-off, but worth knowing on the way in.
6. **Linux power detection is a stub** — `PowerSource::currentState()`
   returns `OnAC` on Linux, so battery-policy gating is a no-op there.
   Speculative ML runs at full tilt on a Linux laptop on battery.
   Track as roadmap follow-up; not blocking the merge. (Item already
   in [`cross-platform-sprint.md`](cross-platform-sprint.md) §Linux.)

## Three new `CONVENTIONS.md` sections (drafted)

Append these after section 10 once the merge lands. Delete the
existing *Deferred — pending the in-flight branch* section in
`docs/CONVENTIONS.md` at the same time.

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
preempt lower-priority *queued* work (the running task is allowed
to drain to its next checkpoint — no thread-killing). On battery
with `Settings::mlRunOnBattery() == false`, Prefetch and Idle
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
`PowerSource` 30-second reactor didn't run, or `cancelMatching`
missed the speculative tag.

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
   Qt::QueuedConnection round-trip.

**Broken if.** Closing a tab mid-ML-task leaves a stale entry in a
cache that fires later against a recycled address, or against
`nullptr`, and crashes; or: a verdict / badge / handler from a
previous document at the same heap address fires against a fresh
one (manifests as "wrong sparkle on Remove Background after a
fast close-and-reopen"). A `DocumentLifecycle` service that
generalises this is roadmap-tracked; until it lands, every
new raw-pointer-keyed cache must hand-subscribe.

---

## Verification checklist for the merge

Walk these after the merge lands, before pushing:

- `make` builds clean on macOS (arm64) and the CI Windows MSVC path.
- `make test` runs all unit tests green, including the new
  `test_ml_scheduler`, `test_cancellation_token`, `test_power_source`,
  `test_selectable_text_store`, `test_selectable_text_layer`,
  `test_background_candidate_scorer`, `test_sam_controller`,
  `test_document_type_defaults`, expanded `test_recent`,
  `test_settings`, and `test_annotation_store`.
- `scripts/run-uat.sh` (offscreen UAT) passes — including the
  expanded recognize-text, background-removal, instant-alpha-and-
  smart-lasso, and search-and-markup spec slots.
- Open a PDF, scroll to page 5, change zoom to fit-width, hide the
  sidebar, hide the markup toolbar, close. Reopen the same file:
  page 5, fit-width, sidebar hidden, markup toolbar hidden,
  window same size + position. (Per-file persistence.)
- Open a *different* PDF that Trailer has never seen: it picks up
  the type-default snapshot from the close above (fit-width,
  sidebar hidden, markup hidden). (Per-type persistence.)
- Open three docs across three windows; quit Trailer; relaunch
  with no args: same three docs reopen in the same windows.
- Drag-resize an annotation rectangle for ~60 frames. Press
  Ctrl-Z once: the annotation snaps back to its pre-drag bounds
  in a single step (not 60 micro-steps). (Compound undo.)
- Click an existing annotation while a drawing tool (Rectangle)
  is active: the existing annotation gets selected, no new one is
  created. (D1 hit-test order.)
- Open a 60-page raster PDF, watch the status bar: the ML
  indicator appears while OCR pumps the visible page + neighbours,
  goes away when idle; the >50 page hint chip is visible until
  the visible page is OCR'd.
- Activate Recognize Text on five pages: indicator label reads
  the user-action label, completes in foreground; clicking on
  text on those pages in the document shows an I-beam and
  drag-select works; `Ctrl+C` copies the joined text in reading
  order.
- Unplug AC mid-OCR-pump on a battery laptop: within ~30 s the
  scheduler cancels Prefetch / Idle; UserAction jobs (any
  explicit click) still run. (PowerSource reactor + battery
  policy.) Linux is exempted by current stub.
- Open an image where the background is a clean white sweep:
  the *Tools → Remove Background* entry shows the sparkle badge
  within a second or two. Open a noisy photo: no badge. (Wave 4.)
- Click *Tools → Remove Background* on a large image: the status-
  bar indicator shows "Removing background…" — no modal popup.
  Close the tab mid-removal: no crash. (`documentAboutToBeRemoved`
  flush + scheduler cancel.)
- Pick Instant Alpha on an image, click somewhere: a brief busy
  cursor while the encoder prepares, then drag refines the mask
  live, release commits. Switch to a different image, click
  Instant Alpha again: another prepare runs (cache key includes
  doc pointer + page + image hash).
- Hand-edit `~/.trailer/settings.toml` to flip
  `[ml.scheduler].run_on_battery = true`; relaunch; verify
  speculative jobs run even on battery (the
  `Settings::mlRunOnBattery()` path is reached). Then flip it
  back.
- Workstream G follow-up smoke: confirm that activating Instant
  Alpha / Smart Lasso *without clicking* does **not** kick off
  an encoder prepare. (Current behaviour — the
  `mlPreloadSegmentationOnToolActivation` setting has no caller
  yet. This is what the first post-merge follow-up wires up.)
