# 0008 — Staged document open: what runs synchronously, what moves off-thread, and how a large file is staged

- **Status:** accepted
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-15
- **Date accepted / superseded:** 2026-07-20 (accepted for the **image** path; see the verdict for the scope boundary against the PDF path)

> **Update (2026-07-21, owner refinement on PR #109 — delayed placeholder
> text).** The owner directed a UX tweak to the shipped image path, verbatim:
> "Only display the 'loading' message after a 1 second delay. If the image loads
> before that 1 second time, then don't display the text. … Still do the header
> read and draw the window first → empty text for up to 1 second → display text
> until image loads." This changes a **user-visible default**: the "Loading
> image…" text no longer appears at first paint; the header-sized window is drawn
> immediately and the placeholder stays **blank** for a grace delay, after which
> the text is shown *only if* the decode is still in flight. A decode that beats
> the grace swaps in the real image with the text never shown — removing the
> single-frame text flash on fast/local opens — while a slow/large decode still
> gets the honest loading state. Header-draw-first and the off-thread decode are
> unchanged; this only defers *when* the text is painted. The grace value is a
> hand-tuned constant, `m_placeholderTextDelayMs` (default 1000 ms) at
> `src/document/ImageAdapter.h:508`, which carries the required in-code rationale
> (what it represents, range considered, symptom to change); it is injectable
> per-document for deterministic tests. The first-paint admissible objection
> above is annotated to keep the shipped grace-then-text behaviour consistent
> with this record. Evidence: `docs/uat/images/staged-open-01-grace-blank.png`
> (grace), `-02-loading-placeholder.png` (post-grace, slow load),
> `-03-decoded-image.png` (decoded).

## Context

Opening a document in Trailer is, today, **fully synchronous on the GUI thread**,
and it does the heavy work of a large file three times before the view exists.
`Application::openFiles` calls `m_registry.open(path)` inline
(`src/app/Application.cpp:140`) — the only `QtConcurrent::run` in `MainWindow` is
the *save* path (`src/ui/MainWindow.cpp:2215`), not open, so there is no
worker-thread open seam. That call flows through `DocumentRegistry::open()`
(`src/document/DocumentRegistry.cpp:14-18`) → `PdfAdapter::open()`
(`src/document/PdfAdapter.cpp:1524-1540`), which constructs `PdfDocument` inline.
The `PdfDocument` constructor (`src/document/PdfAdapter.cpp:146-166`) then, on the
calling (GUI) thread:

- parses the whole file with `QPdfDocument` (`PdfAdapter.cpp:149` — parse #1, the
  one the viewer actually needs to paint page 1);
- parses the **same** file again with qpdf `processFile` via `m_editor->load(m_path)`
  (`PdfAdapter.cpp:157` → `PdfEditor.cpp:33-38` — parse #2, only needed for
  editing / round-trip save); and
- eagerly sweeps **every page** via `readAnnotations()`
  (`PdfAdapter.cpp:158-160` → `PdfEditor.cpp:1082-1089`), where `getAllPages()`
  plus per-page `getMediaBox` / `getKey("/Annots")` force whole-document object
  resolution in qpdf.

