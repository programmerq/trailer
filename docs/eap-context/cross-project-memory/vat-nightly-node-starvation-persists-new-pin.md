---
name: vat-nightly-node-starvation-persists-new-pin
description: First healthy-new-pin VAT nightly (run 29821014842, 2026-07-21, sha 16c8544e) came back WEDGED — 13/14 cases INFRA from the SAME CPU node-starvation, proving starvation is pin-independent. Plus the honesty rule — a RED from a wall-clock-sampled criterion on a starved node is NOT a clean finding.
metadata:
  type: project
  modified: 2026-07-21T12:31:50.792Z
---

# VAT nightly node-starvation persists on the new ev1sim pin (2026-07-21)

The first VAT nightly on the bumped pin (#353: ev1sim → 5905a220) was run **29821014842** (2026-07-21, electricsim head 16c8544e). It came back **WEDGED, not healthy**: `result=fail`, `counts=FAIL:1,INFRA:13` (committed `vat/baselines/nightly_status.txt`). 13 of 14 cases were watchdog-killed (`ev1sim still running after 57-102s — killed`); children peak RSS only ~167 MiB, so **CPU-starved, not memory** — the SAME starvation class as the 7/19–7/20 runs ([[abs-split-mu-determinism-bisect-2026-07-19]]). **Starvation is pin-independent** (old pin, new pin — both starve on the shared node); the fix is infrastructural (reserved CPU + one-run-per-node anti-affinity), which is the owner's open node-starvation decision, not a code change.

**Reporting trap (fell into it this run):** a step-level GitHub-Actions "success" on "Run the abs+safety suites" does NOT mean the run was healthy — the step exits 0 while individual CASES come back INFRA. Always read the case-level `counts=FAIL:n,INFRA:m` in `nightly_status.txt`, never infer health from a green step or a ~30-min step duration.

**Honesty rule — a wedged-node RED is not a clean finding.** `safety_ad_precharge_timeout` was the only non-INFRA case (both sub-runs exited rc=0, then FAIL). Tempting to call it "ran to completion ⇒ genuine finding," but the precondition for that is a HEALTHY rig, and this run's node was demonstrably starved. This criterion samples the precharge relay on **wall-clock** (`notes/manual_supplements.yaml#2026-07-10-ad-precharge-relay-wallclock-sampling`), which mis-times under CPU starvation **even when the process exits cleanly**. So on a starved node its RED most plausibly = the known 7/20 environmental artifact, NOT a new finding. Do NOT escalate a wall-clock-sampled RED off a wedged run. (One carry-forward to re-check on a CLEAN run: the red-proof lane showed the `ad_main_contactor_closed` producer DEAD at the pin — real telemetry gap vs starvation artifact is unseparable on a wedged run.)

**What DID hold — #352 (vacuous-green) did not regress.** The pending-nightly trio `abs_low_mu` / `abs_diagonal_mu` / `abs_mu_jump` stayed `pending-nightly` (verified in committed criteria at post-run sha 5f1c83e0), un-adjudicated (honest baselines watchdog-killed). Critically NO silent re-proof against a dead all-sentinel(-1) `abs_phase_fl` column — the sentinel-aware `signal: abs_phase_fl` liveness guard held. No vacuous green slipped through; but no forward progress either — proof still owed on a non-starved node.

**Commit-back survived on the OLD script** (this run predates the #367 merge): attempt-1 push rejected (remote moved 16c8544e→8051437d), the retry loop's `git pull --rebase --autostash` rebased and the 2nd push landed (5f1c83e0). #367 (merged 2026-07-21) makes that race-recovery deterministic going forward instead of luck-of-the-retry.

Related: [[abs-split-mu-determinism-bisect-2026-07-19]], [[fix-forward-no-revalidation-runs]], [[chrono-simd-march-native-sigill]].
