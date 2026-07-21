---
name: ev1-a2l-refrigerant-safety-case
description: "EV1 A/C refrigerant flammability safety case (ev1 PR #35, commit bff6eef, 2026-07-21): R-1234yf is safe-if-designed-for-A2L (not a latent trap); design-for-A2L-from-day-one checklist is the operative constraint on all future A/C component sourcing."
metadata:
  type: project
  modified: 2026-07-21T15:51:10.484Z
---

Delivered 2026-07-21, ev1 PR #35 (`claude/r134a-tracked-check`, commit `bff6eef`, ready-not-merged, lint PASS). Answers the owner's Pinto/Fiero-analogy fire-hazard worry about switching the A/C loop from R-134a (A1, non-flammable) to R-1234yf (A2L, mildly flammable). Lives in `sourcing/parts/chemicals_fluids.{yaml,md}` (`flammability_isolation_analysis` + `component_refrigerant_compatibility` + `skeptic_review` blocks) with cross-refs from `hvac_compressor.yaml`.

**Honest verdict:** R-1234yf is NOT a latent Pinto/Fiero-class trap — A2L is genuinely hard to ignite (LFL ~6.2 vol%, flame speed ~1.5 cm/s, MIE orders of magnitude above hydrocarbons, vapor AIT ~405°C, small MVAC charge). Global MVAC default since ~2013 because it's hard to ignite. The REAL risk the owner intuited is process, not chemistry: designing to R-134a's "non-flammable" assumption then charging A2L without revisiting the safety case.

**Three genuinely-elevated pairings** (everything else is low-credibility): (1) vintage-routed line chafing onto a live HV connector; (2) counterfeit/contaminated refrigerant — counterfeit R-40 forms PYROPHORIC trimethyl-aluminum on the aluminum lines, independent of the flammability envelope → always run a refrigerant identifier before charging; (3) pressurized oil+refrigerant SPRAY at an arc/hot surface — POE oil mist ignites near ~240°C, far below the 405°C vapor AIT (the Daimler "ball of fire" mechanism).

**OPERATIVE CONSTRAINT for all future A/C component sourcing — the "design-for-A2L-from-day-one" checklist** (so a future yf swap changes the service kit, not the safety case, even while charging R-134a now): POE oil only; shroud/separate refrigerant lines from HV arc sources; orient spray/relief away from hot spots; vent low points (yf is ~4x heavier than air, pools low); sealed hermetic compressor terminals; **spec a BRUSHLESS (BLDC) condenser fan, not brushed** (a brushed commutator arcs continuously right at the leak-prone condenser); HV de-energize on crash. Also: fully evacuate the system (residual air/non-condensables are a documented hermetic internal-rupture path — the "sealed compressor = no oxidizer" argument only holds for a properly evacuated envelope).

**Standards mapping (corrected — earlier labels were wrong):** J639 = umbrella safety standard (unique fittings, flammable label, relief-away-from-hot-surfaces, keep-refrigerant-out-of-cabin); J1739 = the PFMEA methodology; J2842 = EVAPORATOR robustness (NOT compressor); J2844 = refrigerant PURITY (NOT a service procedure); EPA SNAP = J639+J1739+J2844.

Skeptic-hardened (3 independent agents): 12/12 property numbers + 8/8 standards labels confirmed, no fearmongering; 4 under-claims fixed in place. This is the sourcing-side resolution of owner thermal ruling 10(e) ("R-134a unless component sourcing forces otherwise" — see [[owner-decision-queue]] item 10). Related: [[ev1-replica-thermal-architecture-plan]], [[owner-skeptic-pass-before-final]], [[ev1-replica-selective-fidelity]].
