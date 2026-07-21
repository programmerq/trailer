---
name: ev1-harness-sourcing-result
description: EV1 replica harness-sourcing automation result (2026-07-20, PR #20) — redux harness model → 148 connectors/42 segments collapse to 13 families → generated pigtail-availability table; most families are cheap buyable leaded pigtails, bespoke residual is HV blindmate + big Micro-Pack 100 JB housings, honest gap is physical segment lengths (42-segment measure queue). Plus odds-ends hardware PR #19.
metadata:
  type: project
---

Owner harness directive (2026-07-20) executed as staged automation in programmerq/ev1. The leverage: redux's harness model is machine-readable, so connectors collapse to a bounded family set and sourcing becomes a family→pigtail lookup + donor-farm plan. "Pigtails-with-leads convert crimp-thousands into splice-hundreds."

**PR #20 (`claude/harness-sourcing`, ready) — harness sourcing:**
- `sourcing/harness/gen_harness_fingerprints.py` reads redux (`harness/ev1_connector_catalog.yaml` + 12 `unified/by_board/*.yaml`) → `connector_fingerprints.yaml` (148 connector sites: 42 rich per-cavity + 106 stubs) + `segment_fingerprints.yaml` (42 segments from shared-circuit reconstruction, 11 multi-drop). Records redux SHA 4e4d6706; `--check` clean; SKIPs when redux absent so ev1 CI never fails.
- `sourcing/harness/gen_availability_table.py` (redux-free, CI-`--check`-able) joins `family_availability.yaml` verdicts against the census → generated table in `sourcing/parts/harness_sourcing_strategy.md`.
- **13 families / 74 distinct connector P/Ns.** CONFIRMED cheap buyable leaded pigtails: **Metri-Pack 150** (biggest — 48 sites; mirror pigtail 12110293 $8.99 in stock), **Metri-Pack 280** (Grote 841037/841038 ↔ GM 12101898/PT168 & 12101897/PT167), **GT 150** ($9.99), **Micro-Pack 064** ($0.49), **Pack-Con I** ($6.99), **Packard 56**, **Micro-Pack 100W Header** (kit ships verbatim EV1 12129025), lamp sockets (already cased in exterior_lighting).
- DOWNGRADED to candidate in the adversarial pass: **Micro-Pack 100 junction-block backbone** (35 sites / 745 cav — the biggest cavity count; terminals plentiful $0.49 but the big JB housings 12110244/45 show DISCONTINUED → farm from donor); MP480/hybrid/header single-site bodies (terminals via parent family, no pre-leaded SKU).
- BESPOKE residual (scratch-build): the 5 no-P/N **HV blindmate/inline** connectors (AD.HV, AD.LV, PSCM.blindmate, APM.battery_inline, APM.compressor_cannon).
- **Donor-farm plan:** #1 = a mid-90s **Cavalier/Sunfire (J-body) dash+engine harness** (~$40-100 junkyard) = a bag of correct MP150/Micro-Pack connectors with tails + shares EV1's SIR 12126040 & temp-sensor family + column lineage. Also ABS-VI donor cars, Saturn S-series body, mid-90s GM truck/van.
- **Honest gap → `sourcing/harness/measure_queue.yaml`:** redux has topology, not geometry, and R4 forbids invented lengths — so physical SEGMENT LENGTHS (42 segments; 11 multi-drop also need branch-point positions) are the owner's physical-measure residual off a donor/reference car.
- **Competing-interest fork raised as non-blocking PR #20 comment:** SIR/airbag connector 12126040/PT1745 — fidelity/safety vs cost ($63-122 restricted OEM + GM J-38125 "do not substitute" mandate vs ~$8 generic yellow 2-way for a non-live-airbag replica). Owner's call.
- True end-to-end segment donors are rare (EV1 inter-ECU topology is EV-specific); value is connector+lead farming, not drop-in segments. A fingerprint-match pass is automatable against segment_fingerprints.yaml.

**PR #19 (`claude/sourcing-odds-ends`, ready) — odds-and-ends hardware:** `sourcing/parts/fasteners_and_hardware.{yaml,md}` triages the ~368-row hardware bucket into live-orderable / generic-spec / bespoke per class (key/lock cylinders, threaded fasteners, clips, grommets/seals, brackets). Flagship: front-door outside lock cylinder **12520461 = C4 Corvette 1991-1996 cylinder kit, live-orderable coded-with-keys** (cross-ref front_door_hardware.yaml). Key blank 07025019 = Saturn ~$0.88.

Related: [[ev1-catalog-sweep-complete]] [[ev1-sourcing-sweep-round1]] [[owner-max-progress-adversarial-gate]] [[ev1-sourcing-pr-granularity]] [[ev1-replica-sourcing-plan]] [[ev1-replica-part-candidates]].
