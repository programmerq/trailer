---
id: 2026-07-31-markup-toolbar-tool-visibility-reflow
title: Markup toolbar's tool buttons shift position on every document/tab switch
priority: TBD
status: open
source: docs/audit-2026-07-31-g10-deference.md, finding SC-CRIT-2
created: 2026-07-31
---

## Threshold

Switching the active document/tab between documents with different
`hasTextLayer()` / SAM-tool-eligibility states does not change the
on-screen `geometry()` of any `MarkupToolbar` action or widget that stays
visible across the switch (Redact, the Stroke/Fill buttons, the Width
spinner, the Dash combo). Checkable via a geometry-assertion UAT slot: open
two documents in tabs (one with a text layer, one without; or one SAM-tool
eligible, one not), record the toolbar's persistently-visible actions'
rects on each tab, switch between them, and assert the rects for actions
that were visible on both tabs are unchanged.

## Context

`MarkupToolbar::setToolVisible()` (`src/ui/MarkupToolbar.cpp:214-253`)
hides individual `QAction`s inside the toolbar's single row based on
document capability: `Underline`/`Highlight`/`StrikeOut` are shown only
when `hasTextLayer()` is true (`MainWindow.cpp:4019-4022`), and `Instant
Alpha`/`Smart Lasso` are shown only for eligible image documents with
available/download-permitted SAM models (`MainWindow.cpp:4029-4042`). Both
call sites run from `onCurrentDocumentChanged()` — every document/tab
switch. A `QToolBar` packs its actions into one layout, so hiding an action
collapses its slot and shifts every subsequent action left: the `Redact`
button, the SAM-tools separator, `Instant Alpha`/`Smart Lasso`, and the
trailing `Stroke`/`Fill`/`Width`/`Dash` controls all move. A routine
multi-tab workflow — an OCR'd PDF alongside a scanned image with no text
layer — visibly shifts every markup control on every tab switch. G10
(`AGENTS.md`) — spatial constancy.

Not covered by `claude/toolbar-reserved-positions`: that branch's own UAT
additions (`docs/uat/06-cross-cutting.md` UAT-XCT-074/075/076/077) are
scoped to toolbar-*row* position (`insertToolBar`/`insertToolBarBreak`
ordering, and the main toolbar's action geometry against a *sibling*
toolbar's visibility) — not to individual actions appearing/disappearing
*inside* the markup toolbar itself.

Suggested direction (a real trade-off, not prescriptive): either (a) keep
the action always present and switch to `setEnabled(false)` + a G3 tooltip
instead of `setVisible(false)` — trading the reflow for a small
always-there row of dimmed icons, which is a deliberate reversal of the
existing "hidden, not greyed" comment at `MainWindow.cpp:4026-4028` and
should be an explicit arbiter call, not a drive-by; or (b) reserve each
hideable action's slot the ADR-0007 way so hiding blanks the icon without
collapsing the layout.
