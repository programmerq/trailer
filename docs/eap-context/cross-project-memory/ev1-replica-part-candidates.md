---
type: project
title: EV1 replica — identified part candidates + open unknowns
created: 2026-07-16
tags: [ev1-replica, sourcing, candidates, donors]
---

# EV1 replica part candidates

Running list of identified sourcing candidates and open unknowns (2026-07-16).
Confidence: **high** = P/N/owner-in-hand · **med** = sibling inference · **open**.

## Identified (with donor/sibling + P/N)

(Hood latch and radio moved to the authoritative section below — PR#42 gives
their EV1 numbers; their old sibling leads are retained there as fit-confirm hints.)

- **Steering rack** — closest sibling = the **POWERED rack from a 1985–1998
  Saab 9000**. The EV1 "EV212" rack carries a **Saab P/N**. `med`.
- **Steering column** — GM **J-body ("J-car")** column (inferred: its switches
  + clockspring are J-car units). Donors: Cavalier / Sunfire era. `med`.
- **Combo (turn-signal) + wiper switch** — GM J-car, specifically the **1996
  Chevrolet Cavalier**; owner **has these in hand** (~80% of switchgear). `high`.
- **Clockspring + turn-signal cancel tab** — J-car unit. `med`.
- **Wheels** — magnesium 14x4.5 originals (redux CHWT 4-18 `09592081`); any
  wheel works functionally, could **commission replica wheels**. `med`.
