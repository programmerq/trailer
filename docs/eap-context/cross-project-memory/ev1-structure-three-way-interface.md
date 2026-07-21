---
name: ev1-structure-three-way-interface
description: Locked three-way interface contract between the EV1 body decomposition, mass-properties ledger, and load-path solver (confirmed 2026-07-21 ~04:55Z by all three sessions)
metadata:
  type: project
  modified: 2026-07-21T04:57:55.014Z
---

# EV1 structure three-way interface contract (LOCKED)

Locked 2026-07-21 ~04:55Z; all three sessions confirmed.

## Scope

Body component decomposition (ev1 PR #17, `frame/geometry/components.yaml`) ↔ mass-properties ledger (`mass_properties/masses.yaml`, PR #24 merged) ↔ load-path solver (PR #22, `loadpath/`), all on the redux frame-underbody-xyz datum.

## Single-authority split — each fact has one home

- **Decomposition** owns geometry/section/alloy/join-method per component + `mass_ref: <component_id>` (pointer only).
- **Ledger** owns mass+CG: one `subsystem: structure` row per component id with `mass_kg` + `cg_station {x_mm, y_mm, z_mm}`, `basis: estimated` `@inferred` from geometry×alloy density citing `@source:redux spaceframe_alloys.yaml`, plus a `geometry_ref` back-pointer. Generator sums structure rows → frame total. Coarse `spaceframe_bare` 132 kg row retired, but 132 kg kept as reconciliation cross-check.
- **Loadpath** owns joint classes/rates and consumes `cg_station` point-masses keyed by component id.

Non-structural masses (pack/drive/interior) live in the ledger with `attaches_to_component` + their own `cg_station`; spanning masses name a primary node + a span flag.

## Join key

Component id `frame.<zone>.<member>[_side]` — lowercase dotted, e.g. `frame.tunnel.spine`, `frame.rocker.lh` — minted by the decomposition session and used character-for-character in all three homes. No mapping tables.

## Loadpath model schema for decomposition stubs

- **Nodes** keyed to redux datum ids (`datum-bNNN-N`) where one exists, else `inf-<component>-<slug>` with inline x/y/z + ref provenance; a shared node id = the joint.
- **Members**: from/to/section/material (+joint).
- **Materials** reference loadpath `data/alloys.yaml` ids (A356-T6 / 6063-T6 / 5754-O / C210-T6-proxy) — decomposition never re-authors materials.
- **Sections**: `A_mm2` / `Iy_mm4` / `Iz_mm4` / `J_mm4` by id; unknown extrusions = named placeholder sections from the AA6063 multi-cell analog at `confidence: low`.
- **Joints** tagged by PHYSICAL method (`weld_bond` default for AVT seams / `spot_weld` / `self_pierce_rivet` / `adhesive` / `bolt`) — loadpath maps method→class+rates.

Loadpath's committed gap-closures: bolted+solvable joint classes, cg_station masses, confidence/provenance passthrough, datum-ref auto-resolution.

## Related

- [[ev1-spaceframe-avt-reframing-and-dossier]]
- [[ev1-replica-frame-reconstruction-plan]]
- [[redux-markdown-quality-program]]
