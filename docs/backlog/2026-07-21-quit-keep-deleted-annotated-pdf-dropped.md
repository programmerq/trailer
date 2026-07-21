---
id: 2026-07-21-quit-keep-deleted-annotated-pdf-dropped
title: ⌥⌘Q "Quit and Keep Windows" silently drops a dirty-annotated PDF whose backing file was deleted underneath
priority: TBD
status: open
source: correctness review of PR #96 (CF-6/CF-7 quit-integration branch), 2026-07-21
created: 2026-07-21
---

## Threshold

Under ⌥⌘Q ("Quit and Keep Windows") with a **dirty-annotated PDF whose backing
file was deleted underneath**, on `restoreKeptWindows` the document — with its
unsaved annotations — is **preserved**, OR the doc is **prompted** (Save/Keep)
before quit. No annotated doc is silently dropped.

Checkable: a headless test drives `requestQuit(KeepWindows)` +
`restoreKeptWindows` for a PDF that (a) has unsaved annotations and (b) whose
backing file was removed from disk between capture and restore; the test asserts
the restored session still contains that document (annotations intact) — or that
a save/keep prompt was raised before quit. It must **fail** against today's
behaviour, where the document is dropped with no prompt.

## Context

On ⌥⌘Q, a PDF with unsaved annotations whose backing file was deleted underneath
is **not prompted**: it is draftable via `canDraftForKeep`, but the keep path in
`Application.cpp` (~469) gates on `isDirty()`, so it is captured as an
`AnnotatedPath` referencing the now-deleted file (`Application.cpp` ~522). On
`restoreKeptWindows`, the guard

```cpp
if (dd.path.isEmpty() || !QFileInfo::exists(dd.path)) continue;
```

(`Application.cpp` ~606) drops the whole document — annotations and all — with no
prompt. This is a silent-loss window for the dirty-annotated-deleted-PDF case,
violating the never-worry-save invariant (ADR-0004 spirit in
[`PHILOSOPHY.md`](../../PHILOSOPHY.md); see also
[`docs/decision-records/2026-07-19-external-file-change-handling.md`](../decision-records/2026-07-19-external-file-change-handling.md)).

**Scope note — pre-existing, not introduced by the current branch.** This defect
lives on `origin/main` in the #78 keep/restore path. PR #96 (the CF-6/CF-7
branch) fixed the analogous **clean-deleted image** case — a clean-deleted image
is now stored with its raster rather than as a dangling path reference — but
deliberately did **not** expand scope to this pre-existing PDF path. Filed here
so the annotated-PDF hole is tracked to closure independently.

Related item:
[`2026-07-20-deleted-file-clean-doc-silent-loss`](2026-07-20-deleted-file-clean-doc-silent-loss.md)
(the clean-doc close path; distinct from this quit-and-keep restore path).
