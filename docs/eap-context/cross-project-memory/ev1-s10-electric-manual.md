---
name: ev1-s10-electric-manual
description: The Chevrolet S-10 Electric (1997-98) shared the EV1's Delco/GM Power Electronics drivetrain; its factory service manual is a program-wide comparison source (HV, charging, thermal, diagnostics). Digital scans in hand 2026-07-20 (owner to marker-OCR them); eBay 2-vol paper set also in transit. S-10 extractions land in ev1-manual-redux under hard per-vehicle roots.
metadata:
  type: reference
  modified: 2026-07-20T23:25:55.142Z
---

The **Chevrolet S-10 Electric (S10 EV, 1997-1998)** ran the **EV1 propulsion drivetrain** — same ~Hughes/Delco / GM Power Electronics ("System 110") power electronics, three-phase AC induction motor, Delco 6.6 kW Magne Charge inductive charging, lead-acid→NiMH pack + monitoring/thermal, and an under-hood HV service disconnect. Its factory service manual is therefore a **second independent witness to EV1 HV architecture across charging / HV-bus / thermal / diagnostics** — value well beyond the safety workstream.

**Disconnect note (why it came up):** the S-10 productionized an accessible under-hood loop/bail-style HV service disconnect (cover "GM Power Electronics System 110 — Delco Propulsion Systems") resembling a modern EV manual service disconnect (MSD). The **EV1 does NOT have this** — the EV1 has only the behind-seat turn-pull cabin disconnect + an internal (non-accessible) HV interlock loop (redux-confirmed). Owner ruling 2026-07-20: the replica ADOPTS the S-10-style external disconnect rather than inventing one. Photo evidence: public FB post by Sonny Ottinger, 5 Dec 2017.

**Manual acquisition (2026-07-20):** owner PURCHASED a set on eBay — item 406763817041, "1997 Chevrolet S-10 Electric Truck Service Manual Volumes 1 & 2 Shop Manuals", US $75, seller One Lane Bridge (Oneonta AL), GM-published, item-specific pub year 1996 (title 1997). **IN TRANSIT** (seller away until ~2026-07-25). Caveat: it's a **2-volume** set; a **3-volume** set is documented (WorthPoint sold listing) — may be partial/different edition, verify on arrival. Expected to enter the same digitization pipeline as the EV1 manuals (→ redux-style capture) on arrival.

**Accessibility (2026-07-20 hunt):** the factory FSM genuinely exists and trades rarely used; **nothing is directly downloadable**. Real paths: used market (eBay/AbeBooks standing alerts), GM Heritage Archive request (gmhc@gm.com), Helm Inc special-order (low odds), NAHC/Kettering archives (call-and-ask). Free but specs-only: DOE/INL AVT spec PDFs. The commonly-shared forum "S-10 service manual" scans are **gasoline S-10 only** — no EV/propulsion content; not useful.

**Update (2026-07-20 late evening) — scans in hand + repo decision:** someone sent the owner a **SCAN of the S-10 manuals** — a digital copy is now in hand, which **supersedes the "nothing is directly downloadable" accessibility note** above (the eBay paper set remains in transit as a separate physical copy). Owner will run the scans through the **same `marker` OCR process** the EV1 manuals went through. **Repo decision (owner + coordinator agreed 2026-07-20):** S-10 extractions go **INTO ev1-manual-redux (same repo)** under hard per-vehicle roots — parallel `S10 <manual>/` originals dirs, its own extraction tree, **NO S-10 files in EV1 canonical namespaces**, a **vehicle-boundary lint** making cross-vehicle glob leakage a CI failure (the BL-0051 lesson: structural, not conventional), cross-vehicle data linked only via an **explicit crosswalk file**, and the **figpair figure↔step pairing convention baked in at ingest from day one**. A scaffolding session will be spun up when the owner's marker run is ready.

Links: [[ev1-first-responder-safety]] [[ev1-umbrella-repo]] [[ev1-replica-powertrain-direction]] [[ev1-replica-sourcing-plan]].
