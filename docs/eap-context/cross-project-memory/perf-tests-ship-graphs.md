---
name: perf-tests-ship-graphs
description: "Standing practice: every performance-test criterion ships with its data graph, what it measures, and what good/bad looks like"
metadata:
  type: feedback
  modified: 2026-07-20T23:20:17.431Z
---

Standing practice, adopted 2026-07-20 from the owner's abs_split_mu ruling (item 1 in [[owner-decision-queue]]): for performance testing, every criterion ships with the data/graph behind it, a plain explanation of what it measures, and what good vs bad looks like.

**Why:** the owner reviews pass/fail parameter choices from the graphed data, not prose — his split_mu ruling was "inclined to go with your recommendation, but... I should review the graphs with these parameters."

**How to apply:** any session adding or changing a perf criterion commits an annotated plot + explanation alongside the criterion, and surfaces the graph link when asking the owner for metric sign-off.
