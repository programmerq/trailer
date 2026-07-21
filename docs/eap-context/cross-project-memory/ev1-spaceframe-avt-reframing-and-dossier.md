---
name: ev1-spaceframe-avt-reframing-and-dossier
description: The EV1 body is a weld-bonded SHEET-aluminum body (Alcan AVT), not a pure extruded-node spaceframe; a cited frame-manufacturing dossier + redux geometry captures now exist. Includes the C210-T6 open designation question.
metadata:
  type: project
---

# EV1 spaceframe: AVT reframing + the frame dossier (2026-07-20 ultracode session)

**Load-bearing finding (source-grounded — WardsAuto 1996, European Aluminium Association AAM body-structures PDF, GM SAE papers):** the EV1/Impact body is **NOT a pure extruded/cast-node spaceframe** like the Audi A8 ASF or Prowler. It is a **~132 kg WELD-BONDED SHEET-aluminum body** built with **Alcan's Aluminum Vehicle Technology (AVT)** — productionized successor to Alcan/Gaydon ASVT: stamped pretreated **5754** sheet panels joined by structural **adhesive + resistance spot welds + rivets**, with **6063 and C210 extrusions** and **A356 castings** integrated to simplify assembly. GM's own manual calls it a "spaceframe" but loosely. This matches and reinforces [[ev1-replica-frame-reconstruction-plan]] ("largely formed sheet panels, learn sheet-metal CAD") and REDUCES the extrusion-internal-geometry risk surface — extrusions are integrated members, not the whole frame.

**Keystone joining references (verified, citable):** SAE 960165 (Sheasby et al., Alcan — weld-bonding bondline robustness; the single best reference for the actual EV1 joint), SAE 870146 (Wheeler/Sheasby/Kewley — foundational ASVT method), 870149, 900796. Repair manual corroborates heat-cure epoxy + chromium conversion pretreatment. Richest transferable manufacturing analog: **Lotus Elise bonded extrusions** (Kochan 1996 — 2 mm wall, 0.2 mm bond gap, hybrid bond+rivet, galvanic mitigation).

**Deliverables produced this session (6 workstreams, 4 PRs):**
- **programmerq/ev1 PR #17** (ready): `assembly/` Lansing Craft Centre build-sequence reconstruction + `frame/references/` DOSSIER (13-cluster cited library, 62 verified sources; extrusion_internal_geometry_problem.md; galvanic_corrosion_and_dissimilar_metal.md 5-class per-joint checklist). Forward-refs geometry/references/sources.yaml (visual-corpus PR #21).
- **programmerq/ev1 PR #18** (ready): `body/` per-panel composite manufacturing matrix — panels are **SMC + RRIM thermosets** (body-53), NOT vacuum-formable (owner's vacuforming anecdote kept low-confidence); per-panel modern routes.
- **ev1-manual-redux PR #58** (draft, owner-paced): geometry/ spaceframe alloy legend from body-138 (figure PSMBSF66893AA — corrected from OCR'd PSMBSF67088AA; shade-coded by fabrication type, NO per-zone map).
- **ev1-manual-redux PR #59** (draft, owner-paced): geometry/ battery pack + tunnel — manuals yield only ~4 in-plan numbers (stem inner width 303 mm, diagonal 977 mm, run 2198 mm); the deliverable is an 11-item gap list with the physical measurement that closes each.

**What the manuals + literature CANNOT give (needs physical measurement + the standing paid outside-engineering FEA/coupon plan):** internal extrusion geometry (no section views/wall thickness/chamber count exist anywhere — the hardest gap, confirmed; best analog = AA6063-T6 ~2.0 mm multi-cell sections, Kohar 2016 / Zhumagulov 2017, as an FEA starting hypothesis only); factory joint counts / per-seam bond-vs-rivet-vs-weld assignment; exact adhesive cure schedule; published allowables for these alloys; pack-tunnel 3D geometry.

**OPEN RESEARCH QUESTION:** **"C210-T6" is not a resolved standard Aluminum Association designation** — likely an Alcan proprietary alloy. Worth chasing (Alcan/Novelis literature, the AVT SAE papers).

Related: [[ev1-umbrella-repo]] · [[ev1-replica-frame-reconstruction-plan]] · [[ev1-replica-lamps-and-panels-plan]] · [[ev1-replica-part-candidates]].
