# 0008 — Staged document open: what runs synchronously, what moves off-thread, and how a large file is staged

- **Status:** proposed
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-15
- **Date accepted / superseded:** —

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

The user-facing behaviour this produces is the P0 in
`docs/backlog/2026-07-13-startup-hang-large-pdf.md`: opening a 142 MB text-layer
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
(`.../2026-07-13-startup-hang-large-pdf.md:73-78`).

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
  admissible with an honest, unmistakable placeholder that resolves into page 1.
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

Empty while status is `proposed` — the implementing session runs the persona/arbiter
cycle.

## Evidence required to reopen

N/A until accepted.
