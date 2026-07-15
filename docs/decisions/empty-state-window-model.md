# Decision Record: Empty-State Window Model

## Context
Owner-decided behavior (not relitigated):
- Win/Linux: launching with no file, or closing the last document, shows a persistent empty window with an active File menu (Open / Open Recent) and a centered welcoming "Open a file" prompt that is both a drag-target and a click-to-open file picker.
- macOS: no empty window; dock + menu bar only. Activating the app with no windows shows **no** file-open panel — dock icon + menu bar only. The Open panel is shown only on explicit `⌘O` / File → Open. (Refined by backlog `2026-07-12-macos-launch-no-open-panel`; the earlier "activation opens the panel, Preview's behavior" default was reversed by owner dogfood ruling — an auto-panel whose dismissal read as an unwanted quit.)

This record documents the implementation-level decisions and stress-tests them against four user personas. Objections must name a concrete problem; naked preferences carry no weight.

## Implementation decisions under debate
- **D1.** On Win/Linux, closing the last tab of a window that is NOT the last window closes that window; only the last remaining window persists as an empty-state window. Rationale: avoids a pile-up of empty windows while guaranteeing the app is never left with zero windows / no way to open a file.
- **D2.** The empty state is a centered widget: icon + "Open a file" headline + a one-line actionable subtitle + an "Open File…" button; the whole surface is a drag target with a visual highlight on drag-over.
- **D3.** macOS activation with no windows does **nothing automatic** — dock icon + menu bar only, **no** file-open panel. The panel is shown only on explicit `⌘O` / File → Open. (Amended per backlog `2026-07-12-macos-launch-no-open-panel`: the original D3 auto-opened the panel on ApplicationStateChange→active with zero windows; owner dogfood ruling reversed that because the panel's dismissal quit the app and a Mac launch should not pop a picker. The macOS-only `setQuitOnLastWindowClosed(false)` guard is what guarantees dismissing a dialog never quits there; off-Mac the setting is left at Qt's default (true) because the persistent empty-state window already keeps a top-level alive, so a dialog is never the sole window — disabling it there would strand the process with no window and no way to quit or open a file.)

## Persona debate
**Office non-technical (Dana):** Concrete concern — a persisted-but-blank window after closing the last document could read as a crash. Mitigated by D2: the empty state is explicitly welcoming with a labeled button, not a blank area. No objection stands.

**Older careful (Walter):** Concrete concern on D1 — could closing windows lead to a surprise full quit? No: the last window persists as empty state rather than quitting; explicit File→Quit is the only quit path on Win/Linux. On macOS, D3 (as amended) guarantees the surprise he fears cannot happen — activation with no windows does nothing and dismissing a dialog never quits (`setQuitOnLastWindowClosed(false)`); no auto-panel pops that he could accidentally quit by cancelling. Resolved.

**Power migrator (Priya, ex-Preview/Acrobat):** Concrete concern — on macOS, closing the last window must not quit. D3 (as amended) delivers this: the app stays alive as dock icon + menu bar. On reflection an *auto*-reopened picker on activation was the wrong default (owner dogfood: it popped an unwanted Finder dialog whose dismissal quit the app); the panel is now explicit `⌘O` / File → Open only, which still gives her a fast keyboard path back to a document. On Win/Linux the lighter empty state (vs. an Acrobat start screen) is acceptable and has no lying controls. No objection.

**Occasional (Sam):** Concrete concern — a blank window is confusing on rare use. D2's self-explanatory "Open a file" + button serves this directly. He rarely runs multiple windows, so D1's pile-up avoidance is invisible to him. No objection.

## Stalemates
None. No persona raised an unresolved concrete problem. D1's "close non-last windows" is the only genuinely new default (vs. always-persist) and is invisible to single-window users (the common case).

## Arbiter verdict
Proceed with D1, D2, D3. The single item worth the owner's eventual glance: whether closing a non-last window's last document should close that window (chosen) or leave it as an empty-state window. Chose close-it to avoid empty-window pile-up; trivially reversible.
