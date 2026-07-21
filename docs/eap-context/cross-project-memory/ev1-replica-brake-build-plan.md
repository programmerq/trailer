---
type: project
title: EV1 replica brake build plan — the flagship-fidelity wedge
created: 2026-07-16
tags: [ev1-replica, brakes, fidelity, patent]
---

# EV1 replica brake build plan

The brake system is FAITHFUL-required (owner's flagship fidelity wedge). Full
plan as of 2026-07-16:

- **Front:** standard disc brakes, standard calipers. redux CHBK 4-6:
  rotor `18019763`, caliper `18023473` LH / `18023474` RH, pad kit `18023477`.
- **Hydraulic pack (modulator):** GM **ABS-VI**-derived unit — see the
  SHARPENED 2026-07-19 note below, which supersedes the earlier
  "decommission the 3rd channel" phrasing. The EV1 BPMV has only **2 front
  hydraulic channels**; the plan is a donor 3-channel ABS-VI motor pack
  **repurposed to drive the 2 front channels**, with taller/larger solenoids
  per the manual's own description, to meet the higher duty cycle vs a
  standard ABS-VI. (ABS-VI family @source:patent US5281009A; EBCM GM P/N
  16177800 per electricsim chip-identity.)
- **Master cylinder:** NOT hydraulically attached to the pack — a **MODIFIED
  master cylinder serves as the PEDAL-FEEL EMULATOR body**. OPEN: identify
  brake **pressure sensors that fit its ports**.
- **Brake lines:** standard, bent + flared normally (substitute-OK).
- **Rear brakes:** hub + drum backing plate sized as close as possible to a
  **donor drum**. redux CHBK 4-6: BRAKE ASM-RR `18029916` LH / `18029917` RH,
  SHOE KIT `18024169`, SPRING KIT-RR BRK ACTR LVR `18024170`, DRUM ASM-RR
  `18024690`. **Full-faithful option:** a UK company can machine a pair of
  **custom ALUMINUM drums for ~£500 GBP** given the size from the donor.
- **Rear motor actuator (TRICKIEST):** identify motor(s) meeting the EV1 spec,
  then **fabricate a custom housing**. Architecture = **dual-motor
  self-energizing drum, electronic rear actuation + electronic park brake in
  ONE drum**. KEY REFERENCE: EV1 brake patent **@source:patent US5366281**
  (Delco Electronics / GM — parking-brake latch/unlatch + current-verify
  choreography; cited in electricsim `ev1/btcm/firmware/btcm_firmware.c`,
  `ev1/btcm/abs_diag.h`, `formal/p/ev1/PSpec/btcm_specs.p`).

**SHARPENED 2026-07-19** (redux PR #56, scan-cited brakes-311/312,
fidelity-checked): the EV1 BPMV (brake pressure modulator valve) is **2 front
hydraulic channels** (LF+RF, each solenoid + motor pack + piston, two
downstream pressure sensors) **+ an electric motor-pack rear drum — the rear
was NEVER hydraulic**. Manual's own words: "similar to an ABS VI modulator
body with taller solenoids" (the only ABS VI mention in the manual). Build
mental model: a **donor 3-channel ABS-VI pack repurposed to drive the 2 front
channels** — not "decommission the 3rd hydraulic channel" as previously
phrased. DTC corroboration: 051/054 = LF/RF pressure (hydraulic), 152 = rear
(motor). Recorded in redux `harness/ev1_peripherals.yaml` on the
`brake_pressure_modulator` record.

**SOURCING UPDATE 2026-07-20** (sibling sweep round 4, ev1 PR #12): the
donor-core pool for the ABS-VI modulator is the Cavalier/Sunfire/Grand-Am
ABS-VI family; the EV1-specific part is a bespoke 4-sensor/dual-master regen
"skin" on that core. CONFIRMED purchasable cross: solenoid kit 18023464 →
ACDelco 18020566 (this is the taller/larger-solenoid item the plan needs).
Front caliper/rotor/master-cylinder searched, no cross found — remain
candidate/bespoke-machined. Also: the EV1 has NO vacuum booster/pump/hydroboost
anywhere — assist is electro-hydraulic regen-blended via the motorized
modulator (aluminum front calipers, 245 mm discs).

**Open unknowns:** brake pressure sensors (M/C port fit); rear actuator motor
spec (drives the fabricated housing); donor drum choice (sets rear hub/backing
size + the £500 aluminum-drum commission).

Related: [[ev1-replica-selective-fidelity]] · [[ev1-replica-sourcing-plan]] ·
[[ev1-replica-part-candidates]]
