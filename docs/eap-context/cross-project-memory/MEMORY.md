# Team Memory Index

## Owner working preferences

- [manual-answerable-findings-source-and-implement] Owner rule: manual-answerable findings get sourced + implemented, not escalated.
- [[owner-wants-panel-to-one-up-his-ideas]] — brainstorm/debate panels should independently generate + one-up bold ideas; owner withholds his own as a genuine independent test.

## EV1 replica program (owner's physical build)

- [[ev1-replica-definition-of-done]] — goal RESOLVED 2026-07-16: road-legal from-scratch driver, aluminum spaceframe + composite, NC Special Construction title, ~$100k floor, multi-year solo-plus-network.
- [[ev1-replica-selective-fidelity]] — SELECTIVE fidelity: modern powertrain/battery OK; EV1 brakes (electronic rear + e-park in one drum) and interior/HMI must be faithful.
- [[ev1-replica-regulatory-and-geometry-reframe]] — NC titling NOT a blocker, museum scans NOT a gate; real critical path is geometry reconstruction + CAD level-up.
- [[ev1-replica-insurance-path]] — insurance largely de-risked 2026-07-16: Hagerty specialty coverage fits the garage-queen/~5k-mi/yr, work-from-home use profile; not a blocker.
- [[ev1-replica-powertrain-direction]] — modern hardware (Nissan Leaf drive unit, LiFePO4 pack) with faithful EV1 electronics via a signal/behavior emulation contract; core stays faithful, improvements are opt-in + ledgered.
- [[ev1-owner-local-repos]] — owner's local-only EV1 repos (2026-07-16): ev1-rsa (interior HMI reconstruction) + ev1-binectract (Tech2/Tech1 scan-tool RE for gated manuals); brake-corner work already in motion.
- [[ev1-replica-sourcing-plan]] — procurement/BOM standing workstream: provenance-tagged YAML sourcing catalog keyed to redux part IDs, in a build-program repo (NOT redux core, R4), git-folder over wiki; generator emits catalog + BOM.
- [[ev1-replica-brake-build-plan]] — full FAITHFUL brake plan: std front disc; ABS-VI pack with 3rd channel decommissioned + larger solenoids; modified M/C as pedal-feel emulator; donor-sized rear drums (£500 UK aluminum option); bespoke rear dual-motor actuator (patent US5366281).
- [[ev1-umbrella-repo]] — github.com/programmerq/ev1 (created 2026-07-17, blank private) is THE umbrella repo for the replica program (sourcing catalog, CAD, frame reconstruction, compliance dossier, decision log, panel reports); spaceframe recreation is the most critical workstream; parts strategy: siblings for most, bespoke headlamp/stoplamp, Fiero-style composite panels. Operating decisions 2026-07-17: FreeCAD + sheet-metal workbench (not Fusion 360), no Git LFS (external storage + hashed manifests for raw scan data), private with outputs-shareable content, first wave = sourcing catalog + panel reports + decision log, scaffold direct-to-main then PRs.
- [[ev1-replica-frame-reconstruction-plan]] — frame reconstruction is THE most critical part per owner (2026-07-17): recreate the bonded/welded aluminum spaceframe from chassis-manual datums + alloy info + references (#212 videos, GM Heritage assembly-line video, spaceframe photos, hoped-for 3D scans); owner to learn sheet-metal CAD and pay for outside engineering verification; datum coordinates get captured in redux (R4), build repo references them. Priority 2026-07-17: chassis-manual dimension/datum extraction (redux, R4-clean) comes FIRST, before the visual-reference corpus index; routed to the redux queue via the coordinator.
- [[ev1-replica-lamps-and-panels-plan]] — lamps + body panels strategy (2026-07-17): direct-lift shared-P/N parts (front turn signals, rear reflectors) bought outright; bespoke headlamp/stoplamp scratch-built around harvested GM bulb/reflector bowls with replica-technique clear lenses; body panels Fiero-style reinforced composite, mostly vacuformable (years out).
- [[ev1-replica-part-candidates]] — identified candidates (hood latch 15757371, Saab 9000 rack sibling, Intrigue radio 9376173, J-car column/switchgear, magnesium wheels/seats) + open unknowns (parking-brake switch, hood hinge, trunk latch, brake pressure sensors, rear actuator motor spec).
- [[ev1-a2l-refrigerant-safety-case]] — R-1234yf is safe-if-designed-for-A2L (ev1 PR #35, bff6eef); design-for-A2L-from-day-one checklist (POE oil, BLDC condenser fan, hermetic terminals, refrigerant identifier) constrains future A/C sourcing.
