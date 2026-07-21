---
name: abs-split-mu-determinism-bisect-2026-07-19
description: Determinism bisect (2026-07-19) result — abs_split_mu is run-to-run DETERMINISTIC at both ev1sim pins 116a7ff5 (+0.6607°) and e17beae3 (+0.6901°); the pin difference is a deterministic behavioral shift, not a determinism loss. N=10 confirmation was wiped by pinned-runner CPU starvation. Includes the artifact-salvage technique.
metadata:
  type: project
---

# abs_split_mu determinism bisect — result and method (2026-07-19)

Ran the determinism-bisect harness (electricsim `.github/workflows/vat-determinism-bisect.yml`, added by #330) holding electricsim at pin `5d3ba0ee`, varying ev1sim, scenario `abs_split_mu`, measuring per-run yaw drift + run-to-run spread.

**Result — both ev1sim pins are run-to-run DETERMINISTIC (range 0):**
- OLD pin `116a7ff5`: yaw +0.6607° ×3 (range 0.0000), stop_dist 55.9679 m ×3.
- NEW pin `e17beae3`: yaw +0.6901° ×3 (range 0.0000) at N=3.
- The 0.6607 vs 0.6901 gap is a **deterministic behavioral shift** caused by the ev1sim pin change — NOT run-to-run non-determinism. So `abs_split_mu` is NOT intrinsically non-deterministic at N=3 on the pinned node; the earlier cross-lane "#52" variance points to an ENVIRONMENTAL/cross-lane cause, not intrinsic FP/thread-scheduling. (Caveat: N=3 is small; a clean N=10 was not obtained — see starvation below.)

**Node CPU-starvation wipeout (intermittent, real).** The N=10 confirmation run (29705121342, new pin) had ALL 10 runs stall at ~5.0s sim-time and die at the 57s watchdog: 5s sim in 57s wall ≈ 0.09× realtime, ~5× slower than the old-pin run — while the old-pin run and the earlier N=3 completed fine on the same pinned runner. This is severe, intermittent host CPU-starvation on the pinned mighty node (same class as the earlier 2.4-2.5× uniform-dilation / cgroup-CFS-throttle suspicion). A longer watchdog does NOT fix it (would need ~340s); the fix is a non-starved node (reserved CPU + one-run-per-node anti-affinity). Do NOT burn rig runs into a starved node.

**The 57s watchdog** = max_time_s(30) + fleet.READY_BUDGET_S(12) + 15, computed in electricsim `vat/manifest.py:120-127` (Case.timeout_s). It's a pure wall-clock kill guard (popen.wait timeout, `vat/fleet.py:374-383`) — does NOT feed physics/yaw. The ONLY per-case override is the `timeout_s` field in `vat/suites/abs.json`, which lives in the pinned electricsim tree; there is NO env var or CLI flag. To measure a SLOW old pin, cut a one-commit branch off the held electricsim pin whose ONLY diff is `"timeout_s": 120` on the case (physics-neutral; keeps yaw comparable) and dispatch electricsim_ref=that branch's full SHA. (Used branch `determinism/abs-split-mu-timeout120` = 5d3ba0ee + timeout-only, HEAD 4c149cbe.)

**Artifact-salvage technique (essential).** The determinism-bisect in-workflow report step runs `scripts/vat_determinism_report.py` from the PINNED electricsim_ref checkout, so it CRASHES for any pin predating #330 (script absent) — fixed forward in #341 (report tooling checked out from the dispatch ref into `_tooling/`). Regardless, the raw per-run data always uploads as artifact `vat-determinism-abs_split_mu-<runid>` (layout: `run_*/abs_split_mu/on/scenario.csv` + btcm.csv + summary.json). To salvage: download+unzip the artifact, then run the report from a main checkout: `python3 <main>/scripts/vat_determinism_report.py --runs-dir <dir-with-run_*> --scenario abs_split_mu`. Check each `run_*/summary.json` `status` (INFRA = watchdog kill, no scenario.csv/yaw). `summary.json` fields: `ev1sim_sha` = the commit actually checked out+run (git rev-parse of the ev1sim checkout); `ev1sim_pin` = the electricsim baseline pin-file constant (vat/baselines/ev1sim_pin.txt), provenance only — the two differ by design in a bisect (field naming is confusing; a disambiguation follow-up is pending).

Related: [[chrono-simd-march-native-sigill]], [[fix-forward-no-revalidation-runs]].