- **Front wheel bolt pattern = 5×100 mm** — **5-lug is MANUAL-PRINTED** in redux
  (`parts/lists/04-18_hubs_wheels_tires.yaml`: NUT-WHL qty 20 + STUD-WHL qty 20
  over WHEEL-14X4.5 qty 4 = 5 studs/nuts per corner, `@source:redux`); **100 mm
  PCD is confirmed-by-interchange** off the redux-printed front hub **7470014**
  ("HUB KIT-FRT WHL", 04-18 item #010) via ACDelco/Timken 513017K = SKF
  BR930028K = BCA WE60701 (bearing spec: 5 studs / 100 mm BCD / 33-spline /
  M12×1.5 studs). Fitment DBs mislabel the car "Saturn EV1"; the bearing spec is
  the independent witness. **J-body 4×100 is EXCLUDED** as the wheel donor
  pattern — the 4×100-vs-5×100 J/N split is Cobalt-era; in the EV1's 1989–2004
  GM compact-FWD bin J/L/N/X all share 5×100, so it's over-determined. Cross-lane
  **scale anchor** (reconciled with the corpus session, PR #11 commit 8beef82 +
  PR #16): the large photo-measurable rulers are tire OD P175/65R14 ≈ 583 mm and
  the 14-in rim, with count-visible-lugs = 5 as the cross-check. `high`.
- **Seats** — magnesium-alloy frame; substitute-OK (aftermarket racing seat OR
  OEM-similar '90s feel). `med`.
- **Rear aluminum drums** — UK shop, custom pair **~£500 GBP** given donor size.
- **Front brakes** — standard disc/caliper/pad (redux CHBK 4-6 P/Ns).

## Resolved via redux capture (authoritative EV1 P/Ns, PR #42, 2026-07-16)

These formerly-open unknowns now have their **authoritative EV1 GM part number**
from the captured Master Indexes catalog. `@source:redux` — the EV1 P/N, not a
sibling/donor hint. Confidence **high** (authoritative EV1 number).

- **Parking-brake switch** — SWITCH ASM-PARK BRK — EV1 P/N **27000760** —
  @source:redux `parts/lists/09-12_instrument_panel_cluster_console.yaml` item 040
  (printed_page 9-12). `high`.
- **Trunk / rear-compartment latch** — LOCK ASM-R/CMPT LID (rear compartment lid
  lock assembly) — EV1 P/N **27002932** — @source:redux
  `parts/lists/08-26_rear_fascia_rear_compartment_lid.yaml` item 006
  (printed_page 8-26). `high`.
- **Hood hinge** — HINGE ASM-HOOD — EV1 P/N **27001358** (RH) / **27001359** (LH)
  — @source:redux `parts/lists/08-06_hood_front_turn_lamps_fascia.yaml` item 039
  (printed_page 8-6). `high`.
- **Steering wheel** — WHEEL-STRG — EV1 P/N **16759727** — @source:redux
  `parts/lists/09-08_seats_belts_steering_wheel_sir.yaml` item 013
  (printed_page 9-8). `high`. (Bespoke magnesium; substitute/replica still the
  physical plan, but the authoritative EV1 number is now on record.)

Also now on record (already had a sibling lead, EV1 number now authoritative):

- **Hood latch** — LATCH ASM-HOOD PRIM — EV1 P/N **16630592** — @source:redux
  `parts/lists/08-06_hood_front_turn_lamps_fascia.yaml` item 011 (printed_page
  8-6). Supersedes the sibling lead (gmpartsdirect 15757371) as the authoritative
  EV1 number.
- **Radio / head unit** — RADIO ASM-AM/FM STEREO & CLK & T/PLYR & CD/PLYR — EV1
  P/N **16234339** (reman; core # 27002417) — @source:redux
  `parts/lists/09-12_instrument_panel_cluster_console.yaml` item 004 (printed_page
  9-12). Confirmed by the sourcing session's authoritative catalog (2026-07-20).
  The earlier Delco/Intrigue sibling lead **9376173** is superseded and does not
  appear in the final catalog. Authoritative source: `sourcing/coverage_ledger.md`
  in the ev1 repo.

## Open unknowns (not yet identified)

- **Brake pressure sensors** — must fit the modified pedal-feel-emulator M/C ports.
- **Rear motor actuator motor spec** — trickiest; anchors to patent US5366281;
  drives a fabricated custom housing.

**Framing:** EV1 parts routinely carry a different P/N than their sibling but
hint at the part family — the sibling P/N is a sourcing lead, not an equivalence;
confirm fit before purchase.

Related: [[ev1-replica-sourcing-plan]] · [[ev1-replica-brake-build-plan]] ·
[[ev1-replica-selective-fidelity]] · [[ev1-catalog-sweep-complete]] ·
[[ev1-harness-sourcing-result]]

## Module packaging siblings (owner facts, 2026-07-19)

- **BTCM = 1995 Firebird EBTCM** — owner-CONFIRMED by his own sleuthing: same shape, size, AND connector part numbers. The anchor precedent for module-packaging sibling hunts.
- **Owner's model:** only THREE modules have sibling packaging — BTCM (done), SDM ("visually looks similar to several others of this era" — follow-on case), TJB (active hunt, dispatched 2026-07-19 to the sibling-parts session using the connector-set fingerprint from electricsim #309's resolved TJB part numbers). LHJB/RHJB have NO siblings — dimensions to be inferred from other references.
- Goal: candidate same-casing modules pin down TJB housing dimensions (owner has none).

Related: [[ev1-sibling-id-methodology]].

## HV EHPS pump — call overturned (2026-07-19)

UPDATED 2026-07-19 (sibling session, Parallel-keyed search): the earlier 'HV EHPS pump has NO auto-sibling, general-market spec-match only' call is OVERTURNED — there is a live product category of 200–450V 3-phase external-inverter EHPS pump assemblies matching the EV1 PSCM architecture. Concrete candidates: NANFENG EHPS-1010R1.5/89C (1.5 kW, 237 VAC 3-phase PMSM, IP67, published 0.8 Ω stator winding inside the manual's 0.8–2.0 Ω phase-to-phase spec — first candidate touching the EV1 number; ~1 kW sibling unit also listed); Freightliner MT50e as architecture existence-proof (bare 3-phase motor + pump + external VFD, one inverter shared with A/C compressor, mirrors the EV1 shared-inverter arrangement). Connector is a 6-pin re-termination (3-phase + HVIL + ground), not a redesign. NF Group / Brogen sell HV A/C compressors alongside (one-vendor option for both HV motors); Guchen stays the compressor specialist. Being recorded in ev1 PR #6. Also: the keyed Parallel search connector is now LIVE in the sibling session and was the difference-maker (pulled OEM spec tables in excerpts).

SWEEP COMPLETE 2026-07-20: the owner-directed full-catalog sibling sweep finished — all 42 redux parts lists / 1103 P/Ns covered via sourcing/parts/ case files in the ev1 repo (PRs #10-#14; #10 = the coverage ledger, now the AUTHORITATIVE sourcing record superseding this memory's per-part details). Final classification: 160 sourced / 234 cross-list candidates / 86 size-match / 255 bespoke / 368 commodity hardware. For any part-sourcing question, consult sourcing/coverage_ledger.md + case files in programmerq/ev1, not this memory.
