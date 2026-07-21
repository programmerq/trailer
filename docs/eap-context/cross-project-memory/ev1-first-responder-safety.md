---
name: ev1-first-responder-safety
description: EV1 replica first-responder safety workstream (blind-spot #7) — ISO 17840 rescue-sheet stack + 33-decision choose-list in programmerq/ev1 safety/, draft-then-ready PR #26. 0 open forks (all 33 settled); 4 resolved by owner rulings.
metadata:
  type: project
  modified: 2026-07-20T23:27:25.621Z
---

First-responder / rescue-information workstream for the EV1 replica (owner-directed 2026-07-20, "blind-spot #7"). Home: `safety/` in [[ev1-umbrella-repo]] (programmerq/ev1), delivered as PR #26 (ready-for-review).

**THE STANDARD:** the owner was recalling **ISO 17840** "Road vehicles — Information for first and second responders." Part 1 = passenger-car **rescue sheet** (one page, 5 blocks, standardized colour/pictogram code), Part 3 = **Emergency Response Guide (ERG)** (10 chapters, fixed header RGB), Part 4 = **propulsion-energy "PEI" diamond label**. (Owner's remembered numbering was off — rescue sheet is Part 1, not Part 2.) US cross-refs: **SAE J2990** (responder recommended practice) + **NFPA Emergency Field Guide**. Deliverable = 17840-1 sheet + -3 ERG + -4 label + QR-sticker workflow.

**FAITHFUL BASELINE** (`safety/ev1_safety_baseline.md`, path:line manual cites): HV interlock loop (break → contactors open + bus-cap discharge <42.4V/250ms, DTC 070); manual service disconnect behind driver's seat (turn-then-pull, splits pack at midpoint into two ~half-voltage packs — still lethal); Auto-Disconnect board K1/K3 contactors + isolation/ground-fault/precharge; the deliberate >1 km/h isolation-inhibit (HV stays live into a ground fault while moving); orange HV cables; 26×12V VRLA lead-acid 312V T-tunnel pack + hydrogen vent. **Two defining GM absences: NO crash-triggered HV shutdown, and ZERO first-responder docs** — so the rescue deliverable is essentially net-new, ledgered as modern additions.

**CHOOSE-LIST** (`safety/choices/*.yaml` + rollup `safety/choose_list.md`): 33 decisions, each with EV1-faithful option / modern-practice option / debate-derived default. Gated by TWO independent adversarial verifiers (fidelity-fork lens + life-safety/standards lens). Load-bearing safety rules that MUST hold: (1) every rescue-sheet number is a MODERN-pack property, never laundered from the 1996 manuals (EV1 specs live only in the baseline/emulation ledger); (2) assume-energized-until-verified — no fabricated responder wait-time; touch-safe = ISO Class-B 60V DC; (3) crash disconnect = controller-independent pyrofuse/coil-kill PRIMARY, the faithful-interlock-loop break is a secondary ledgered cosmetic flourish; (4) HVIL must be a hardwired, firmware-independent coil interlock into the modern contactors; (5) the linchpin decision `hv-present-confirmation-indicator` (how a responder confirms HV off).

**FORKS:** 0 open — all 4 genuine forks resolved by owner rulings (2026-07-20). (1) exterior-safety-label-visibility → DISCREET/faithful: markings hidden at shut-lines/charge-flap, single B-pillar PEI energy diamond, cut-point never hidden (owner: 'Faithful. Modern EVs don't have any conspicuous markings today'). (2) manual-service-disconnect-placement → keep faithful behind-seat cabin lever + adopt S-10-Electric-style external disconnect (owner ruling; the under-hood loop disconnect is the S-10's, NOT the EV1's — redux-confirmed). (3) charging → NACS conductive inlet + cosmetic Magne-Charge paddle (ev1 #9). (4) pack location → faithful T-tunnel (ev1 #24). Reclassified during reconciliation: isolation-inhibit-above-speed fork→settled.

Links: [[ev1-umbrella-repo]] [[ev1-replica-powertrain-direction]] [[ev1-replica-selective-fidelity]] [[adversarial-debate-not-owner-gating]] [[owner-wants-panel-to-one-up-his-ideas]].
