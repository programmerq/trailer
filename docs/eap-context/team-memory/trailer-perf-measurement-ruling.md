---
name: trailer-perf-measurement-ruling
description: Owner ruling 2026-07-10 — performance budgets are enforced by implementing agents + review rounds, NOT a CI wall-time gate; CI gets only deterministic proxies
metadata:
  type: project
---

Owner, 2026-07-10 (verbatim gist): "Rather than a CI gate, the performance guidelines should be handled by the agent(s) that implement changes. Any CI measuring of performance will be inaccurate."

**How to apply:** the perf budgets in docs/performance-budgets.md are targets the IMPLEMENTING agent measures locally (report numbers in the work item's Done evidence) and the review round checks. CI must NOT gate on wall-time. CI may enforce only noise-immune deterministic proxies: structural assertions (e.g. first-page paint occurs before full-file read; no synchronous file IO on the GUI thread), counter budgets (paints/relayouts per interaction), and optionally instruction-count tripwires (cachegrind/perf-stat style) on hot paths — instruction counts are far stabler than CPU time, which is stabler than wall time, on the shared trailer-k8s Xeon runners. This supersedes the "reference rig + corpus makes B1–B4 binding in CI" idea; the committed corpus remains useful for agents' local measurements.

Related: [[trailer-review-before-push-policy]], [[trailer-followup-docket]] (P1 enforcement-layer item adjusts accordingly).
