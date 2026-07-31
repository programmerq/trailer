# Sidebar/Inspector dock title bars carry no visible caption; accessible names preserved via windowTitle(), not accessibleName()

- **Status:** accepted <!-- proposed | accepted | superseded-by <YYYY-MM-DD-slug> -->
- **Arbiter:** the agent role named for this record (session
  `claude/ui-deference-polish`); the owner (programmerq) is the
  escalation-only override.
- **Date proposed:** 2026-07-31
- **Date accepted / superseded:** 2026-07-31 (owner directive, relayed via
  the session brief — see Context)

## Context

Owner directive (verbatim, relayed via the coordinator brief for this PR):

> "We don't need to label the sidebar 'sidebar'."

A concurrent G10 audit (PR #134, open at the time of writing, not yet
merged) independently filed the identical defect for the sibling panel —
`docs/backlog/2026-07-31-inspector-dock-title-names-itself.md` on that
branch — noting the risk that fixing only the reported instance leaves its
twin (`Inspector.cpp:82`) unaddressed. This record and its implementing PR
cover both, closing that backlog item's substance.

**What ships today on `main` (before this record):** `Sidebar` and
`Inspector` are both `QDockWidget` subclasses constructed with a caption —
`QDockWidget(tr("Sidebar"), parent)` and `QDockWidget(tr("Inspector"),
parent)` respectively — which Qt's native title-bar chrome renders as
visible text above each panel's contents. `docs/ux-guidelines.md` names
"a sidebar labelled 'Sidebar'" as one of the motivating anti-patterns for
gate G10 (deference): a label that describes the chrome instead of what it
contains is the chrome announcing itself.

**The obvious-looking fix, and why it's wrong:** blank `windowTitle()`
(`QDockWidget(QString(), parent)`) and call `setAccessibleName(tr(...))`
instead, on the assumption that `accessibleName()` is the fallback
screen-reader name once the visible caption is empty — the pattern that
works for most Qt widgets (`QPushButton`, `QGroupBox`, ...). **Verified
empirically, not assumed:** a standalone probe against this Qt build
(`QAccessible::queryAccessibleInterface(dock)->text(QAccessible::Name)`)
shows a `QDockWidget`'s built-in accessibility interface reads
`windowTitle()` directly and does **not** consult `accessibleName()` at
all — unlike `QPushButton`/`QGroupBox`, which the same probe confirms DO
prefer `accessibleName()` when set. Blanking `windowTitle()` therefore
silently blanks the accessible name too, regardless of any
`setAccessibleName()` call alongside it — precisely the "cleanup that
quietly breaks screen-reader users" trap the brief warned against.

## Options

- **A. Blank `windowTitle()` + `setAccessibleName()`.** The obvious fix.
  Rejected per the empirical finding above: silently produces an EMPTY
  accessible name on this Qt build, a real accessibility regression
  wearing a fix's clothes.
- **B. Leave `windowTitle()` populated (so the native accessibility path
  stays correct); install a custom, textless title-bar widget
  (`setTitleBarWidget`) that repaints only a close button (+ float button
  where the dock supports floating), no caption text.** What ships.
  `setAccessibleName()` is ALSO set, as defence-in-depth for any AT bridge
  that reads the property directly rather than through Qt's built-in
  interface — cheap insurance, not the load-bearing fix.
- **C. Do nothing; leave the caption visible.** Rejected: contradicts the
  owner's explicit directive and the accepted G10 gate's own motivating
  example.

## Personas debate

- **Office non-technical user:** Never reads a dock's title bar as
  information — the panel's own contents (page thumbnails, or the
  document/annotation properties) already say what it is. Favours B; A
  and B look identical to this user (the difference is invisible without
  a screen reader), so this persona doesn't discriminate between them.
- **Older careful user:** No stake in the visual change; would be
  seriously harmed by A if they rely on a screen reader — an accessible
  name that silently vanishes is a worse failure mode than never having a
  label at all, because it looks fixed in code review. Decisive for B over
  A.
