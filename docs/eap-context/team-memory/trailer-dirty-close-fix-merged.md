---
name: trailer-dirty-close-fix-merged
description: P0 UAT-FND-014 silent-discard-on-close FIXED + MERGED via PR #47 (fix/dirty-close-prompt → main) 2026-07-12, commit f0f76ad; veto-signal + confirmCloseDirtyDoc, 6 regression cases, ADR 0004 accepted
metadata:
  type: project
---

# P0 dirty-close silent-discard — FIXED + MERGED

The docket P0 (see [[trailer-followup-docket]]) — "closing the last tab of a dirty document silently discards edits (UAT-FND-014)" — is **FIXED and MERGED**.

- **PR #47**, branch `fix/dirty-close-prompt`, merged to `main` on **2026-07-12**.
- Merge/head commit **f0f76ad2e2fcff10179c080e7e8a8529d0e868e2** (`f0f76ad`).

## What was broken

`DocumentView::onTabCloseRequested` erased the document before `allTabsClosed` fired, so `closeEvent`'s dirty-check saw zero docs and never prompted. Edits were silently discarded. Violated the never-worry-save no-silent-loss invariant (see [[trailer-requirements-summary]]).

## The fix — design

- New **synchronous veto signal** `DocumentView::documentCloseRequested`, wired to a reusable **`MainWindow::confirmCloseDirtyDoc`** slot **extracted from `closeEvent`** (the window-close dirty-prompt logic is now shared, not duplicated).
- The tab-close path emits the veto signal *before* erasing the doc; a Cancel restores the tab/doc, Discard closes without saving, Save persists first.

## Two paths now covered

Both were silently discarded before this fix (the window-close path already prompted):

1. **close-last-dirty-tab**
2. **close-non-last-dirty-tab**

## Regression coverage

Six UAT-FND-014 automated regression cases added in `tests/uat/test_uat_foundations.cpp`:

1. Cancel restores the document
2. Discard closes without saving
3. Save on a titled document
4. Save on an untitled document via Save-As
5. non-last-tab close prompts
6. clean document closes with no prompt

Result: **full suite 47/47 green, uat 15/15**.

## ADR

`docs/decision-records/0004-never-worry-save-invariant.md` flipped **proposed → accepted (2026-07-12)** via the persona/arbiter cycle. This satisfies the never-worry-save no-silent-loss invariant.

## Related

- [[trailer-followup-docket]] — this closed the top-ranked (P0) follow-up.
- [[trailer-requirements-summary]] — never-worry-save invariant this satisfies.
- [[trailer-integration-batch-pr40]] — prior integration batch context (PR #40 on main); this fix lands on top of that main line.
