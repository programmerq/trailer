---
id: 2026-07-19-external-change-parent-dir-removed
title: rearmFileWatch re-adds only the file, not the parent dir if the dir is deleted/renamed
priority: P3
status: open
source: code review of claude/external-file-change-handling, finding F8b, 2026-07-19
created: 2026-07-19
---

## Threshold

Deleting or renaming an open document's **containing directory** (and later a
sibling path reappearing, e.g. dir recreated or the file restored) still
surfaces the change: the monitor re-arms its parent-directory watch, not only
the file watch, so a subsequent event is caught. Checkable headlessly via
`ExternalChangeMonitor`: after the watched file's parent directory is
removed/renamed and then re-created with the file present again, the monitor
emits `externalChange()`/`fileDeleted()` on the next modification rather than
going permanently deaf.

## Context

`ExternalChangeMonitor::rearmFileWatch`
([`src/document/ExternalChangeMonitor.cpp` ~62–70](../../src/document/ExternalChangeMonitor.cpp))
re-adds only `m_path` (the file) when it reappears:

```cpp
if (QFileInfo::exists(m_path) && !m_watcher->files().contains(m_path))
    m_watcher->addPath(m_path);
```

`QFileSystemWatcher` also drops a **directory** watch when the directory itself
is removed or renamed (the inode changes), just as it drops a file watch on
atomic file replace. `setPath` adds the parent dir once, but nothing re-adds it
after the dir is deleted/renamed — so if the directory is swapped out from
under an open document, both watches can go silent and later in-place edits are
missed. No data-loss path results (the save-time guard is the backstop
regardless), so this is a detection-robustness gap.

Doing this item means: extend `rearmFileWatch` (or `onFilesystemEvent`) to also
re-add `m_dir` when it exists but is no longer in `m_watcher->directories()`,
mirroring the file re-arm. Note the per-platform add/remove limits called out
in the ADR OS caveats
([`docs/decision-records/2026-07-19-external-file-change-handling.md`](../decision-records/2026-07-19-external-file-change-handling.md)).
