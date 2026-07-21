# 2026-07-19 — External file-change handling: watch, reload, and a save-time clobber guard

- **Status:** accepted <!-- proposed | accepted | superseded-by <YYYY-MM-DD-slug> -->
- **Arbiter:** the agent role named for this record; the owner (programmerq) is the escalation-only override.
- **Date proposed:** 2026-07-19
- **Date accepted / superseded:** 2026-07-19 (accepted)
- **Builds on / extends:** [`0004-never-worry-save-invariant.md`](0004-never-worry-save-invariant.md).
  ADR 0004 closed the *unsaved-edits-on-close* silent-loss gap. This record
  closes the adjacent gap it did **not** cover: the backing file changing *on
  disk* while a document is open — which, before this change, Trailer neither
  noticed nor defended against on save.

## Context

Trailer holds a document's bytes in memory after open and writes them back on
save. Nothing watched the file between those two points, and `save()` wrote
unconditionally. Three concrete gaps followed:

1. **No watcher.** If another program rewrote the file (a re-export, a `git
   checkout`, a sync client landing a new version), Trailer kept showing the
   stale in-memory copy with no indication the on-disk truth had moved.
2. **No mtime baseline.** Trailer had no record of the file identity it opened,
   so it could not tell "the file I loaded" from "a newer file at the same
   path."
3. **Silent clobber on save.** Because of (2), a `save()` overwrote whatever was
   on disk — including a *newer* copy written by someone else — with no
   conflict surfaced. This is the exact silent-data-loss shape ADR 0004 ruled
   inadmissible, but for the *other party's* edits rather than the user's own
   unsaved ones. ADR 0004's floor ("no silent data loss, ever") logically
   extends here, but its implementation (the dirty-close prompt) did not cover
   this path.

The never-worry-save model (PHILOSOPHY → *Never worry about saving*) and the
no-narration-dialogs taste rule (PHILOSOPHY → *How Trailer reduces friction*)
pull in opposite directions for the *clean* case: reload silently vs. announce.
The owner-ratified resolution below picks silence when nothing is at risk and a
non-modal surface only when there is a genuine conflict.

## Decision

Watch the current document's backing file (and its parent directory), capture a
load-time mtime/size baseline, classify every observed change against that
baseline plus the dirty flag, and act per the matrix below. Add a **save-time
conflict guard** so no write can clobber an uncaused external change.

### Behaviour matrix

| On-disk event | Buffer clean | Buffer dirty (unsaved edits) |
|---|---|---|
| **Modified in place** (mtime/size differ from baseline) | **Silent reload** from disk — no dialog (Preview-style). | **Non-modal in-window banner**: [Reload (discard my edits)] / [Keep mine] / [Compare]. Never auto-resolved. |
| **Deleted** | Keep buffer + show deleted banner; **Save recreates** the file. | Keep buffer + show deleted banner; **Save recreates** the file. |
| **Renamed / atomically replaced** (temp-write + rename; the common editor save pattern) | Detected via the **parent-directory** watch (Qt drops the file watch when the inode changes on rename — we re-add it), then routed to **silent reload**. | Same detection, routed to the **conflict banner**. |
| **Our own save** (in-process write) | Watcher **muted** across the write; baseline **refreshed** to the just-written bytes on completion, so the resulting filesystem events classify as *NoChange*. | Same. |

`Reload` / silent-reload re-reads the document in place and drops the (empty, or
user-discarded) edit state. `Keep mine` is a **force-save** that clobbers the
newer on-disk copy on purpose and refreshes the baseline (a one-shot flag,
consumed by exactly one save attempt). **(Refined 2026-07-20:** the banner's
Keep-mine no longer writes on click — it keeps the buffer, refreshes the
baseline, and defers the overwrite to the user's next explicit Save. See
[`2026-07-20-conflict-banner-keep-mine-semantics.md`](2026-07-20-conflict-banner-keep-mine-semantics.md).
The adapter-level force-save mechanism described here is unchanged.)**
`Compare` is a **G3-honest
disabled-with-tooltip placeholder** — no side-by-side diff view exists yet, so
the control is present-but-inert with a tooltip explaining why and where to go,
never a lying control.

### Three owner-ratified choices (verbatim)

