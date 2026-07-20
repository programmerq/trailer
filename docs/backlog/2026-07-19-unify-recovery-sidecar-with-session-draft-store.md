---
id: 2026-07-19-unify-recovery-sidecar-with-session-draft-store
title: Unify the auto-save RecoveryStore with #74's SessionDraftStore into one app-data persistence mechanism
priority: unranked
status: open
source: PR claude/discard-file-integrity (P0 write-side never-worry-save); coordination with PR #74 (adr/quit-and-keep-windows)
created: 2026-07-19
---

## Threshold

There is exactly **one** app-data store type responsible for persisting
unsaved document work, used by **both** the 30 s auto-save recovery path and
the macOS Quit-and-Keep-Windows restore path. Concretely, when both this PR and
PR #74 have landed:

- `src/document/RecoveryStore.*` and `src/settings/SessionDraftStore.*` are not
  two parallel stores writing overlapping app-data areas; one is removed or one
  is expressed in terms of the other (shared on-disk area, shared
  index/manifest, shared restore-on-launch/open entry point).
- The existing green tests of both paths still pass:
  `tests/test_discard_file_integrity.cpp`, `tests/test_recovery_store.cpp`,
  `tests/uat/test_uat_foundations.cpp`
  (`uat_fnd_030_autoSaveWritesRecoverySidecarNotBackingFile`), and #74's
  `tests/test_session_draft_store.cpp` / `tests/test_quit_and_keep_windows.cpp`.
- No behaviour regresses: auto-save still never writes the backing file, and
  Quit-and-Keep-Windows still restores unsaved/annotated docs.

Close this item in the PR that performs the unification, citing this id.

## Context / Body

This PR (the focused P0 fix for the write-side never-worry-save invariant, DR
`2026-07-19-autosave-recovery-sidecar`) introduces `RecoveryStore`: an
app-data (`QStandardPaths::AppDataLocation/autosave/`) index mapping each
backing file to a full-document recovery **sidecar** plus the source mtime, so
the 30 s auto-save tick can persist a crash-recovery snapshot **without ever
writing the user's file**, and reopen can silently restore a newer sidecar as a
dirty document.

Unmerged PR #74 (`adr/quit-and-keep-windows`) introduces `SessionDraftStore`:
an app-data manifest + blob store of `SessionDocDescriptor`s
(`Path` / `Draft` raw bytes / `AnnotatedPath` = on-disk file + JSON annotation
payload) for macOS "Quit and Keep Windows" restore-on-relaunch.

The two mechanisms have the **same intent** — persist unsaved work to app-data
and restore it later without touching the user's file — and materially
overlapping storage and restore semantics. They were built independently
because #74 is unmerged and unifying them is a larger refactor than the P0 fix
should carry (it would touch #74's not-yet-merged design and on-disk format).
Rather than block the P0 on that refactor, the two stores are shipped separately
and this item tracks folding them into one mechanism once both have landed —
most naturally by expressing the auto-save sidecar as a `SessionDraftStore`
entry (its `AnnotatedPath` descriptor already stores "backing file + unsaved
annotations", which is exactly what an annotation-only recovery snapshot needs),
or vice versa. Cross-referenced from the PR body so #74's reviewer sees it.
