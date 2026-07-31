---
id: 2026-07-31-status-bar-permanent-widget-reflow-chain
title: Status bar's six permanent widgets shift each other's position on independent visibility toggles
priority: TBD
status: open
source: docs/audit-2026-07-31-g10-deference.md, finding SC-CRIT-1
created: 2026-07-31
---

## Threshold

For every pair of the status bar's permanent widgets — `m_mlIndicator`,
`m_largeDocOcrHint`, `m_zoomIndicator` (or its post-fix replacement),
`m_readOnlyBadge`, `m_mlProgress`, `m_ocrModelMissingHint`
(`src/ui/MainWindow.cpp`) — toggling one widget's visibility does not change
the on-screen `geometry()` of any other widget in the row. Checkable via a
geometry-assertion UAT slot in the shape of `uat_xct_076_...` (ADR
`0007-toolbar-anchoring-and-overflow`'s own regression guard for the
equivalent toolbar problem): record each permanent widget's rect at a
baseline, toggle each widget's visibility trigger independently (ML
scheduler activity, large-doc-OCR-hint dismissal, Two-Pages view-mode
switch, OCR batch start/finish, auto-OCR-missing-model state), and assert
every *other* widget's rect is unchanged after each toggle.

## Context

`QStatusBar::addPermanentWidget()` packs its widgets into one left-to-right
box layout in call order; hiding one collapses its slot and shifts every
widget positioned after it. The status bar currently has six such widgets,
added in this order and each independently toggled: `m_mlIndicator`
(`MainWindow.cpp:516-520`, visibility at `:527-528`), `m_largeDocOcrHint`
(`:545-601`, visibility at `:594`, `:599`, `:4235`), `m_zoomIndicator`
(`:604-610`, visibility at `:3234`, `:3239` — separately tracked for removal
as *permanent* chrome by the `claude/image-open-zoom-window-size` branch,
which does not by itself fix this item's positional-coupling problem),
`m_readOnlyBadge` (`:621-629`, visibility at `:4085-4086`, `:4133-4134`,
driven purely by Two-Pages view-mode state), `m_mlProgress` (`:635-636`,
internal show/hide in `MlProgressWidget.cpp:44,68,83,110,126`), and
`m_ocrModelMissingHint` (`:716-743`, visibility at `:745`).

Concrete repro: switch the current document into Two Pages view mode while
an OCR batch is running — `m_readOnlyBadge` appears and `m_mlProgress` (a
widget hosting a Cancel button the user may be about to click) jumps right
by the badge's width, for a reason (view mode) unrelated to the ML work it
displays. G10 (`AGENTS.md`) — spatial constancy.

Suggested direction (not prescriptive — implementer's call): reserve each
permanent widget's footprint even when logically hidden (blank its content
but keep it in the layout), or group co-occurring widgets into one
container so hide/show happens inside a sub-layout that doesn't perturb
siblings — the same "reserve position, don't let visibility toggle it"
pattern `0007-toolbar-anchoring-and-overflow` already applied to the
toolbar rows.