1. **Clean-doc reload is SILENT.** When the buffer is clean there is nothing to
   lose, so the new content is loaded with **no dialog and no banner** — this
   matches the no-narration-dialogs taste rule; a dialog here would be pure
   interruption with no decision to make. **"Silent" is defined precisely as:
   no modal dialog and no persistent banner.** A subtle, non-modal status-line
   note (the current implementation flashes *"Reloaded — the file changed on
   disk."*) is still emitted and is **compliant** — a transient status note is
   neither an interrupting dialog nor a persistent attention surface, and it
   keeps the reload from being a jarring unexplained content swap. (N1: this
   makes the record and the code agree that the status flash is intended, not a
   silence violation.)
2. **The save-time mtime conflict guard is IN scope.** The must-have deliverable
   is that a save re-stats the file's mtime/size against the load-time baseline
   *before committing the write*, and refuses to clobber a changed-under-us file
   unless the user explicitly forces it (Keep mine). This is the piece that
   closes the silent-clobber hole; it is not deferred.
3. **PDFs KEEP their open handles.** Trailer does not drop the PDF's open file
   handle to achieve uniform live-reload behaviour across platforms.
   Loss-prevention is instead made uniform through the *save-time guard* (choice
   2), which is platform-independent. The consequence is an **accepted,
   documented divergence** on Windows: because the open PDF handle holds an
   exclusive share mode (see OS caveats), an external writer is *blocked* from
   overwriting the open PDF in place, so the live-reload path is effectively
   unreachable there while the file is open — whereas on macOS/Linux the
   external overwrite succeeds and the reload/banner path runs. This divergence
   is accepted: the guard guarantees no *Trailer-caused* loss on every platform,
   and Windows' stricter sharing only makes external clobbering *harder*, never
   easier.

## OS caveats

- **Windows — exclusive share mode.** A file opened without
  `FILE_SHARE_WRITE`/`FILE_SHARE_DELETE` cannot be overwritten or deleted by
  another process while the handle is open; `CreateFile` sharing flags govern
  this
  (https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilea).
  This is why keeping the PDF handle (choice 3) blocks external in-place
  overwrites on Windows and produces the accepted live-reload divergence above.
  (It is also why Trailer's *own* same-file save tears down its handles before
  the rename — see the Windows notes in AGENTS.md and `saveCommitOnUi`.)
- **macOS / Linux — advisory locks only.** POSIX file locking is advisory:
  nothing stops another writer from replacing the file, so the watcher +
  save-guard are the only real defense. There is **no Qt equivalent of macOS's
  `NSFilePresenter` / `NSFileCoordinator`** file-coordination protocol
  (https://developer.apple.com/library/archive/documentation/FileManagement/Conceptual/FileSystemProgrammingGuide/FileCoordinators/FileCoordinators.html),
  so Trailer cannot participate in coordinated writes; it observes after the
  fact via the watcher.
- **`QFileSystemWatcher` drops the watch on atomic rename.** Qt's watcher stops
  reporting a path once the file it referred to is replaced by rename (the inode
  changes), and repeated adds/removes are subject to a per-platform limit
  (https://doc.qt.io/qt-6/qfilesystemwatcher.html). The mitigation is to **watch
  the parent directory** as well and **re-arm the file watch** when the file
  reappears — which is exactly the atomic-replace (temp + rename) editor save
  pattern.
- **Linux — inotify semantics.** The underlying `inotify` delivers a burst of
  events for a single logical change (e.g. `IN_MODIFY` several times, or
  `IN_MOVED_FROM`/`IN_MOVED_TO` for a rename)
  (https://man7.org/linux/man-pages/man7/inotify.7.html), which is why the
  monitor **debounces** a burst into one classification rather than reloading
  per raw event.

## Checkable threshold this record establishes (G1)

All observable pass/fail, provable headlessly (proven by the tests listed under
Evidence):

1. **No silent clobber.** An external overwrite of a **dirty** document blocks
   the save and surfaces the conflict banner; **no on-disk bytes are
   overwritten** until the user picks Reload or Keep mine.
   (`test_external_change_guard::saveBlockedWhenFileChangedExternally` asserts
   `save()==false` and the on-disk bytes are byte-for-byte unchanged.) For the
   PDF two-phase save the re-stat runs **twice** — once in the worker-thread
   begin phase and again on the UI thread in `saveCommitOnUi`, immediately
   before the destructive remove+rename — so an external write that lands in
   the multi-second gap between the two phases still aborts the commit with the
   on-disk bytes intact
   (`test_external_change_guard::pdfCommitGuardBlocksMidFlightExternalChange`;
   the forced-clobber counterpart is `pdfCommitGuardForceClobbersMidFlightExternalChange`).
2. **Clean reload is silent and correct.** A **clean** document whose file
   changes on disk shows the **new content** with **no banner**.
   (`test_uat_external_change::uat_ext_002_cleanChangeReloadsSilently`:
   `imagePixelSize()` becomes the new size, banner stays `Hidden`.)
3. **Deleted keeps the buffer; Save recreates.** When the file is deleted the
   buffer is retained, the deleted banner shows, and Save writes the file back
   out. (`uat_ext_003_deletedShowsBannerAndKeepsBuffer`.)
4. **Keep mine is a one-shot force-save.** Forcing the save clobbers on purpose
   and refreshes the baseline so a following clean save succeeds; the force flag
   does not disarm the guard for later saves.
   (`forceSaveClobbersAndRefreshesBaseline`, `forceFlagIsOneShot`.)
5. **The classifier is exhaustive and pure.** `classifyExternalChange` returns
   exactly one of `{NoChange, CleanExternalChange, DirtyConflict, Deleted}` for
   every baseline/exists/dirty combination. (`test_external_change_state`.)

## Magic constants (G6)

- **`kDebounceMs = 250`** at
  [`src/document/ExternalChangeMonitor.cpp:19`](../../src/document/ExternalChangeMonitor.cpp).
  The window that collapses an inotify/`QFileSystemWatcher` burst (or a
  truncate-then-append / temp-then-rename save) into a single classification.
  Carries its in-code rationale comment (what it represents, the 100–500ms range
  considered, and the symptom to change it — double reloads if too low, laggy
  notice if too high), as required by PHILOSOPHY → *Hand-tuned values stay
  hand-tuned*. This is a pure internal-tuning value: it changes no user-visible
  default (a reload still happens; only how a burst is coalesced), so per G6 it
  needs the in-code comment, which it has, and this citation — not a separate
  behaviour ADR beyond this record.

## Compare-button status (honest)

**Placeholder — disabled with tooltip (G3).** The Compare button is present in
the conflict banner but `setEnabled(false)` with the tooltip: *"Comparing the
two versions isn't available yet — a side-by-side diff view is not built. Use
Reload to take the on-disk copy or Keep mine to overwrite it."*
(`src/ui/FileChangeBanner.cpp`). It exists so the option is discoverable once a
diff view lands; it never silently substitutes a different behaviour.

## Known limitations

**Background tabs share one monitor.** A single monitor + banner track the
**current** document. A change to a background tab's file is not shown while
that tab is hidden. When the user switches **into** that tab, the now-current
document is **re-classified immediately** against its baseline (see
`MainWindow::retargetExternalChangeMonitor`, which re-points the watcher and
then runs the classify-and-act path), so a pending conflict surfaces — banner
for a dirty conflict, silent reload for a clean change — on return rather than
waiting for the next filesystem event. The save-guard still fires on any save
attempt regardless of which tab is foreground, so no data-loss path results
either way. (F4: the re-classify-on-return step is what makes this claim
truthful; without it the conflict would not appear until the next raw FS
event.)

**Same-size + same-second overwrite is undetectable.** Change detection is
mtime + size only (`classifyExternalChange`,
[`src/document/ExternalChangeState.cpp:34`](../../src/document/ExternalChangeState.cpp));
there is no content hash. On a filesystem with 1-second mtime granularity, an
external overwrite that lands in the **same wall-clock second** as the
load-time baseline **and** yields a file of the **exact same byte size** is
classified `NoChange` and slips past both the watcher and the save-time guard.
This is an **accepted default**: mtime + size is a cheap, allocation-free stat
that catches every realistic external edit (content edits almost always change
the size, and edits more than a second apart change the mtime), whereas hashing
every file on every save would tax the large-PDF path against the size envelope
in [`docs/performance-budgets.md`](../performance-budgets.md) for a vanishingly
rare collision. The residual hole is tracked for an **optional content-hash
fallback** (consulted only when mtime + size are equal) in backlog item
`2026-07-19-external-change-same-size-blind-spot`. (F3.)

## Evidence

- Unit: `tests/test_external_change_state.cpp` (pure classifier + `FileBaseline`
  stat), `tests/test_external_change_monitor.cpp` (debounce / mute / typed-emit
  / re-arm), `tests/test_external_change_guard.cpp` (adapter-level save-guard on
  a real `ImageDocument`, plus the PDF two-phase begin→commit guard on a real
  `PdfDocument`), `tests/test_file_change_banner.cpp` (banner modes +
  G3 disabled-Compare). All green under `ctest -LE uat`.
- UAT: `tests/uat/test_uat_external_change.cpp` (`UAT-EXT-001..004`), labelled
  `uat`. Emits the G2 before/after evidence pair under
  `docs/uat/images/`.

## Evidence required to reopen

A documented path where a save clobbers an externally-changed file without the
user forcing it; a spurious reload/banner caused by Trailer's own write; or a
usability finding that the silent clean-reload surprises a concrete user at a
concrete step, plus owner sign-off.
