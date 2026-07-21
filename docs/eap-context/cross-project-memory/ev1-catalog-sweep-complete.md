---
name: ev1-catalog-sweep-complete
description: EV1 replica sibling-part sweep COMPLETE (2026-07-20) — whole 42-list parts catalog now covered by sourcing/parts case files across rounds 3-6 (PRs #10-#14, all ready, awaiting owner merge); coverage tally + the notable confirmed cross-lifts + key architecture determinations.
metadata:
  type: project
---

The owner's whole-catalog sibling-ID mandate ("keep it rolling until we've visited the whole part catalog") is DONE as of 2026-07-20. All 42 redux parts lists (1103 GM P/Ns) are covered by `sourcing/parts/` case files in the programmerq/ev1 repo.

**PRs (all ready-for-review, awaiting owner merge; branches `claude/coverage-ledger` + `claude/sourcing-round3..6`):**
- **#10** — coverage ledger (`sourcing/coverage_ledger.md`+`.yaml`): 42/42 lists visited, 1103/1103 rows classified (160 sourced / 234 cross-list+sibling / 86 size-match / 255 bespoke / 368 hardware-generic).
- **#11** round-3 — propulsion_modules, battery_hv_electronics, suspension_and_wheels, cooling_and_drivemotor, seats_envelope_match, speakers.
- **#12** round-4 — brake_hydraulics, steering_linkage, hvac_system, chassis_control_electronics.
- **#13** round-5 — harness_connectors, battery_thermal, exterior_lighting, wiper_arms_blades.
- **#14** round-6 — door_body_hardware, horns_antenna_accessory, body_panels, chemicals_fluids.

**Final coverage: 42/42 lists — 39 dedicated case files + 3 covered-by-classification (07-02 body_structure, 08-02 underbody_deflectors, 09-06 carpet/rear-trim, all bespoke-27 or commodity-hardware), 0 untouched.**

**Notable CONFIRMED cross-lifts found rounds 3-6 (buy-outright, hard fingerprint):**
- Front wheel hub 7470014 → GM J/N-body FWD, 5x100mm 33-spline (ACDelco 513017 / Timken BR930028K).
- Cooling: temp sensor 12110446; coolant-level switch 25626342 → GM 3.8L H-body (via 19151900); axle nut 22636597 → Saturn SL/HHR.
- HVAC ambient temp sensor 16169194 → ACDelco 15-71823 (Cadillac Northstar / Olds Aurora) + its connector 12126469.
- Front park/turn/marker combo lamp 05978405 (LH)/05978406 (RH) → WHOLE assembly cross to 1993-2002 Firebird/Trans Am (4th-gen F-body). Rear reflector 05976021 → N-body; license lamp 00897192 → 49-vehicle GM commodity.
- Wiper blades 22154584/22155686 → GM 19120758 (22.24-in hook).
- Hood primary latch 16630592 → GM 15757371 (authoritative EV1 # is 16630592; 15757371 is the sibling lead). Outside-handle clip 16609280 → GM bin. Body push-in retainer 20664092 + bumper energy-absorber retainer 21077131 → universal GM.
- Brake ABS-VI solenoid kit 18023464 → ACDelco 18020566; cover kit 18020567 shared; modulator donor-core = Cavalier/Sunfire/Grand-Am ABS-VI pool.
- Chemicals (10-02): all 24 rows commodity GM/AC-Delco rebadges (washer solvent 01051515, R-134a 12345922→12356150, DOT3 12377967→19353126).

**Key architecture determinations (candidate/bespoke, honest caveats):**
- EV1 brakes are BOOSTERLESS electro-hydraulic regen-blended (NO vacuum booster, NO vacuum pump, NO hydroboost); aluminum front calipers on 245mm discs.
- EV1 front chassis is BESPOKE aluminum control-arms + aluminum knuckles, NOT J-body strut — so tie-rod ends do NOT lift from J-body; inner tie rod is the strongest Saab-9000 candidate (threads the confirmed Saab rack).
- EBTCM 16231961 + throttle-by-wire pedal 25140664 = bespoke EV1 variants (pedal is a same-Delphi-block sibling of the 1997 C5 Corvette pedal — only other GM DBW of the era).
- Propulsion modules (PIM/APM/PSCM/charger) → Chevy S-10 Electric power-electronics family (S-10 carried the EV1 drivetrain; candidate given 85kW detune). Magne-Charge J1773 receptacle 16234312 marked SUPERSEDED by the NACS-first charging decision (2026-07-20).
- Battery HV contactor 10490007 → Kilovac Czonka/EV200-class sealed HV DC contactor (candidate; 320V break ≈ ~312V pack). Battery ventilation blower 27003192 = EV1-unique assembly, source commodity 12V squirrel-cage motor guts (closes the cooling-fan gap). Real per-module pack-temp sensor = 10489830 x6 (12182027/28 are cooling-AIR inlet-temp + airflow).
- Harness connectors: all ~33 across 5 ELPD lists = commodity Packard/Delphi (GM never made its own connectors); junction blocks 27003328/29 the only bespoke exception.
- Seats = SIZE-MATCH only (redux has no dims → measure gate): Miata NA/NB #1, then light fixed-back/repro bucket, MR2, CRX/del Sol, Fiero, Saturn SC. Antenna = fixed mast (confirmed by absence of any motor/relay). No spoiler part exists in the catalog.

Parallel_Search MCP (keyed) stayed live/decisive across rounds 3-6 P/N resolution. Method + earlier rounds: [[ev1-sibling-id-methodology]] [[ev1-sibling-id-scaled-cases]] [[ev1-sourcing-sweep-round1]] [[ev1-lamps-hvac-siblings]] [[ev1-replica-part-candidates]] [[ev1-replica-sourcing-plan]] [[ev1-no-numeric-lesson-ids]].
