---
name: ev1-double-check-2026-07-20
description: EV1 sourcing double-check audit (2026-07-20, PR #30) — re-verified all ~88 open tracked items across 6 subsystems against main f8b3216; result 88 HOLD / 0 CLOSE / 3 REWRITE (2 HVAC citation fixes + 1 stale note). Catalog holds up; every donor cross re-confirmed adversarially. Key surprise: the 5 open sourcing PRs are stale-behind-main and should be rebased.
metadata:
  type: project
---

Owner directive 2026-07-20 ("for all open items, do a double-check pass in chunks") executed on programmerq/ev1. A scout enumerated ~112 open items; the 88 agent-verifiable ones (open_check questions, @inferred donor crosses, special markers) were re-verified in 6 subsystem chunks by parallel read-only verifiers, adversarial refute-by-default on every cross, redux citations re-checked line-by-line. Landed as **PR #30** (`claude/double-check-open-items`, ready) with an audit doc at `sourcing/audits/double_check_2026-07-20.md`.

**Result: 88 checked / 88 HOLD / 0 CLOSE / 3 REWRITE.** The catalog holds up — no open item was closeable by desk research (each is gated on a physical measurement, bench/connector check, owner design decision, or an uncompleted donor hunt), and NOTHING was over-claimed. Per-chunk: brakes 8H, HVAC 13H+2 rewrite, lighting 9H, mirrors/doors/glass 29H+1 rewrite, steering/switches 15H, SIR/suspension/propulsion 14H.

**The 3 REWRITEs (all citation-precision; facts unchanged):**
- `hvac_system.yaml` ambient-sensor redux_ref pointed at a non-existent file `06-08_fwd_lamp_ip_harness_pass_cpt_lh.yaml` → corrected to the real atlas `06-08_fwd_lamp_ip_harness_motor_cpt_rh.yaml` (item #020). [PR #30, commit 8fbfc2f]
- Pack-voltage "225–430 V" was cited to hvac-80 but is printed on **hvac-79** (hvac-80 is the compressor-PWM page) — fixed in `hvac_compressor.yaml` (×3) + `hvac_climate_system.yaml` (×2). [PR #30, 8fbfc2f]
- `heated_windshield.yaml` (PR #25) had a stale "glass.yaml NOT on main" note — glass.yaml merged at 60812b6; reworded. [PR #25, commit af23f68]

**Crosses re-confirmed adversarially (all survived):** brake solenoid 18023464→ACDelco 18020566, ABS-VI WSS 10456045→19259629, HVAC actuator 16163982→Dorman 604-106, ambient 16169194→ACDelco 15-71823, blower module→15-8690, clockspring 26036217→Cavalier/Sunfire, DBW pedal 25140664→C5 Corvette block, front hub 7470014→BCA WE60701 (5×100), propulsion→S-10 Electric, contactor 10490007→Kilovac EV200, cooling level switch 25626342→19151900, front combo lamp 5978405→F-body, reflector 05976021→N-body.

**Key surprise / actionable follow-up:** all 5 open sourcing PRs (#13 round-5, #14 round-6, #19 odds-ends, #20 harness, #25 body-glass) are STALE-behind-main — their `git diff main..branch` shows mass deletions (added-files-only). GitHub's 3-way merge won't silently delete main's files, but they should be **rebased onto main** for clean review before merge (needs force-with-lease). Minor: A7 `door_glass_weatherstrip_profile` duplicate id between main and PR #25 (merge hygiene); soft spots left HOLD = NANFENG EHPS-1010R1.5 specific SKU (category confirmed) + Intrigue radio salvage variant 9376163 vs 9376173 (sibling-hint only; authoritative EV1 radio is 16234339).

Related: [[ev1-catalog-sweep-complete]] [[ev1-harness-sourcing-result]] [[ev1-seals-and-heated-windshield]] [[owner-max-progress-adversarial-gate]] [[ev1-sourcing-pr-granularity]].
