---
name: trailer-undo-cap-desync
description: FIXED 2026-07-10 on fix/undo-stabilization @ 877f9fb (pushed to origin) — >64-edit cap desync, release no-op asserts, ImageDocument unification
metadata:
  type: project
---

# Trailer undo cap desync — FIXED, pushed to origin (awaiting batched PRs)

Original bug (confirmed 2026-07-09 at de62300): `AnnotationStore` capped history at kMaxUndo=64 but still emitted `historyPushed()` when evicting the oldest frame, while `PdfDocument` appended to `m_undoLog` unconditionally → after >64 annotation edits, lost undo + phantom redo (`canRedo()` stuck true); release builds compiled out the `Q_ASSERT`s guarding the log/stack sync → empty-vector `.back()` UB; ImageDocument never got the unified chronological log.

**Status 2026-07-10: all three issues FIXED on local branch `fix/undo-stabilization` @ `877f9fb` (5 commits), stacked on `chore/session-setup-hook` @ `03ac5c9` (3 commits), base `origin/main` @ `67da60f`. 39/39 ctest green after clean rebuild; two local review passes applied per [[trailer-review-before-push-policy]].**

Fix design (~3 lines):
- Store-owned depth with a new `historyEvicted()` signal so the chronological log stays in lockstep with the capped annotation history (no more unconditional append on eviction).
- `undo()`/`redo()` now return bool, with runtime guards on log/stack desync instead of release no-op `Q_ASSERT`s (no `.back()` UB).
- Cap raised 64→128 with an enforced `setMaxUndoDepth`; ImageDocument now mirrors PdfDocument's unified chronological log.

Pushed to origin 2026-07-10: chore/session-setup-hook @ 03ac5c9 and fix/undo-stabilization @ 877f9fb; awaiting batched-PR assembly.

[[trailer-remote-build-recipe]] [[trailer-review-before-push-policy]] [[trailer-inflight-work-persistence]] [[trailer-integration-batch-pr40]]
