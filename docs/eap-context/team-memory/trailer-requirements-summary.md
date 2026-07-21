---
name: trailer-requirements-summary
description: Confirmed design-criteria decisions from the 2026-07-09 socratic interview — gates, decision machinery, milestones, push/PR policy
metadata:
  type: project
---

Confirmed 2026-07-09 (owner: programmerq). Basis for all criteria work. See [[trailer-remote-build-recipe]] for build.

**Philosophy:** Apple-CALIBER thoughtfulness, platform-native per OS (OS experience shapes = portability, not feature gating). Frugality: minimal binary/RAM, no VM/ballooned deps; per-item standing questions: is it optimized? can software meaningfully improve it? trading CPU/RAM/size for design simplicity? Never-worry saving: continuous persistence + honored explicit-Save affordance. No lying controls ever: unbuilt = disabled + tooltip (revisit hide-vs-disable at GA); silent nearest-equivalent permanently forbidden. Empty states: macOS = dock + menu bar, no window, open panel on activation; Win/Linux = empty window, Open/Recent active, centered drop-target prompt; launcher-pickers rejected.

**Decision machinery:** Personas UNRANKED — adversarial lenses only. Arbiter issues Decision Records (ADR lifecycle); reopening = superseding evidence + owner sign-off. Admissibility: objections must articulate a concrete problem caused/solved; naked preferences carry no weight. Owner is escalation-only (true stalemates/flagged ambiguity only).

**Gates (pass/fail):** Every work item declares a checkable enforceable threshold BEFORE work begins (goes in CLAUDE.md). UX Done = screenshots/recording of every affected state from the RUNNING app checked against threshold; default changes also need a Decision Record. Perf budgets: derived from platform norms, owner ratifies; interactions well under 1s; first-page render never blocks on full-file read; pathological inputs out of scope. GUI Preferences pane = hard 1.0 gate (TOML stays as escape hatch).

**Milestones/deferrals:** 1.0/GA = semver judgment (most users get value from most features); repo private now, public before 1.0; pre-1.0 feedback via simulated-tester adversarial review windows. Accessibility: before 1.0, at the dogfood-default milestone (owner comfortable making Trailer his default viewer). Update channel: deferred; no Apple Developer Program ever; allowlist install docs accepted. First arbiter docket: Cmd-A semantics, ML progress/cancel + missing-model prompt, magic-number thresholds (each needs written rationale).

**Push/PR policy (owner, 2026-07-09, verbatim rule):** "if you think it's time to push, it isn't. Do an adversarial review/second pass." Nothing is pushed or PR'd until an adversarial second-pass review of the full diff passes. PRs are then LARGE and BATCHED to conserve CI/Actions minutes. Work stays committed locally until then.
