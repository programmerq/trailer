# Trailer — Roadmap

The tactical Now / Next / Later view of where Trailer is going.

**Companion docs.** [CRITERIA.md](CRITERIA.md) defines what "done,"
"priority," and "1.0" mean — this file's ordering is *computed* from
it, not chosen fresh each session. [DESIGN.md](DESIGN.md) has the
long-form phase spec (Phases 0–8) and is the *strategic* roadmap.
[TODO.md](TODO.md) is the live HITL punch list of pickable items.
[CHANGELOG.md](CHANGELOG.md) is what already shipped. This file is
the bridge — what's happening between releases at a glance.

**House rules.**

- **No version-number targets.** Per project policy, the version
  number falls out of what ships, picked at tag time with semver
  applied to the diff. Don't plan toward a specific "0.2.0" /
  "0.3.0" — talk about "next release."
- **No calendar dates.** Trailer moves fast enough that dates are
  either false precision or stale within a week. Sequencing comes
  from this doc; timing is whatever the maintainer feels.
- **One thing at a time.** Solo maintainer + AI-agent assistance.
  The roadmap is a queue, not a parallel plan. *Now* should hold
  whatever the next session picks up.
- **Roadmap edits are batched.** Update this doc when priorities
  shift meaningfully — not for every commit. Frequent thrash
  signals unclear strategy, not responsiveness.

Last meaningful update: post-0.1.0 ship + reframe after PR #24
landed and the post-#25 HITL pass surfaced gaps (2026-05-20);
quick-wins pass cleared Now #2 (SAM preload), Next #10 (Linux power),
and two HITL items (search-bar close, page-mode shortcuts).

---

## Recently landed (PR #24 + PR #25 + PR #26)

The 30-commit working branch that this section described as
"in flight" through 2026-05-18 **merged onto main as PR #24** on
2026-05-19 (squash-merge commit `4dba247 HITL waves 1-4`). The
roadmap was not updated at the time; the 2026-05-20 reframe corrects
the picture below. The branch ref `claude/mystifying-proskuriakova-
e07cb6` still exists but is now *behind* main (its tip is the
merge-base) — there's nothing to recover from it that isn't already
on main.

What actually shipped, with what's still rough flagged inline:

- **Wave 1 — UX defaults + persistence (A, B, C, I).** Markup
  toolbar hidden by default, search bar collapses to icon button,
  initial window-size clamp, fit-to-content default zoom; fit-mode
  persistence + per-page fit on arrow keys; thumbnail size halved
  to 80×100 with in-pixmap page-number badge
  ([src/ui/Sidebar.cpp:94](src/ui/Sidebar.cpp:94)); per-file +
  per-type + per-window state restoration including macOS-style
  "open the windows you left open." New `Settings` `[session]` +
  `[ml.scheduler]` blocks. New module
  `src/settings/DocumentTypeDefaults`. **Rough edge:** the thumbnail
  delegate's `sizeHint` returns 108 px per row but live sidebars
  still render rows much taller — captured in
  [TODO.md](TODO.md)'s 2026-05-20 HITL section. Search-bar close
  button doesn't collapse the toolbar slot (QWidgetAction wrapper
  not hidden) — same place.
- **Wave 2 — ML governance + annotation perf (D, E, J).**
  `src/ml/MlScheduler` (single-worker priority queue:
  `UserAction > VisiblePage > Prefetch > Idle`, with 30s
  power-policy re-evaluation), `src/ml/CancellationToken`,
  `src/platform/PowerSource`. Threaded through OcrEngine,
  SamSession, BackgroundRemover. Annotation hit-test order swap
  (existing annotations tested before drawing tools), 6×6 handles,
  compound undo (`AnnotationStore::beginCompound/endCompound`),
  sidebar debounce, status-bar ML indicator.
- **Wave 3 — OCR + SAM in-place (F, G).** `SelectableTextStore`
  (per-doc per-page hash-keyed OCR cache, in-memory),
  `SelectableTextLayer` (transparent overlay with honest cursor +
  drag-select + Ctrl+C), `OcrController` (per-window pump:
  VisiblePage + ±1 Prefetch). `PdfDocument::renderPageForOcr` at
  144 DPI on white. Rebuilt `RecognizeTextDialog` as a parameter
  UI. Large-doc (>50 pages) hint chip. Smart Lasso / Instant Alpha
  in-place via `AnnotationOverlay` tool modes (Workstream G's
  merge — preload-on-tool-activation shipped locally in `5bd27de`,
  completing the wave-2/3/4 ML governance arc).
