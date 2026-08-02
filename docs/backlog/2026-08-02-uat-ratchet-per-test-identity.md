---
id: 2026-08-02-uat-ratchet-per-test-identity
title: UAT ratchet compares counts, not test identity — a regression can hide behind a new passing test
priority: P3
status: open
source: follow-on from the 2026-08-02 ratchet rule fix (ratio → absolute passed count)
created: 2026-08-02
---

## Threshold

`scripts/compare-uat-baseline.sh` returns `worse` for a night where test
`X` passed in the baseline and fails in the current run, **even when the
total number of passing tests rose** (e.g. baseline 40/41, current 41/43,
where the two new tests pass and `X` regressed). Demonstrated by a case in
`scripts/test-compare-uat-baseline.sh` that fails against today's
count-only rule and passes after the change.

## Context

The ratchet compares nightly UAT results against the previous nightly's
published `uat-summary.json`. As of 2026-08-02 the comparison axis is the
**absolute passed count** — it replaced a pass-ratio axis that reported a
false `REGRESSED` whenever a failing test was *added* (see that script's
header comment, and the false positive on `nightly-20260801`'s macOS
lane: 40/41 → 40/42 with the pass count flat).

Absolute-count comparison fixes that false positive and is strictly better
for this repo's growth pattern, but it is still a **count** comparison, so
it inherits a narrower version of the same class of blind spot in the
opposite direction:

| baseline | current | reality | verdict today |
|---|---|---|---|
| 40/41 | 41/43 | 2 new tests pass, 1 existing test regressed | `better` ❌ |
| 40/41 | 40/42 | 1 new test fails, nothing regressed | `same` ✅ |

The first row is the gap. It needs **per-test identity**, which the
summary does not carry: closing it means writing a `{test_name: passed}`
map into `dist/uat-summary.json` (schema_version 2) and comparing sets —
`worse` iff some test passed in the baseline and fails now. That is a
schema change plus a parser change, not a tweak to the comparison, which
is why it was not folded into the 2026-08-02 fix.

Two things make this lower priority than the bug it replaced:

- **Direction of failure.** The ratio bug fired on the *common* path
  (adding a red regression guard, which AGENTS.md's CI cadence section
  actively requires) and cried wolf. This one fires only when a
  regression and a suite addition land on the *same night*, and it
  under-reports rather than over-reports.
- **The lanes it affects are non-gating.** Wine and macOS UAT are
  informational; the ratchet reds the run's own signal, never the
  artifact. A missed night is caught by the next night where the counts
  are not coincidentally offsetting.

Note that the `passed` count alone cannot distinguish these cases even in
principle — no refinement of the current schema closes this. The parser
in `scripts/parse-ctest-uat-summary.sh` already reads ctest's summary
line only; per-test status would come from `ctest --output-junit` (which
both lanes could emit alongside the existing log at no extra runtime
cost) rather than from parsing the human-readable log harder.
