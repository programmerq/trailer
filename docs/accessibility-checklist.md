# Trailer — Accessibility Checklist

> **ENFORCED starting at the dogfood-default milestone (per gate G8 in
> [`../AGENTS.md`](../AGENTS.md)), not before.** Until the owner declares that
> milestone (a `dogfood-default` marker in `ROADMAP.md` / the changelog, or a
> git tag `dogfood-default`), G8 is dormant and this checklist is a target, not
> a merge/release gate — but no PR may *regress* an accessibility affordance
> already shipped.

This is the concrete, per-row pass/fail checklist gate G8 cites. Each row is
run against the **running app** (not a `grab()` capture — several rows depend on
live focus, AT, and event-loop behaviour) and marked pass/fail with the
evidence noted. It covers the accessibility surface specified in DESIGN §6.12.

Numbers are seeded from the platform accessibility guidance — Apple HIG
*Accessibility*:
https://developer.apple.com/design/human-interface-guidelines/accessibility

## Checklist

| # | Item | Pass condition (objective) | How to check |
|---|---|---|---|
| A1 | **Text contrast (WCAG AA)** | Every text element meets a contrast ratio of **≥ 4.5:1** at ≤ 17 pt, or **≥ 3:1** at ≥ 18 pt (or ≥ 14 pt bold), against its background — in every theme, including the high-contrast theme | Sample foreground/background colours of each text style with a contrast checker; verify against the size threshold |
| A2 | **Non-text / control contrast** | Interactive-control boundaries, icons, and meaningful graphics meet **≥ 3:1** against adjacent colour | Contrast-check control borders/glyphs and focus indicators |
| A3 | **Full keyboard operability** | Every actionable command is reachable and operable by keyboard alone — no mouse-only path. Tab order is logical and does not trap | Unplug the mouse; drive every menu, toolbar, dialog, and canvas action from the keyboard |
| A4 | **Visible focus** | The focused control always shows a visible focus indicator (`focus-visible`); focus never lands on an element with no visible ring | Tab through every surface; confirm the focused element is always visually identifiable |
| A5 | **AT labels on every actionable control** | Every button, menu item, toggle, field, and canvas action exposes an accessible name/role via the platform API (NSAccessibility / UIA / AT-SPI); none announces as unlabeled/"button" only | Navigate with VoiceOver (macOS) / Narrator (Windows) / Orca (Linux); confirm each control is announced with a meaningful name and role |
| A6 | **Reduce Motion honoured** | With the OS Reduce-Motion (or the in-app equivalent) setting on, every non-essential animation named in DESIGN §2.4 is suppressed or replaced with an instant transition | Enable Reduce Motion; exercise the flows that animate; confirm no suppressed animation plays |
| A7 | **Text scaling to ≥ 200%** | The UI and document chrome remain usable — no clipped, overlapped, or unreachable controls — at a text/UI scale of **≥ 200%** | Set the in-app configurable text size (and/or OS scaling) to ≥ 200%; verify no control is clipped or lost |
| A8 | **Minimum control target size** | Every actionable control presents a hit target of at least **28 × 28 pt** (macOS minimum) | Measure the smallest interactive controls (toolbar buttons, close affordances, inline chips) |
| A9 | **No colour as the sole signal** | No state or meaning is conveyed by colour alone — every colour-coded signal also carries text, shape, icon, or position | Review each status/validation/selection cue; confirm a redundant non-colour channel exists |
| A10 | **High-contrast theme present** | A high-contrast theme is selectable independent of system theme, and passes A1/A2 in that theme | Select the high-contrast theme; re-run A1/A2 |
| A11 | **Configurable text size present** | An in-UI control changes text size and the setting persists across relaunch | Change the text-size control; relaunch; confirm the value survives |
| A12 | **Read-aloud / TTS** | Selected text can be read aloud via the OS speech engine (DESIGN §6.12) | Select text; invoke read-aloud; confirm speech output |

## How to use

- At the dogfood-default milestone, run every row against the running app on
  each available platform, record pass/fail + evidence (screenshot, AT
  transcript, or measured value), and attach the completed table to the
  milestone as the G8 evidence artifact.
- Before that milestone, use this as the definition of "no regression": a PR
  that touches an affordance above must not move any passing row to failing.
