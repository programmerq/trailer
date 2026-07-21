---
name: trailer-test-count-reporting-convention
description: Reporting convention (surfaced 2026-07-15 via PR #58) — report test coverage as "N executables / M new test functions (named)", not a bare ctest "X/X", because ctest counts executables not functions and a bare count can hide added-or-omitted coverage
metadata:
  type: feedback
---

# Report tests as "N executables / M new test functions (named)"

Surfaced on PR #58 (2026-07-15): the PR reported "UAT 16/16" and the owner's relayed question asked why it wasn't 17/17 (vs PR #55's 17/17). Root cause of the ambiguity: `ctest -L uat` counts test EXECUTABLES, not test FUNCTIONS. PR #58 added 5 UAT slots inside the existing `test_uat_recognize_text` executable (uat_ocr_060/065/070/080/090) and 5 unit slots inside the existing `test_adapters` executable, registering zero new executables — so the ctest total stayed 16. PR #55 reported 17 because it added a whole new UAT executable on its branch.

**Why:** a bare "X/X passed" suite-count is ambiguous — an unchanged count can hide whether new coverage was added, and functions added to existing executables never move the number. Counts should not be able to mask omissions.

**How to apply:** when reporting test results (PR body, status, replies), state coverage as "N executables / M new test FUNCTIONS (named)" — e.g. "16 UAT executables (unchanged from main); +5 UAT functions uat_ocr_060/065/070/080/090 in test_uat_recognize_text; +5 unit functions in test_adapters; 0 removed/skipped." Name the added functions and explicitly confirm nothing was removed or QSKIP-ped. Related: [[trailer-review-before-push-policy]].