- **Power migrator:** Notices the reduced chrome as a small, welcome
  decluttering; expects panel identity to still be discoverable via
  right-click / View menu regardless of the caption (unaffected either
  way — the View-menu toggle actions set their own text explicitly,
  independent of the dock's `windowTitle()`).
- **Occasional user:** No stake either way visually; same accessibility
  exposure as the older careful user if using AT.

## Admissible objections

- **Older careful / any AT user, Option A, "screen reader announces the
  panel" step:** the accessible name is silently empty — verified, not
  hypothetical. Decisive against A.
- **Any user, Option B, "drag the dock to float / undock" step (Inspector
  only — Sidebar does not carry `DockWidgetFloatable`):** a custom title
  bar widget could in principle swallow the mouse events Qt uses for
  drag-to-float. Resolution: the custom widget
  (`buildTextlessDockTitleBar`, `src/ui/IconHelper.cpp`) is a plain
  `QWidget` with a stretch and only the button rects consuming clicks —
  the well-established Qt pattern for custom title bars (drag/float
  handling is installed on the title-bar AREA regardless of custom vs.
  native content, as long as the custom widget doesn't intercept the
  background). Not a blocking objection; noted for anyone touching this
  code later.
- **Any user, Option B, "click the close button" step:** functional
  parity with the native close (✕) button is required, not just its
  removal. Resolution: the custom title bar's close button is wired to
  `QDockWidget::close()`, the same effective action; regression-guarded by
  `uat_fnd_019_dockPanelsHaveNoVisibleCaptionButKeepAccessibleName`
  (`tests/uat/test_uat_foundations.cpp`), which locates the button by its
  "Close" tooltip (not position) and asserts clicking it hides the dock.

### Rejected as naked preference

- None raised — every alternative to B was either the owner's directive
  itself (removing the caption) or a concrete, checkable accessibility
  failure (A), not taste.

## Checkable threshold this record establishes

- **No visible caption text.** The custom title bar
  (`dock->titleBarWidget()`) contains no `QLabel` — the only widget type
  that would paint the panel's name.
- **Accessible name preserved, verified via the real interface, not just
  the property.** `QAccessible::queryAccessibleInterface(dock)->text(
  QAccessible::Name)` equals `"Sidebar"` / `"Inspector"` respectively —
  not merely `dock->accessibleName()`, which (per the empirical finding)
  is NOT what a `QDockWidget`'s built-in interface actually reads.
- **Close affordance functional parity.** Clicking the title bar's close
  button (identified by its "Close" tooltip) hides the dock, matching the
  native title bar's ✕ button.
- **No position shift (G10 spatial constancy).** The custom title bar
  occupies the same title-bar STRIP the native one did (same
  `DockWidgetMovable`/`DockWidgetClosable` features, comparable height) —
  the dock's content (`m_stack` for Sidebar, `m_tabs` for Inspector) does
  not move as an unrelated-state side effect of this change.

All four automated by
`uat_fnd_019_dockPanelsHaveNoVisibleCaptionButKeepAccessibleName`
(`tests/uat/test_uat_foundations.cpp`).

## Arbiter verdict + rationale

**Option B is adopted.** The owner's directive settles that the caption
must go; the empirical accessibility finding rules out the obvious
implementation (A) on concrete, checkable grounds, not taste. B is the
only option that satisfies both constraints simultaneously: `windowTitle()`
stays exactly what it was (so the ALREADY-CORRECT native accessibility
path is untouched — the most robust possible preservation, since it
doesn't depend on this record's author having correctly guessed every AT
bridge's lookup order) while the PAINTED layer is replaced with a
minimal, textless bar. The shared helper
(`buildTextlessDockTitleBar`, `src/ui/IconHelper.h`/`.cpp`) is used by
BOTH `Sidebar` and `Inspector` so the fix can't drift between the two
panels, and automatically adapts to each dock's actual feature set (only
Inspector gets a float button, matching its `DockWidgetFloatable`
default that `Sidebar` deliberately opts out of).

## Evidence required to reopen

A concrete, checkable accessibility regression in a REAL AT client (not
just this Qt build's `QAccessible` probe) — e.g. a screen reader that
reads the custom title bar's buttons but not the panel name via some other
path this record didn't test — plus owner sign-off. A future contributor
wanting the visible caption back for some OTHER reason (e.g. disambiguating
a third, visually-similar dock) is a new, separate decision, not a
reopening of this one.
