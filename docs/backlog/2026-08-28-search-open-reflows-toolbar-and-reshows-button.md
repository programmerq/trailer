---
id: 2026-08-28-search-open-reflows-toolbar-and-reshows-button
title: Opening Find grows the main-toolbar row (every row-1 control shifts down) and the hidden search icon is re-shown beside the open field
priority: TBD
status: open
source: first run of the generalized constancy sweep (UAT-XCT-093, tests/uat/test_uat_constancy_sweep.cpp)
created: 2026-08-28
---

## Threshold

The `KnownDefect` tolerance entries referencing this item in
`tests/uat/test_uat_constancy_sweep.cpp` (toggle row `search-expand`) are
**deleted**, and

```sh
QT_QPA_PLATFORM=offscreen ./build/tests/uat/test_uat_constancy_sweep
```

passes with the strict oracle: opening Find moves no main-toolbar
control, and while the SearchBar is expanded the search icon button is
**not visible** (assert `searchButton->isVisible() == false` after
`showSearchBar()` + a toolbar relayout).

## Context

Two distinct defects surfaced by one sweep row (offscreen, 1100x750):

1. **Row height reflow.** The expanded `SearchBar` (a QLineEdit-bearing
   widget) is taller than the 18px icon buttons, so revealing it grows
   the main toolbar row and every row-1 control re-centres 3px lower:
   `sidebarModePicker`, all zoom/rotate/markup/form toggle buttons moved
   `(x,23) -> (x,26)`. Same class as this sweep's status-bar height
   finding (2026-08-28-status-bar-slot-height-not-reserved): the
   horizontal anchoring work (ADR 0007) never covered the vertical axis.

2. **The hidden search icon comes back.** `showSearchBar()` hides the
   icon with `m_searchButton->setVisible(false)` — the WIDGET only. The
   button was added via `QToolBar::addWidget()`, so it is controlled by
   a wrapping `QWidgetAction`, and QToolBarLayout re-imposes the
   action's (still-visible) state on the widget at the very relayout
   the expanding bar triggers. Measured: with the bar open,
   `searchButton` is visible at `(709,26)` — pushed left by exactly the
   bar's width, sitting redundantly beside the open field. The fix
   MainWindow already applies to the BAR itself (hide the wrapper
   action too — see the `m_searchBarAction` comment in
   `buildMainToolbar()`) was never applied to the button. Symmetric
   sites: `showSearchBar()` / `hideSearchBar()` and the R3 floor
   measurement block in `buildMainToolbar()` (which toggles the same
   pair and would inherit the same wrapper-action handling).

Both are user-visible layout changes to fix, so they take the usual
gate evidence; this item exists so the sweep can gate at release
meanwhile without burying the findings.
