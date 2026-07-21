---
name: trailer-integration-batch-pr40
description: The 7-branch criteria batch PR #40 MERGED to main 2026-07-11 (integration/criteria-batch → main); 45/45 green, authorship rewritten to Jeff, CI on trailer-k8s
metadata:
  type: project
---

# Criteria batch assembled → draft PR #40

**MERGED to main 2026-07-11.** The batch (criteria gates G1–G9, decision machinery, empty-state, affordances, preferences, undo stabilization, session-setup hook, skills, perf scaffolding + perf ctest labels @ e0bed9c) is live on main. Companion PR #44 (runner-image ccache bake, node24 action bumps, apt-index fix, Wine perf exclusion) merges separately; the Wine perf exclusion activates against this batch's perf labels once both are on main.

On 2026-07-10 the seven completed input branches were integrated into `integration/criteria-batch` and opened as **draft PR #40** (https://github.com/programmerq/trailer/pull/40), base `main`. 29 commits, +6025/-211, **45/45 ctest green** offscreen (Qt 6.11 + ONNX 1.25 via session-setup recipe).

**What's in it:** undo-stabilization (incl. `chore/session-setup-hook`), design-criteria-gates (G1-G9), todo-designer-review-fold, empty-state-window-model, inert-affordances, preferences-pane; plus two new skills (`.claude/skills/review-before-push`, `.claude/skills/decision-brief`), the three folded rulings (G2 offscreen-`grab()`, agent-measured perf with NO CI wall-time gate, arbiter=agent-role), and 3 structural perf tests + a 48KB `docs/perf/corpus/`. Decision record 0005 added (persistent-disabled empty-state toolbar).

**Merge:** one branch at a time, build+ctest after each. Only ONE trivial conflict (`tests/CMakeLists.txt` union). The 4-way `MainWindow.cpp/.h` hotspot auto-merged clean; all three feature sets verified coexisting.

**Authorship:** all 29 commits rewritten (git filter-branch) to `Jeff Anderson <jefferya@programmerq.net>`; every model-ID trailer/session-URL scrubbed from messages (grep-clean, tree content unchanged). Source branches left untouched.

**Open items (NOT in this PR):** macOS empty-state path is `QSKIP`-only (needs real-Mac pass); **P0 silent-discard-on-close is the NEXT work item**; G7 Preferences = 4 of 12 §6.13 panes (1.0 follow-up); render-ordering perf test `QSKIP`s until a linearized corpus exists.

**CAVEAT — memory-file discrepancy (resolved 2026-07-10 hygiene pass):** the integration brief named 7 memory files but 5 were not visible to that session (they live at the `team/` top level: [[trailer-requirements-summary]], [[trailer-review-before-push-policy]], [[trailer-perf-measurement-ruling]], [[trailer-ux-evidence-ruling]], [[trailer-followup-docket]]). The duplicate `pre-push-local-review-policy` was merged into the canonical [[trailer-review-before-push-policy]]. The rulings were sourced from the brief + on-branch docs (AGENTS.md G-gates, docs/performance-budgets.md) instead. Related: [[trailer-ci-on-k8s-runners]], [[trailer-remote-build-recipe]].
