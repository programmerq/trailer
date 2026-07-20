---
id: 2026-07-20-deleted-file-clean-doc-silent-loss
title: File deleted on disk while a clean doc is open shows no "•" — possible silent buffer loss after Dismiss + Close
priority: TBD
status: open
source: UX-walkthrough driven-mode audit 2026-07-20 (Persona A worker A2-F5)
created: 2026-07-20
---

## Threshold

Two parts:

1. **Verify the risk (do this first).** With a *clean* doc open, delete the
   backing file, Dismiss the deleted-on-disk banner, then close the window.
   Observe whether the window closes without a save prompt — i.e. whether the
   in-memory buffer (now the only copy of the content) is discarded silently.
2. **If confirmed, close the hole.** When the backing file no longer exists, the
   open doc is treated as having unsaved content: closing it must go through the
   never-worry-save prompt (Save/Discard/Cancel), not close silently. Checkable:
   delete-underneath → Dismiss → Ctrl+W surfaces the unsaved-changes prompt.

Additionally (Sev 1 wording): the deleted banner reads "Your **edits** are still
open" even for a clean doc — reword to "your open copy" (or equivalent) so it does
not imply edits that don't exist.

## Context

When the backing file is deleted while the doc is **clean**, the deleted banner
fires correctly ("⚠ This file was deleted on disk. Your edits are still open —
Save to recreate it." with [Save] [Dismiss]) and the buffer is kept, but the
title shows **no "•"** dirty marker even though the buffer is now the only copy of
the content. If Dismiss→Close then closes a "clean" doc without a prompt, that is
a data-loss hole adjacent to the never-worry-save intent
([`docs/decision-records/2026-07-19-external-file-change-handling.md`](../decision-records/2026-07-19-external-file-change-handling.md)
and the ADR-0004 never-worry-save spirit in [`PHILOSOPHY.md`](../../PHILOSOPHY.md)).

The audit did **not** drive Dismiss→Close, so part 1 (verification) is required
before deciding severity — if confirmed this rises from Sev 2 to a Sev 3
data-loss defect. Distinct from the existing external-change edge items, which
cover detection/autosave-honesty, not the deleted-clean-doc close path.

## Provenance

Driven against real `build/trailer` (main `6aab23f`), Xvfb+xdotool, dpr=1;
Dismiss→Close path flagged but not driven. Evidence:
`menu-ext/ext-07-deleted-underneath-shows-banner.png` (no "•" in title). Curated
evidence to commit under `docs/uat/images/2026-07-20-deleted-file-no-dirty-marker.png`.