- **Wave 4 — Background-removal polish (H).**
  `BackgroundCandidateScorer` (Sobel edges / HSV saturation /
  luminance bimodality, threshold 0.50). Sparkle badge on the
  Tools→Remove Background action. Removal routed through
  `MlScheduler` at `UserAction`; modal `QProgressDialog` replaced
  by the status-bar indicator. `DocumentView::documentAboutToBeRemoved`
  signal for cache invalidation before raw `IDocument*` keys
  dangle.
- **PR #25 — release infrastructure.** CHANGELOG, RELEASING
  runbook, VERSION + release-notes scripts; PDF page-op undo
  (rotate/delete/move/insert/crop); ROADMAP scaffolding. The
  ROADMAP wave summary above was authored by PR #25 but missed
  the PR #24 landing date.
- **PR #26 — audit + process scaffolding.** 15-lens audit,
  CONVENTIONS.md, smoke-session protocol, TODO source-type
  preamble.

Anything still rough from Wave 1 lives in
[TODO.md](TODO.md)'s 2026-05-20 HITL section, not back in this
file.

---

## Now (next release window)

CRITERIA.md §4's ranking function (job degraded × how badly × how
often) computes **one head**, not a pickable list —
[TODO.md](TODO.md) carries the full ranked queue, sources, and line
numbers; this is the at-a-glance summary. Read the head and go.

**Now.** Add endpoint drag handles to Line/Arrow annotations — *J3 ·
Blocks · frequent*. The resize path only ever writes `bounds`, never
`points`, so dragging a Line/Arrow's own endpoint handle has no
visual effect (`AnnotationOverlay.{h,cpp}`).

**Then**, in ranked order:

2. First-run auto-OCR is a silent no-op when the OCR model isn't on
   disk yet — *J5 · Blocks · frequent*.
3. Copy Page as Image copies the un-annotated raster — every mark is
   dropped — *J3 · Blocks · frequent*.
4. Annotations save to the wrong page after a delete/move, stale page
   indices — *J7 · Blocks · occasional*.
5. No "changed on disk" detection — external edits silently
   reload/overwrite or desync undo — *J1/J3/J4 · Workaround ·
   frequent*.

Full ranked queue (15 open items), the someday pool, and the
ambiguity ledger behind the provisional tie-break rule live in
[TODO.md](TODO.md).

## Next

Planned, not yet started — strategic items **beyond** the tactical
ranked queue. The queue itself and the someday pool live in
[TODO.md](TODO.md); this section deliberately does **not** re-list them,
so a queue item (like the current *Now* head) never also shows up here as
a "deferred" future item. What remains is release infrastructure and
forward features the ranked queue doesn't carry:

- **Signed-update channel (macOS + Windows).** The requirement, not the
  implementation: existing installs on both desktops can discover and
  pull new releases over a cryptographically signed channel that does
  **not** require Apple Developer Program enrollment. Sparkle 2's
  ed25519-signed appcast is the leading macOS candidate, with
  **WinSparkle** as its Windows sibling — same appcast XML format and
  ed25519 pubkey, one signed feed serves both. A thin custom checker
  against GitHub's `/releases/latest` API with our own ed25519
  verification also qualifies, on either OS. Velopack does **not**
  qualify — its trust model leans on Apple Developer ID / Microsoft
  Authenticode, which the no-Apple-Dev policy rules out. Needs: ed25519
  keypair + safekeeping, signed feed hosted on GitHub Pages (or
  equivalent), library linkage into each bundle, "Check for Updates…"
  menu item, RELEASING.md amendment. The point: when our release
  pipeline is compromised, existing users don't get malware as an
  "update." Non-job infra — CRITERIA.md §4 parks it in someday since it
  degrades no listed job, but it's real and well-scoped; tracked in
  detail in [TODO-packaging.md](TODO-packaging.md) and carried in
  [TODO.md](TODO.md)'s someday pool.
- **Sidebar TOC / Highlights & Notes — edit + jump-to.** Today
  read-only. Click-to-jump on a TOC entry and click-to-scroll on a
  highlight would close the loop.

Everything else this section used to enumerate has moved. In
[TODO.md](TODO.md)'s ranked queue: the first-time OCR download pump,
word-level OCR selection, `SelectableTextStore` disk persistence,
shape-aware Line / Arrow handles (the current *Now* head), FreeText
`/AP` appearance streams, and the multi-doc image-batch `ThumbnailModel`.
In its someday pool: the Linux/Windows screenshot region pickers and the
two-page view layout. Two items shipped in the 2026-07 burst and are no
longer planned work: the unified **Preferences pane** now exists
(`src/ui/PreferencesDialog.cpp`, tabs General / Files / Machine Learning
/ Advanced, under four accepted decision records in
[`docs/decisions/`](docs/decisions/) `0001`–`0004`) and bears on gate
**G7** rather than this list; the Linux `PowerSource` implementation
landed in `9b24eb4`.