The user-facing behaviour this produces is the P0 originally tracked in
`docs/backlog/2026-07-15-offthread-pdf-open-placeholder.md` (the residual
successor to the former `2026-07-13-startup-hang-large-pdf` P0, closed by #63):
opening a 142 MB text-layer
PDF on the owner's real-Mac dogfood pass hung the whole app for minutes, pinning
100% of one core, with no first page, no progress, and no cancel path. That
directly violates the binding structural invariants in
`docs/performance-budgets.md:56-66` — *first-page render must not block on a
full-file read* and *the whole UI never blocks during long work* — which are the
corpus-independent, CI-enforceable part of the budgets file (the perf-measurement
ruling: CI enforces only structural invariants, never a wall-clock assertion,
`docs/performance-budgets.md:98-115`). It is also exactly the staging the B4 launch
row (`:128`), the B5 progress-indicator row (`:129`), and the B6 cancel row
(`:130`) presuppose but the code does not yet honour.

This record exists to settle **how** open is staged, per Theme 3 of the UX research
agenda (`docs/research/2026-07-13-ux-research-agenda.md:90-121`): what work is
legitimate to run synchronously on open, what must move off the main thread, and
how a large-file open is staged so the first page paints before the whole file is
parsed — **without regressing edit/annotation correctness**. It does not fork the
backlog item; it ratifies and refines the fix-direction that item already sketches
(`docs/backlog/2026-07-15-offthread-pdf-open-placeholder.md`; formerly the
`2026-07-13-startup-hang-large-pdf` P0, closed by #63).

**What ships today (so this record is not misread as describing the target):** a
single synchronous GUI-thread open that does parse #1, parse #2, and the all-pages
annotation sweep before the view exists. The `QSKIP` in
`tests/test_perf_gui_thread_io.cpp:93-98` records that "there is no worker-thread
open seam yet" (`:9-15`) and skips the target invariant rather than asserting it.

External grounding (a template for the options, not a ruling for Trailer):

- Apple's *Prioritize Work with Quality of Service Classes* (Energy Efficiency
  Guide) names **User-Initiated** work as "work the user has initiated requiring
  immediate results, such as **opening a saved document** … nearly instantaneous
  (a few seconds or less)," and **Utility** as work that "may take some time and
  doesn't require an immediate result … typically with a visible progress bar." A
  document open is a User-Initiated action whose *first paint* must feel immediate,
  while the heavier tail (full editor parse, annotation resolution) is Utility-class
  work that belongs off the main thread —
  https://developer.apple.com/library/archive/documentation/Performance/Conceptual/EnergyGuide-iOS/PrioritizeWorkWithQoS.html
- NN/g's response-time limits (0.1 s instantaneous / 1 s flow / 10 s attention)
  already frame `docs/performance-budgets.md` (cited at `:30-31`): first paint
  should land inside the 1 s flow limit; past 10 s a percent-done indicator **and**
  a cancel path are mandatory — https://www.nngroup.com/articles/response-times-3-important-limits/
- Reference apps: Adobe's **linearized PDF / Fast Web View** reorganises a file so
  "the first page can be displayed immediately, before the entire file has finished
  downloading," with hint tables pointing at later pages
  (https://mapsoft.com/posts/linearized-pdf.html) — the canonical "first page
  before full parse" staging, though it describes network streaming, not a local
  disk open. Whether Preview.app paints page 1 of a local 100 MB+ PDF near-instantly
  while the rest streams from disk is the same behaviour observed locally, but is
  **(needs-live-verification)** on a real Mac — no reputable source documents
  Preview's local staging.
- Qt seams: `QtConcurrent::run` "runs a function in a separate thread. The return
  value … is made available through the QFuture API," and `QFutureWatcher` delivers
  the result back on the GUI thread via its `finished` signal without a blocking
  `QFuture::result()` call (https://doc.qt.io/qt-6/qtconcurrentrun.html). This is
  exactly the in-repo save-path pattern to mirror for open: worker phase in
  `QtConcurrent::run` (`src/ui/MainWindow.cpp:2215-2216`) → `QFutureWatcher::finished`
  commits on the UI thread (`:2187-2213`). `QPdfDocument` itself loads with a
  `Null → Loading → Ready` status and renders pages individually via `render()`
  (https://doc.qt.io/qt-6/qpdfdocument.html), so parse #1 already has a lazier
  seam than the qpdf `processFile` editor load, which resolves the whole document.

## Options

- **A. Defer the qpdf editor load until first edit.** Keep `registry.open()` on the
  calling thread, but make `m_editor->load` (`PdfAdapter.cpp:157`) and the
  `readAnnotations()` sweep (`:158-160`) **lazy** — triggered on the first edit or
  first annotation access rather than in the `PdfDocument` constructor. Parse #1
  (`QPdfDocument`, `:149`) still runs on open to paint page 1. This removes parse #2
  and the all-pages sweep from the open path entirely; the structural proxies
  (editor-parse-count 0, 0 pages walked before first paint) pass without moving open
  off-thread. It does **not** by itself satisfy the "no GUI-thread file read"
  proxy — parse #1 still reads on the GUI thread — so the `QSKIP`'s thread-identity
  assertion cannot yet flip.
- **B. Worker-thread open with a placeholder first page, then stream.** Move
  `registry.open()` onto a worker via the `QtConcurrent::run` + `QFutureWatcher`
  pattern already proven for save (`src/ui/MainWindow.cpp:2215-2217`). The view shows
  a placeholder page immediately; page 1 paints as soon as the worker's parse #1
  reaches `Ready`; the editor load and annotation resolution continue off-thread.
  This satisfies all three proxies including "no GUI-thread file read," but it is the
  larger change and raises thread-affinity questions for `QPdfDocument` / qpdf objects
  and for the edit path.
- **C. Hybrid (defer **and** off-thread).** Defer the qpdf editor load and
  annotation sweep to first access (A), **and** move the remaining open work —
  parse #1 — onto a worker with a placeholder first page (B). This is the only option
  that both eliminates the redundant parse #2 / eager sweep from the open path *and*
  clears the GUI thread of the file read, letting every structural proxy flip from
  `QSKIP` to a real assertion.

## Personas debate

- **Office non-technical user:** Opens a bill or a lease addendum and expects it to
  appear at once. Has no mental model of "parsing"; a multi-minute frozen window
  reads as a crash, and there is no cancel to escape it. Wants first paint fast under
  every option; is indifferent to whether editing is deferred, provided that when
  they later highlight a line it just works. A and C both give fast first paint; B's
  placeholder-then-page-1 is acceptable **if** the placeholder is honest (clearly a
  loading state, not a blank document that looks finished).
- **Older careful user:** Fears the app "doing something behind my back." A
  deferred editor load (A/C) is invisible and therefore fine — *until* the first
  annotation access triggers a multi-second stall with no warning, which is the exact
  "it froze when I clicked" surprise this lens dreads. Their stake is that any
  deferred heavy work, when it finally fires, must itself carry feedback and a cancel
  path (B5/B6, `docs/performance-budgets.md:129-130`), not silently move the freeze
  from open-time to first-edit-time.
- **Power migrator (ex-Preview / ex-Acrobat):** Expects the linearized-PDF norm —
  page 1 now, the rest streaming (Fast Web View). Reads a frozen open as strictly
  worse than both reference apps. Favours B/C because "placeholder then page 1 then
  the rest" is the behaviour they already trust; would find A acceptable for first
  paint but notice the first-edit stall that A leaves on the GUI thread.
- **Occasional user:** Opens Trailer rarely and won't remember any of this. Needs the
  common case — open, look, maybe sign — to never hang. Has no stake in the A-vs-B
  architecture, only in the outcome: first page fast, no freeze, and if anything does
  take >10 s it must show progress and let them cancel.

## Admissible objections

- **Older careful user, Option A (deferred editor load), first-annotation step:**
  deferring `m_editor->load` + `readAnnotations()` to first access moves the
  whole-document qpdf resolution out of open and into the first edit/annotation
  action. On a large file that action then stalls the GUI thread — the freeze is
  relocated, not removed — and it fires with no progress indicator or cancel, failing
  B5/B6 (`docs/performance-budgets.md:129-130`) at the step "I clicked to highlight
  and the app froze." Any option that defers this work must also ensure the deferred
  work, when triggered, runs off-thread or carries feedback + cancel.
- **Office / occasional user, Option B placeholder, first-paint step:** if the
  placeholder page is not visibly a loading state, the user reads a blank or stub
  page as the *actual* document (an empty PDF, a failed open) and acts on it — the
  concrete failure is "I thought the file was empty / broken." Option B is only
  admissible with an honest placeholder that resolves into page 1. **(Refined
  2026-07-21 — see the Update note below.)** The shipped image path shows the
  placeholder text after a ~1 s grace rather than instantly: a decode that beats
  the grace swaps in the real image with no text (no single-frame flash), and a
  decode slow enough that the user could otherwise mistake the window for empty
  gets the honest "Loading image…" text before that point. The window itself is
  drawn header-sized from the first frame, so the "blank" grace is a momentary,
  correctly-sized empty region, not a stub document the user can act on — the
  objection stays satisfied.
- **Any editing user, Options A/B/C, edit-correctness step:** the research question
  bars regressing edit/annotation correctness. Whichever way the editor load is
  deferred or moved off-thread, the first edit or save must observe a *fully and
  correctly* loaded qpdf editor — the same bytes and annotation set the eager path
  produced. The concrete failure to guard against is "my annotation landed on the
  wrong page / my save dropped an existing annotation" because an edit raced a
  half-completed background load. This is the correctness floor every option must
  clear, and it is the reason the structural proxies are necessary but **not**
  sufficient on their own.

### Rejected as naked preference

- "Just make open faster / it should be instant." — rejected: names no user, step,
  or failure and no seam; it restates the symptom. The admissible version is the
  first-paint objections above, tied to specific file:lines and budget rows.
- "Threads are risky, keep it synchronous and optimise the parse." — rejected as a
  naked preference: it asserts a taste about threading and names no user-step-failure
  that the synchronous path avoids. The synchronous path is precisely what produces
  the documented multi-minute hang; a threading concern is admissible only as the
  edit-correctness objection above (a concrete race at a concrete step), which is
  already listed.

## Checkable threshold this record would establish

Deterministic **structural** proxies (per the perf-measurement ruling —
agent-measured locally + reviewer check; CI enforces only corpus-independent
structural invariants, **never** a wall-clock assertion,
`docs/performance-budgets.md:98-115`), tied to the binding invariants at
`docs/performance-budgets.md:56-66` and the B4/B5/B6 rows (`:128-130`), retiring the
`QSKIP` in `tests/test_perf_gui_thread_io.cpp:93-98`:

1. **No all-pages walk before first paint.** The page-visit counter in
   `PdfEditor::readAnnotations()` (`src/document/PdfEditor.cpp:1082-1089`) reads
   **0 pages** during `DocumentRegistry::open()` — annotations are resolved
   lazily / off-thread, not eagerly across every page at construction.
2. **No second full-file parse before viewer-ready.** The editor-parse counter
   incremented in `PdfEditor::load` (`src/document/PdfEditor.cpp:33-38`) is still
   **0** at the moment `pageCount()` first returns > 0 — the qpdf `processFile` load
   is deferred until the first edit / annotation access.
3. **No synchronous file IO on the GUI thread.** The live `QSKIP` in
   `tests/test_perf_gui_thread_io.cpp:93-98` becomes a real assertion:
   `InstrumentedIODevice::readThreads()` (`tests/perf_iodevice.h`) contains **no**
   entry equal to the GUI thread.

These are pass/fail behaviours a reviewer or agent can declare independently, on any
machine, with no corpus and no wall-clock. They do **not** assert a latency number:
B4 stays advisory until the reference rig + corpus is ratified
(`docs/performance-budgets.md:71-96`), and this record adds no timing gate.

Because the three options clear different subsets of these proxies, the threshold
each option *would* establish differs: Option A clears proxies 1 and 2 but leaves
proxy 3's `QSKIP` in place (parse #1 still reads on the GUI thread); Option B clears
all three but does not by itself remove parse #2 from the load path; **Option C is
the only option under which all three proxies flip to real assertions.** Whichever
option the arbiter picks, the edit-correctness floor (admissible objection #3) is a
co-requirement: the proxies gate the *staging*, not the *correctness*, and both must
hold before this record can be accepted.

## Arbiter verdict + rationale

**Option B, scoped to the IMAGE path.** This verdict adjudicates the staging of an
**image** document open (still images: PNG / JPEG / BMP / TIFF / WebP / …). The
**PDF** path is *not* decided here: it was already settled by the accepted record
`0006-defer-offthread-pdf-open.md` (Option A — lazy editor/annotation loading,
with the residual `QPdfDocument::load` off-thread read behind a placeholder
captured as a P2 follow-up). Moving that residual PDF read off the GUI thread is
the sibling PDF work item, gated by ADR 0006's "Evidence required to reopen"; this
record does not pre-empt it.

Why **B** for images, and why A and C collapse for this path:

- The three structural proxies in the threshold below are **PDF-shaped** — parse #1
  (`QPdfDocument`), parse #2 (qpdf `processFile`), and the all-pages `/Annots`
  sweep. An image open has **none** of that structure: it is a single pure
  operation, `QImageReader::read()`. There is no "second parse" and no
  "all-pages sweep" to defer, so **Option A's deferral has no image analog** (its
  distinguishing move — keep the one read on the GUI thread, defer the *rest* —
  leaves the *only* heavy pass, the full-pixel decode, on the GUI thread, which is
  exactly the freeze this staging removes). **Option C (hybrid defer + off-thread)
  collapses into B** for images, because the "defer" half it adds over B targets a
  redundant second parse that images do not have.
- **Option B is the natural and lowest-risk fit for an image decode.** The
  edit-correctness floor (admissible objection #3) that made B risky for PDF —
  `QPdfDocument` / qpdf thread-affinity, a half-loaded editor racing the first
  edit — **does not arise for images**: the worker constructs a *fresh*
  `QImageReader` from the path and shares nothing with the GUI thread (`QImage` is
  reentrant, copy-on-write), mirroring the "throwaway instance, shares nothing"
  discipline ADR 0006 adopted for its annotation sweep. The single piece of shared
  state — the decoded `m_image` — is adopted **only on the GUI thread** (the
  `QFutureWatcher::finished` slot, or the blocking `ensureDecoded()` that every
  pixel-access caller reaches while itself on the UI thread, per the
  OcrController / ThumbnailModel "render on the calling UI thread" contract), so no
  edit can observe a half-decoded image.
- **The honest-placeholder objection (Office / occasional user) is satisfied by
  construction.** The placeholder is a visible "Loading image…" state sized from a
  header-only `QImageReader::size()` `contentSizeHint`, not a blank or stub image
  that could be mistaken for an empty/broken file (G3 — no lying controls). It
  resolves into the real pixmap on decode completion.
- **The older-careful-user objection against A** (a deferred heavy pass that later
  stalls the GUI at first interaction with no feedback) is the decisive one for
  images: A would relocate the decode freeze from open-time to first-zoom /
  first-edit time. B removes it outright by decoding proactively off-thread, so
  there is no "it froze when I clicked."

**Image-path threshold established (replaces the PDF-shaped proxies above for this
path):** the four pass/fail points in the backlog item
`docs/backlog/2026-07-15-staged-image-open.md` — (1) no full-pixel
`QImageReader::read()` on the GUI thread at open (only a header-only `size()` for
`contentSizeHint`), enforced by an additive image case in
`tests/test_perf_gui_thread_io.cpp`; (2) an honest "Loading image…" placeholder
painted immediately, sized from the header hint; (3) off-GUI-thread decode
(`QtConcurrent::run` + `QFutureWatcher::finished`) that swaps the placeholder for
the real pixmap; (4) the view/zoom unit tests deterministically await the
placeholder→pixmap swap. The edit-correctness co-requirement holds for images by
the GUI-thread-only adoption argument above. This verdict does **not** add any
wall-clock/latency gate (consistent with the perf-measurement ruling).

Blast-radius note (residual accepted under B, image path): because the decode is
now deferred, the capability predicates (`supportsZoom` / `supportsEditing` /
`supportsSelectableText` / `supportsThumbnails` / `supportsSearch`) are keyed to a
still-image being **available or pending** (decoded, or a valid header read),
rather than to `m_image` already being non-null — so a control is never falsely
disabled during the brief decode window, and no capabilities-changed signal is
needed for images (unlike the PDF forms-toolbar refresh ADR 0006 required). A
reload or recovery arriving while the initial decode is in flight **supersedes** it
(a generation counter makes the stale `finished` callback a no-op), so the two
never race.

## Evidence required to reopen

For the **image** path (this verdict): a reproducible correctness defect
attributable to the off-thread decode — an edit or save observing a half-decoded
or wrong `m_image`, or a reload/open decode race that the generation guard fails to
supersede — with the failing case named. The **PDF** path is out of scope here and
reopens only under ADR 0006's own evidence bar.
