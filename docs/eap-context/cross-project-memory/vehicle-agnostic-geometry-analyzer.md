---
name: vehicle-agnostic-geometry-analyzer
metadata:
  type: project
  modified: 2026-07-21T14:32:25.298Z
description: General vehicle-agnostic geometry completeness analyzer paired with the general load solver; EV1 decomposition demoted to test fixture.
---

Owner reframe (2026-07-21 ~13:04Z, given directly in the spaceframe session): the structural tooling's deliverable is NOT an EV1 reconstruction — it's a **general, vehicle-agnostic geometry analyzer** paired with the general load solver (ev1 loadpath/, PR #22 lineage): take CAD-produced geometry for ANY vehicle (2-door, 90s pickup, 50s truck) and report **what's missing**. The EV1 34-component decomposition becomes a TEST FIXTURE for the tool, not the point. The session owns the engineering-practice calls — unknown EV1 geometry never blocks solver progress; characteristics from other vehicles fill gaps and define per-vehicle-class "complete."

Completeness checks the analyzer performs: disconnected/unsupported members; load paths that terminate nowhere (suspension pickup without a continuous path to a reaction); archetype-expected members missing (rockers, pillars, cross-members, torque boxes); members lacking section/material; broken symmetry; under-constraint (mechanisms the solver chokes on).

Division of labor agreed: the loadpath session keeps the vehicle-agnostic SOLVER; the spaceframe session builds the COMPLETENESS ANALYZER that feeds it (boundary aligned between them, no duplicate build). Research pass on FE model-quality practice + structural archetypes as priors underway.

Link [[ev1-structure-three-way-interface]], [[ev1-replica-frame-reconstruction-plan]].

Status (2026-07-21 ~14:31Z): the reframed goal is DELIVERED end-to-end. `structure_analyzer/` landed on ev1 PR #41 (draft, stacked on loadpath PR #40) with the two-tier bridge LIVE: Tier-1 topological/archetype checks (16 of 30 catalogued live — connectivity, dangling members, load-path-to-ground reachability, Maxwell/DOF determinacy, symmetry, missing-archetype-member; rest stubbed+flagged) consuming loadpath's model_io parser and calling `lp::diag::diagnose()` for Tier-2 matrix confirmation + per-DOF localization (B2 classified zero-energy modes included — e.g. supports-removed reports 6 rigid-body modes with node names; a released hinge localizes to its joint). CLI takes any model + `--archetype` (2door / sedan_unibody / pickup_bof) + `--json`. All tests green both sides. The EV1 34-component decomposition now feeds FOUR consumers on one shared datum + component-id keying: mass ledger (#38), loadpath solver (#22/#40), assembly build-order (#17), completeness analyzer (#41). #41 stays draft legitimately (stacked on unmerged solver PRs + stubbed checks). Also: the git-proxy stale-tip hazard bit again and was beaten via ls-remote hard-refresh (see [[git-proxy-stale-tips-rebase-hazard]]).
