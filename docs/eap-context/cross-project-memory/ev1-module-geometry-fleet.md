---
name: ev1-module-geometry-fleet
description: First-cut @inferred enclosure geometry for the whole EV1 module fleet (PR #3), by the TJB method — connector footprints + the elec-524 scaled figure + sibling casings. Feeds the buildability scorecard's dimensions column.
metadata:
  type: project
---

Owner-directed 2026-07-18 scale-up of the TJB geometry method to every EV1 module ("inject size/shape inferences for the rest of the modules"). Shipped as **PR programmerq/ev1#3** (branch claude/module-geometry, off main after PR #2 merged), ready-for-review. Records live at `geometry/modules/<mod>.yaml` (one merge-clean file each) + `geometry/modules/README.md` (fleet table + method legend + shared caveats). All geometry `@inferred 2026-07-18`, connector-footprint-anchored, PCBs deliberately UNDERSIZED (grow-only). Connector/service PNs `@source:redux`.

**Method per module class:** (A) scaled connector-location figure elec-524 (PSMELC68272AA) → junction blocks; (B) footprint-stack from connector-face pages → ECUs; (sibling-casing) → matched to a donor archetype; (needs-photo) → no usable figure.

**Fleet envelopes (L×W×H mm, all @inferred):**
- LHJB (P/N 27003329) ~87×68×40, PCB ~65×46 — method A, med/low conf, 11 conns all-4-edges.
- RHJB (P/N 27003328) ~105×66×40, PCB ~83×44 — method A, carries SIR enable/disable.
- TJB (P/N 27003330) ~89×72×32, PCB ~67×52 — method A (record on main, sourcing/parts/tjb.yaml).
- RSA ~55×68×33 — footprint (conn 12045575, W anchored/depth weak), low-med.
- HTCM ~112×85×34 — footprint (main 12045575 + aux 12047946), low.
- BPM ~150×72×40 — footprint, low (potted HV in-pack, real casting likely larger).
- BTCM ~145×105×40 — footprint, low-med. Sibling 1995 F-body EBTCM = LEAD ONLY (no published dims, no PN match; 12110113 is a generic 32-way GM controller connector) — NOT dimensionally confirmed. Tempers the owner's headline BTCM=EBTCM precedent.
- SDM ~100×90×30 — sibling-casing (mid-90s GM SIR cast-Al ~4in-sq×~1.2in box; P/N 16218119 not tied to a specific dimensioned donor), class-confidence.
- IPC / PIM / APM = NEEDS-PHOTO (IPC is a dash cluster not a connector-box; PIM/APM are bespoke HV power modules with no printed HV connector PNs). Demoted.

**KEY CORRECTION:** the "I/P J-block" (27003328) is actually the RHJB. Three-hand junction-block family: LHJB 27003329 (LH) / RHJB 27003328 (RH, "BODY & I/P WRG HARN JUNC") / TJB 27003330. Confirmed via the SIR enable/disable "View A" inset on elec-524-RH. The merged TJB case file was corrected (commit 265f545).

**Buildability scorecard integration:** a parallel session owns the electricsim buildability scorecard and added a geometry/dimensions column fed from THIS data via `notes/module_geometry.yaml` (they commit it; ev1 geometry/modules is the richer source). Agreed 5-state enum: measured | sibling-confirmed | inferred-envelope | none, + separate `sibling_status` (lead-unconfirmed | archetype-class | measured) + `confidence` (low|med|high). GATE: inferred-envelope-WITH-real-mm / measured / sibling-confirmed = geometry-present (pass); `none` = BLOCKER/demote. Owner's framing (verbatim, relayed): a low-quality inferred range still PASSES — "we can reference those actual connector part numbers to infer a range… as we find better information/references, we can adjust!" Result over the original 10: 8 present (all 7 inferred-envelope + SDM soft via confidence), 3 demote (ipc/pim/apm). BTCM stays inferred-envelope (footprint), NOT demoted — EBTCM is the annotation, not the basis. **FLEET TOTAL (13 modules): 8 present / 5 demote (ipc, pim, apm, pscm, ad)** — pscm+ad added after they were run (see below).

**Shared caveat:** elec-524 has NO scale bar → junction-block absolute mm rest on the Micro-Pack 100 footprint; unresolved 0.135-vs-0.21 mm/px anchor tension (~×1.55 span); a physical-fit check argues true JB sizes sit at the UPPER end of the ranges. Height is the weakest axis fleet-wide. Real caliper/ruler-in-frame measurement upgrades any of these instantly.

**Redux-drift routed to coordinator:** BPM J1 drawing_ref PSMELC67244AA looks mis-attached (catalog: 12129025→PSMELC68238AA); BPM J2 retainer "LT GRAY" is a transcription error for CLEAR. The 12045575/PSMELC67247AA "possible copy error" was RESOLVED as genuine GM housing reuse (RSA J1 + HTCM main), not an error.

**PSCM + AD — RAN 2026-07-18** (coordinator-requested, commit 4f0fbfd on PR #3): both status NONE / needs-photo — every connector PN is UNPRINTED so there's no footprint to anchor, and the only views are unscaled isometric exploded illustrations. PSCM = Power Steering Control Module (BLDC pump inverter, svc 16233051); AD = in-pack HV Auto-Disconnect contactor pack (svc 10490007, not a PCB box). geometry/modules/pscm.yaml + ad.yaml.

Related: [[ev1-sibling-id-scaled-cases]] [[ev1-sibling-id-methodology]] [[ev1-umbrella-repo]] [[ev1-frame-datum-capture-geometry-domain]].
---
