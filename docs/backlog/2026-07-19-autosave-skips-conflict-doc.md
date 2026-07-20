---
id: 2026-07-19-autosave-skips-conflict-doc
title: Auto-save silently skips a conflicting doc yet still flashes "Auto-saved."
priority: P3
status: open
source: code review of claude/external-file-change-handling, finding F7, 2026-07-19
created: 2026-07-19
---

## Threshold

When an auto-save pass encounters a dirty document whose backing file changed
externally (the adapter guard refuses the clobber and `save()` returns false),
the outcome is observably distinguishable from a clean success: the pass either
produces a **subdued, conflict-specific notice** (not a bare "Auto-saved.") or
**routes the conflicting doc to the file-change banner**. Checkable: with one
dirty-conflict doc and no other dirty docs, an auto-save tick must NOT flash the
plain "Auto-saved." success string as if nothing were wrong.

## Context

`MainWindow::autoSaveDirtyDocs`
([`src/ui/MainWindow.cpp` ~722–747](../../src/ui/MainWindow.cpp)) mutes the
monitor, loops over dirty docs, and calls `doc->save()`. A doc whose file
changed under us is correctly **not** clobbered — the adapter's
`saveWouldClobberExternalChange` guard makes `save()` return false — but the
loop only tracks `savedAny` and, if any *other* doc saved, flashes
"Auto-saved." The skipped conflicting doc gets no signal at all, so the user
believes everything was saved while a genuine conflict sits unresolved.

The save-guard means there is **no data-loss path** here (the ADR backstop
holds); this is a **notification-honesty** gap, not a correctness one. Doing
this item means: detect the skipped-because-conflict case inside the pass and
surface it — a subdued status note naming the doc, or raising the conflict
banner for the current doc if it is the one that conflicted — instead of
folding it silently under a success flash. Relates to the "no lying controls /
honest surfaces" spirit in [`PHILOSOPHY.md`](../../PHILOSOPHY.md) and the
external-change ADR
([`docs/decision-records/2026-07-19-external-file-change-handling.md`](../decision-records/2026-07-19-external-file-change-handling.md)).
