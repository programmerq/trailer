---
id: 2026-07-12-untitled-close-save-regression
title: Headless regression test for untitled-doc close-save path + non-blocking progress for large saves
priority: P2
status: open
source: affordances session harvest; consolidated docket 2026-07-10
created: 2026-07-12
---

## Threshold

A headless regression test covers the untitled-document close-save path, and
large saves show non-blocking progress (the save does not freeze the UI thread
and a reviewer can observe progress during a large save). TBD — declare the
concrete progress-widget / timing pass/fail line before work begins.

## Context / Body

Leftover from the P2 regression-proofing batch (the makeDisabledAction helper,
the `-Werror=switch` enum-switch convention, and the Settings live-vs-restart
registry all landed in PR #48). Still open: the untitled-doc close-save path
has no headless regression guard, and large saves block rather than showing
non-blocking progress.

## Provenance

Affordances work session, harvested into the consolidated follow-up docket
2026-07-10 (P2 regression-proofing; the remaining open sub-item).
