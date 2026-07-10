# Decision Record: Empty-State Window Model

## Context
Owner-decided behavior (not relitigated):
- Win/Linux: launching with no file, or closing the last document, shows a persistent empty window with an active File menu (Open / Open Recent) and a centered welcoming "Open a file" prompt that is both a drag-target and a click-to-open file picker.
- macOS: no empty window; dock + menu bar only. Activating the app with no windows shows the file-open panel (Preview's behavior).

This record documents the implementation-level decisions and stress-tests them against four user personas. Objections must name a concrete problem; naked preferences carry no weight.

## Implementation decisions under debate
- **D1.** On Win/Linux, closing the last tab of a window that is NOT the last window closes that window; only the last remaining window persists as an empty-state window. Rationale: avoids a pile-up of empty windows while guaranteeing the app is never left with zero windows / no way to open a file.
- **D2.** The empty state is a centered widget: icon + "Open a file" headline + a one-line actionable subtitle + an "Open File…" button; the whole surface is a drag target with a visual highlight on drag-over.
- **D3.** macOS activation with no windows opens the file-open panel, triggered on ApplicationStateChange→active when zero windows exist (guarded; not exercisable off-mac).

## Persona debate
**Office non-technical (Dana):** Concrete concern — a persisted-but-blank window after closing the last document could read as a crash. Mitigated by D2: the empty state is explicitly welcoming with a labeled button, not a blank area. No objection stands.

**Older careful (Walter):** Concrete concern on D1 — could closing windows lead to a surprise full quit? No: the last window persists as empty state rather than quitting; explicit File→Quit is the only quit path on Win/Linux. On macOS, D3 matches the Preview behavior he expects. Resolved.

**Power migrator (Priya, ex-Preview/Acrobat):** Concrete concern — on macOS, closing the last window must not quit and the dock icon must reopen a picker. D3 delivers this. On Win/Linux the lighter empty state (vs. an Acrobat start screen) is acceptable and has no lying controls. No objection.

**Occasional (Sam):** Concrete concern — a blank window is confusing on rare use. D2's self-explanatory "Open a file" + button serves this directly. He rarely runs multiple windows, so D1's pile-up avoidance is invisible to him. No objection.

## Stalemates
None. No persona raised an unresolved concrete problem. D1's "close non-last windows" is the only genuinely new default (vs. always-persist) and is invisible to single-window users (the common case).

## Arbiter verdict
Proceed with D1, D2, D3. The single item worth the owner's eventual glance: whether closing a non-last window's last document should close that window (chosen) or leave it as an empty-state window. Chose close-it to avoid empty-window pile-up; trivially reversible.