## Later

Directional. Inclusion here is a commitment to the *direction*,
not the *scope* or *timing*.

- **Flathub submission (Linux Flatpak).** Lowest-effort Linux
  auto-update story; Flathub handles per-user updates over its
  existing infrastructure.
- **Intel Mac binary.** Either an ML-disabled-everywhere mode OR a
  third-party ONNX x86_64 bundle — both sanctioned by project
  policy. Low priority.
- **Remaining Phase 6 format / colour work.** OCR-in-place landed
  in PR #24; remaining: **HEIC, OpenEXR, RAW** read support;
  **lcms2** colour management (Assign Profile, Soft Proof);
  **OCR-embed-on-PDF-export**; **alt text generation**. The
  `MlScheduler` foundation makes anything inference-shaped
  (notably alt text) substantially cheaper to ship — the natural
  next candidates now that the foundation is in place.
- **`MlScheduler` per-priority eviction policies + concurrent
  workers.** Today single-worker, pre-cancels on submit but doesn't
  impose per-priority CPU/memory caps. As feature count grows ("I
  want SAM ready *while* OCR runs"), a small worker pool plus a
  user-facing "pause all ML" toggle become reasonable.
- **Compound undo at the `PdfCommand` layer.** AnnotationStore now
  has it; rotating three pages still produces three undo frames.
  The unified chronological log shipped without it (see Risks
  below); batching page ops into one chronological-log entry is
  still open.
- **Generalise `documentAboutToBeRemoved` into a `DocumentLifecycle`
  service** (or make `IDocument` a `QObject`). Today the signal is
  hand-subscribed by every cache that holds raw `IDocument*` keys
  outside `DocumentView`'s lifetime. The contract is implicit and a
  recycled-allocator-address bug class is one missed subscription
  away.
- **Refactor: split `MainWindow` into `MainWindow` +
  `DocumentSessionCoordinator`.** PR #24 concentrated +696 lines
  in `onCurrentDocumentChanged` and friends. Not blocking but the
  gravity is getting strong.
- **Phase 7 stretch.** 3D viewing (USD, Collada, OBJ, STL); scanner
  support (SANE / WIA / ImageCaptureCore); camera import
  (libgphoto2); photo location / map view; local search index +
  command palette.
- **Phase 8 polish.** Full keyboard-shortcut audit; screen-reader
  audit; **localisation framework + community translations**
  (worth bumping up — Trailer's strings are already `tr(…)`-wrapped,
  and OCR now exposes multi-language selection in the dialog);
  user-facing documentation site.
- **Path to 1.0.** What "1.0" means — every job's bar (J1–J8) met on
  both Windows and Linux, plus an N-week dwell of real document work
  with no fallback to Preview — is defined in
  [CRITERIA.md](CRITERIA.md) §7, not restated here. PHILOSOPHY.md's
  stability gates (on-disk format thrash settled, UX defaults quiet
  for a minor cycle, gates G7/G8) still apply on top. Feature-complete
  (PR #24 covered most of the "open it, do one thing, move on"
  mandate) is necessary but not sufficient — CRITERIA.md's dwell
  clock is the real gate, and it resets on any regression that sends
  the owner back to Preview.

## Won't have

Explicitly off the table so they stop eating planning oxygen.

- **Apple Developer Program enrollment** ($99/yr). Deferred
  indefinitely by project policy. The `xattr -dr
  com.apple.quarantine` install instruction is the standing answer
  for macOS. Reopening requires a funding plan attached.
- **Notarized macOS builds.** Gated on the above.
- **Cloud sync, accounts, telemetry, premium tier, ads, model
  training on user content.** Non-negotiable per PHILOSOPHY.md.
- **Per-PDF AutoFill matcher tuning.** Direct field manipulation
  is the primary path; AutoFill is a demoted secondary feature.
- **Apple Pencil / iOS / mobile builds.** Desktop Qt6 widgets
  only.
- **Browser / web version.** Native only.
- **Replacing Photoshop / Acrobat / GIMP.** Trailer is a workbench
  for "open it, do one thing, move on" — not a full editor or
  forensic PDF tool.
- **Telemetry-driven tuning of ML thresholds.** The
  `BackgroundCandidateScorer` 0.50 threshold and similar magic
  numbers stay hand-tuned. No anonymous-metrics collection,
  consistent with PHILOSOPHY.md.
- **Multi-process / sandboxed worker model for ML.** A
  single-process scheduler is the deliberate choice today (ORT
  session reuse, shared cancellation, simpler debugging). Revisit
  only if a real concurrency requirement forces it.

---

## Risks

- **Raw `IDocument*` keys outside `DocumentView` lifetime.** PR
  #24's `OcrController` + Remove-Background-candidate cache hold
  raw pointers safely because they subscribe to a new
  `documentAboutToBeRemoved` signal. The contract is *implicit* —
  any future code path that destroys a document outside
  `DocumentView::onTabCloseRequested` will silently keep stale
  keys. **Mitigation:** generalize into a service when the third
  cache lands, or make `IDocument` a `QObject` (tracked under
  *Later*).
- **Compound undo + cross-stack ordering.** Compound annotation
  undo collapses drags to one frame; the PdfAdapter used to choose
  which stack to undo by last-touched source, so interleaved
  gestures exhibited "Cmd-Z unwinds the wrong thing" symptoms.
  **Resolved:** the unified chronological log shipped (both
  adapters, `AnnotationStore` + `PdfCommand`) and retired the
  heuristic.
- **Update-signing key management.** Whichever ed25519
  implementation we pick (Sparkle's appcast, WinSparkle's
  matching feed, or a custom checker), the private key needs to
  be (a) safely stored — losing it strands every existing user
  on whatever version they have, and (b) reachable from CI to
  sign each release. **Mitigation:** store in a password manager
  *and* a GitHub Actions secret; document recovery in
  RELEASING.md once the updater lands; consider a "new pubkey"
  entry signed by the old key for rotation.
- **Continuous-mode overlay drift may be Qt-PDF-blocked.** The
  view-geometry information needed for a correct fix may not be
  exposed by Qt PDF's current API. **Mitigation:** scope before
  implementing; if Qt-blocked, choose between per-page overlay
  widgets and waiting on / contributing the Qt patch.
- **`MlScheduler` is single-worker.** Two big inferences serialise
  (OCR on a heavy page + BG-removal on a 24MP image). Correct
  today (CPU + ORT memory contention) but as feature count grows,
  workflows like "I want SAM ready *while* OCR runs" will start
  to feel slow. **Mitigation:** tracked under *Later*; not yet
  user-visible.
- **Implicit contracts compound.** PR #24 introduced several quiet
  conventions: the `QPointer<…>` worker-write-back pattern in
  `OcrController`; the per-priority setting names under
  `[ml.scheduler]`; the `Sidebar::Mode` ↔
  `RecentFiles::SidebarMode` synchronization via `static_assert`.
  These work but are easy to break with innocent refactors.
  **Mitigation:** CONVENTIONS.md (added in PR #26) is the right
  home; extend it as new patterns crystallise. The risk is now
  about *drift between CONVENTIONS.md and the code*, not about
  the conventions being uncodified.
- **Multi-workstream coordination via commit subjects only.** PR
  #24 coordinated 9 workstreams across 4 waves with no tracking
  doc; the structure existed only in commit messages. This worked
  once; it scales badly, and the resulting ROADMAP staleness
  (this very reframe) is one of the second-order costs.
  **Mitigation:** when the next multi-stream branch is on the
  horizon, open a tracking document (or issue) before the first
  commit, and update ROADMAP at merge time, not before.
- **CHANGELOG discipline drift.** New convention as of the
  release-tooling pass — `[Unreleased]` is updated as features
  land. **Mitigation:** `scripts/release-notes.sh v$PREV..HEAD`
  reconciles from `git log` if entries get missed.
- **UAT slot coverage drift.** Tight coupling between fixes and
  UAT slots is a discipline issue, not a tooling one.
  **Mitigation:** the rule is now a stated house rule in
  [AGENTS.md](AGENTS.md); review checklist should ask "is there
  a UAT slot?" for every user-visible change.
- **Status claims go stale between bursts.** This project moves in
  concentrated bursts — the maintainer's own sessions plus parallel
  AI-agent branches — with quiet stretches between. The
  2026-07-09–12 burst alone shipped the empty-state window model, a
  unified Preferences dialog, an undo-stabilization batch, and the
  full ADR/Arbiter adjudication machinery in roughly 50 commits, and
  the working docs — written at the pace of the *previous* burst —
  silently drift out of sync with what the code now does. Caught and
  corrected while drafting CRITERIA.md: the *Next* list had carried a
  unified Preferences dialog as a planned, "timely" item long after it
  shipped with four accepted decision records behind it — that entry has
  since been retired from *Next* (the dialog now bears on gate G7).
  **Mitigation:** treat a status claim in ROADMAP.md or TODO.md as a
  hypothesis to verify against current source, not a fact —
  especially right after a burst. CRITERIA.md §3's cold-open
  self-test ("name the single next action without reading history")
  is the standing check; run it whenever a burst lands, not on a
  fixed cadence.
