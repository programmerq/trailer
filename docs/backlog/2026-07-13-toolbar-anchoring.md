---
id: 2026-07-13-toolbar-anchoring
title: Form toolbar shoves the main toolbar rightward; main toolbar must be permanently top-left and overflow chevron fixed-size
priority: P2
status: open
source: v0.3.0 real-Mac dogfood report (2026-07-13)
created: 2026-07-13
---

## Threshold

The main toolbar is permanently anchored top-left on its own row; the form
toolbar anchors right near the search field; narrow windows collapse form
buttons into the existing "show more" extension chevron; the chevron stays a
fixed size when toggled.

Declared pass/fail (G2 evidence via offscreen `QWidget::grab()` per AGENTS.md
G2, both toolbar states):

- Toggling the form toolbar on/off leaves the main toolbar's top-left origin
  pixel **stable**.
- The form toolbar's buttons are right-aligned near the search field.
- Shrinking the window collapses form buttons into the extension chevron.
- Clicking the chevron does not change its own bounding rect and does not move
  adjacent widgets.

Verified: `grab()` of the form-hidden and form-shown states shows the main
toolbar origin unchanged; `grab()` at a narrow width shows overflow into the
chevron; the chevron's rect is identical before/after toggle. `[real-Mac]`
confirmation of the pixel behaviour is a bonus but not required — `grab()`
suffices per the ux-evidence ruling for non-native-chrome layout.

## Context

Owner dogfood report: opening the form toolbar pushes the main toolbar to the
right instead of the main toolbar staying anchored top-left. Rule: main toolbar
permanently top-left on its own row; form toolbar anchors right near the search
toolbar; narrow windows overflow into the existing "show more" pattern; the
markup toolbar's second-row behaviour is the correct reference.

Root cause — insertion order + break placement put the main toolbar on the same
row as the form toolbar, to its right:
- Markup toolbar docked at `src/ui/MainWindow.cpp:275-276`.
- Form toolbar docked + break at `:335-341`
  (`addToolBar` `:336`, `insertToolBarBreak(m_formToolbar)` `:341`).
- Main toolbar built at `buildMainToolbar()` `:710`, docked at `:732`, break
  `insertToolBarBreak(m_markupToolbar)` at `:733`.

Top-area append order is `markup, form, main`; breaks sit before `markup`
(`:733`) and before `form` (`:341`). Walking the layout: row 1 = `markup`;
row 2 = `form` then `main` (no break before `main`, so it shares the form's
row). With markup+form hidden by default, `main` renders alone at top-left;
**showing the form toolbar inserts `form` to the left of `main`, pushing `main`
rightward** — the reported displacement. `main` was added last and never given
its own row/break, so it is effectively a tenant on the form's row. The markup
toolbar is correct because it owns row 1 alone.

The "show more" overflow is Qt's built-in `QToolBarExtension` chevron
(objectName `qt_toolbar_ext_button`, referenced in a comment at
`src/ui/MarkupToolbar.cpp:26-27`). It has **no** custom size policy or
stylesheet anywhere, so its size hint tracks its arrow/checked state and the
toolbar's `toolButtonStyle`/`iconSize` — toggling it reflows neighbours.

Fix direction: (1) give `main` the top row by itself (build/insert it first at
the front of the top area) and place a break before each of markup and form so
neither shares main's row — replicate the `:733` wiring for form. (2) Add a
leading expanding spacer at the front of the form toolbar (mirror the main
toolbar spacer at `MainWindow.cpp:807-809`) so its buttons sit right. (3) Pin
the extension button size via
`findChild<QToolButton*>("qt_toolbar_ext_button")->setFixedSize(...)` (or a fixed
min/max-width stylesheet) on all three toolbars. The toolbar-anchoring research
theme in `docs/research/2026-07-13-ux-research-agenda.md` feeds the
multi-toolbar-layout / overflow-affordance decision.

## Provenance

v0.3.0 real-Mac dogfood report, 2026-07-13. Root-cause file:line refs from the
grounded investigation pass against `a4abbcf`.
