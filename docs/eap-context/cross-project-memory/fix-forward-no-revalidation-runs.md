---
name: fix-forward-no-revalidation-runs
description: Owner ruling (2026-07-14) — fix-forward, treat the latest completed VAT result as the baseline to improve on; no dedicated/ceremonial validation or capture runs of unfixed states
metadata:
  type: feedback
---

Owner ruling 2026-07-14 (VAT nightly context, generalizes to CI validation): "we're holding back incorrectly just to get runs on something that is intentionally very complex and is supposed to surface errors. Once we fix them, we don't need to rerun the old versions that don't have the fix."

**Why:** the VAT suite exists to surface errors; demanding N clean runs of an unfixed state before acting is backwards and wastes rig time.

**How to apply:**
- Treat the most recent completed run's result as the baseline-to-improve-on; do NOT fire dedicated "second validation" / "stable-rig proof" / capture-ceremony dispatches.
- Land fixes through the normal PR flow; the next NATURAL nightly (skip-guard passes because the merge is a code change) produces the fresh traces; baseline re-anchoring (e.g. BL-0183) happens from that natural run's artifacts.
- Expected reds shrink as fixes land; any NEW divergence gets investigated, not pre-cleared with reruns.
- Context markers: run #41 (2026-07-13, id 29275167992) designated the baseline; #245 = pin→9014aa4 + sigkill gate recalibration.

Related: [[owner-prefers-larger-prs]], [[pr-draft-ready-merge-policy]].
