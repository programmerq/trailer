---
id: 2026-07-31-inspector-dock-title-names-itself
title: Inspector dock is titled "Inspector" — chrome naming itself instead of its content
priority: TBD
status: open
source: docs/audit-2026-07-31-g10-deference.md, finding DEF-MOD-1
created: 2026-07-31
---

## Threshold

The Inspector dock widget's title bar does not read the literal word
"Inspector" while an annotation is selected and shown in it — it either
names what it currently displays (e.g. the selected annotation's type), or
drops the redundant title text the dock frame already visually separates
from the document, consistent with whatever fix lands for the identical
Sidebar issue. Checkable pass/fail: open the Inspector on a selected
annotation and read its dock title bar; it must not be the bare word
"Inspector" with no content-specific information.

## Context

`Inspector::Inspector(QWidget *parent) : QDockWidget(tr("Inspector"),
parent)` (`src/ui/Inspector.cpp:82`). Opened on demand (hidden at
construction, `MainWindow.cpp:372`; shown when the user selects an
annotation, `:377-380`), its title bar reads "Inspector" — the same defect
`docs/ux-guidelines.md` already names for the Sidebar ("A sidebar labelled
'Sidebar.' A label that describes the chrome instead of what it contains is
the chrome announcing itself.") G10 (`AGENTS.md`) — deference.

This is the same underlying bug as the known in-flight Sidebar title fix
(`Sidebar.cpp:255`, tracked on `claude/ui-deference-polish`) but on a
sibling widget the coordinator's brief did not name. Filed separately
because there is a real risk the in-flight PR's diff is scoped narrowly to
the reported Sidebar instance and this sibling survives untouched; if that
PR's fix turns out to be a shared helper/pattern that already covers both,
close this item by reference to that PR instead of a separate diff.
