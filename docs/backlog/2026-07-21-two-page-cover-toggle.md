---
id: 2026-07-21-two-page-cover-toggle
title: Two-up mode — cover-alone vs. cover-paired pairing toggle (PR2)
priority: P3
status: open
source: committed follow-up of PR #113 (two-page view increment, PR1)
created: 2026-07-21
---

## Threshold

In Two-Pages (facing) mode the user can toggle between **cover-alone** and
**cover-paired** spread pairing, and the spread rhythm updates accordingly:

- **Cover-alone** (the PR1 default): page 1 renders alone, then facing pairs
  `[2,3], [4,5], …` — the macOS-Preview book rhythm.
- **Cover-paired**: pairing starts from page 1, so spreads are
  `[1,2], [3,4], …`.

Toggling the control re-lays-out the visible spreads live (no reopen), and the
choice persists across relaunch (consistent with never-worry-save). Pass/fail
is observable: switch the toggle, confirm the same document re-pairs from the
other anchor and the on-screen spreads change accordingly.

## Context

PR1 ships a single hard-coded pairing: cover-alone
(`spreadsFor(pageCount, coverAlone=true)` in
[`src/document/SpreadLayout.h`](../../src/document/SpreadLayout.h)). That is the
correct rhythm for **book-like** documents (title/cover page first), but a
non-book document — a two-column report, a scanned duplex stack, a slide
export — pairs correctly only under cover-paired. Until this toggle lands,
non-book documents get the wrong pairing with no way to correct it.

The work is to surface a user control (View menu + persisted setting) that
selects the `coverAlone` argument, thread it through `TwoPageView`'s
spread-build path so a change re-lays-out live, and persist it via the settings
store. The pure pairing function already takes the `coverAlone` flag, so this is
a plumbing + UI + persistence item, not a layout-algorithm change.

Do this after the PR1 view increment (PR #113) lands.
