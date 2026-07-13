---
id: 2026-07-12-g5-real-app-empty-state-run
title: Run gate G5 once against the real app to prove the evidence artifact is producible
priority: P1
status: open
source: criteria-gates session harvest; consolidated docket 2026-07-10
created: 2026-07-12
---

## Threshold

Gate **G5** (empty state is platform-correct) is run once against the real
app, producing per-OS empty-state screenshots (or, on macOS, the dock-icon +
menu-bar note per the §2.4.2 contract). The artifact demonstrably exists,
proving the G5 evidence artifact is producible, not just specified.

## Context / Body

G5 is defined in `AGENTS.md` and depends on the DESIGN §2.4.2 empty-state
contract, but has never been exercised end-to-end against a built app. This
item runs it once per available OS to confirm the required evidence can
actually be produced.

Related: this overlaps the macOS-launch rework — see
`2026-07-12-macos-launch-no-open-panel`, which may change what the macOS
empty-state artifact should show.

## Provenance

Criteria-gates work session, harvested into the consolidated follow-up docket
2026-07-10 (P1 item c).
