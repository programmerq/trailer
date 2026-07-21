---
name: ev1-frame-datum-capture-geometry-domain
description: EV1 frame/body datum + dimension capture landed as a new geometry/ domain in ev1-manual-redux (PR #47) — the geometry base for the spaceframe reconstruction. Content is in the BODY REPAIR manual, not the chassis manual.
metadata:
  type: project
  modified: 2026-07-18T04:20:02.228Z
---

The owner's top-priority frame-geometry capture (see [[ev1-replica-frame-reconstruction-plan]]) is DONE as of 2026-07-17 — delivered as **redux PR #47** (branch `claude/frame-datum-capture`), ready-for-review, CI green, **merge is the owner's call** (not yet merged).

**Key finding — wrong manual name in the tasking:** the EV1 frame datum/dimension geometry is NOT in the chassis manual (that's suspension/alignment/running-gear only). It's in the **BODY REPAIR manual**, pages `body-137`–`body-164`. Future geometry work cites the body-repair manual.

**New self-contained `geometry/` domain** (mirrors the `parts/` domain layering — outputs the build repo [[ev1-umbrella-repo]] REFERENCES by id, not duplicates):
- `geometry/schema/frame_datum.schema.json` + `material_callout.schema.json` (draft-07, `additionalProperties:false`, shared `source_ref` + `confidence{level,note}` defs; `confidence.level:low` requires a note).
- `geometry/scripts/validate_geometry.py --check`, `geometry/ci/lint.sh`, `make test-geometry` (wired into the `test:` aggregate).
- `geometry/data/*.yaml`: reference_frame, spaceframe_alloys, datum_points_152_155 / _156_157 / _158, linear_dims_160_161 / _159_162_163_164.

**Captured (R4-clean, every value cites `manual: body` + printed_page):**
- **Master XYZ reference frame** (body-149): X "0" plane **1999.00 mm** forward of the master gauge hole (measured rearward); Y cross-car, **+ = right / − = left** of centerline (flip only Y sign to swap hand); Z datum plane **309 mm below the rocker-panel lower-flange edge**. Rules: all dims to center of hole; centerline dims combined-when-equal → divide by two; mechanicals removed; symmetrical unless noted; relational dims are reference-only.
- **4 spaceframe alloys** (body-138, fig PSMBSF67088AA): A356-T6 (castings), C210-T6 + 6063-T6 (extrusions), 5754-HO (sheet stamping).
- **65 XYZ hardpoint datum points** (body-152/155/156/157/158) — ids `datum-b<page>-NN`, all reference `frame-underbody-xyz`.
- **61 point-to-point reference dimensions** (body-159–164, `reference_only: true`) — ids `dim-b<page>-NN`.

**Fidelity:** the numeric charts are un-OCR'd figure JPEGs; every value was read at high zoom AND passed an independent adversarial second-read (paired-fidelity pattern, default-refute). All values confirmed digit-for-digit. `datum-b155-08`'s Z was earlier flagged low-confidence (final digit runs off the scan's right edge) but is now **confidence HIGH, owner-confirmed (Z=904.0 correct)** — the off-edge low-confidence flag was dropped; there is no remaining low-confidence value.

**`datum-b158-10` — SETTLED 2026-07-19, corrected value FINAL and CORROBORATED at Y=430.0. DO NOT RE-LITIGATE.** The datum **keeps the faithful printed `coordinates.y: 227.0` at high confidence** (per R4, printed value stays as captured) AND carries `corrected_value: {y: 430.0}` + `erratum_ref: 2026-07-19-b158-10-y-misprint`. **The earlier 454 guess is SUPERSEDED and wrong — the point is rail-centered at Y=430.0, exactly EVEN with `datum-b158-09` (Y=430) and `datum-b158-12` (Y=430)**, confirmed by the owner (programmerq) stepping through the EV1 #212 ("V212") walkaround video.
- **Corroboration — CORROBORATED (no longer "relayed" / "pending").** The owner authored the authoritative whiteout-style scan correction HIMSELF and committed it directly to redux `main` as commit **28ee1d94** (edited both `originals/EV1 body repair manual/body-158.jpg` and `EV1 body repair manual/_page_158_Figure_0.jpeg`; the hand-drawn "Y=430.0" is cleanly legible). That owner-authored, GitHub-visible commit + the V212 video confirmation is the corroboration the reviewer repeatedly demanded. The erratum `corroboration` field is now CORROBORATED (cites 28ee1d94 + V212); `ruling_source` = programmerq (honest owner attribution, NOT a literal period technician).
- **Geometry errata channel:** `geometry/schema/errata.schema.json` + `geometry/data/errata.yaml` hold the ruling (programmerq, 2026-07-19): Y=227 is a manual misprint, corrected value 430.0. **PR #47 (commit `6e5a031a`) was REBASED onto `main`** so it carries the owner's edited scan. **Redux convention §28** (`SCHEMATIC_READING_CONVENTIONS.md`) documents that the body-158 scan carries a deliberate owner-authored correction — authoritative; cite the erratum, do NOT flag it as a scan/print divergence.
- **Validator GUARD** (commit `feebfc3d`): any datum with a `corrected_value` MUST cite an `erratum_ref` that resolves to a real erratum entry — corrections can't arrive as bare value changes (the cause of the earlier revert loop).
- The **ev1 reconstruction layer** (`programmerq/ev1` `assemblies.yaml` override) uses `y:430`, backed by the auditable redux erratum.
- **DURABLE LESSON (refined) — an AGENT must never mutate a canonical source scan in place; the OWNER may correct his own scans.** `scan_path` must stay a faithful reproduction *when an agent touches it*: the adversarial paired-fidelity re-read substrate re-reads scans as ground truth, so an agent's burned-in overlay breaks independent verification — that agent attempt was correctly blocked/dropped. The OWNER making an authoritative correction to his OWN source scans (as he did via 28ee1d94) is the SANCTIONED path, precisely because it's owner-authored + documented by an erratum + a reading-convention note (§28). Owner-authored scan correction = fine and citable; agent pixel-edits = never.

**Open follow-ups** (backlog `BL-2026-07-17-geometry-domain-hardening-follow-ups`, non-blocking): (1) add the geometry gate to CI `.github/workflows/lint.yaml` — currently local-only like bus/parts, but this is safety-critical build geometry; (2) make `validate_geometry.py` FAIL rather than soft-skip when jsonschema/pyyaml missing; (3) referential-integrity so `datum_point.reference_frame` must resolve to a defined frame id.

**ev1 build-repo OpenSCAD frame-datum viewer (the reconstruction layer):** `programmerq/ev1` PR #1 (draft, branch `claude/frame-datum-scad`) adds a regeneratable OpenSCAD frame-datum viewer under `geometry/`:
- `scripts/gen_frame_scad.py` reads the redux datum YAMLs + `assemblies.yaml`, applies overrides, solves the opening quads, and emits the `.scad` (has `--check`).
- `geometry/assemblies.yaml` is the ev1-owned inference layer: `overrides`, `global_tolerance_mm: 1.0`, and 4 groups.
- `geometry/ev1_frame_datums.scad` is generated + checked in; renders headlessly to a valid STL.

Groups:
- **`chassis`** — 65 faithful datum points, fixed.
- **`subframe`** — inferred best-guess; BOTH subframe chassis anchors (`datum-b158-10` and `datum-b158-12`) are now Y=430, so the subframe is symmetric at Y=±430.
- **`roof`** — **4 ROOF-corner points (no windshield datums)**, posed nearly LEVEL (Z=1200) per owner.
- **`trunk` (body-164)** — 4-corner opening whose SHAPE is solved from point-to-point distances via triangulation; only the group POSE is a best guess; movable + rotatable. Trunk top-left anchored to owner-provided `4632, ±542, 963`.

Each group carries translate/rotate transforms so the owner can manipulate it in OpenSCAD/FreeCAD.

**ev1 viewer (PR #1, commit `948c9dd`):** `assemblies.yaml` override + both subframe anchors set to Y=430 (symmetric ±430); roof group is 4 roof-corner points posed level (Z=1200).

**Layering rule (reinforced):** redux = printed facts only; the ev1 build repo = engineering inference/reconstruction (best-guesses, corrections, assemblies). Matches [[ev1-umbrella-repo]] and the parts-catalog layering.

**Durable lesson — an R4 correction to a legibly-printed value must be recorded as a SIGNED erratum, NOT a bare coordinate change.** The printed value stays as captured, and the correction rides alongside as `corrected_value` + an owner-ratified `erratum_ref` + a validator guard requiring the ref to resolve. A bare coordinate edit is what the automated R4 reviewer rejects, and it caused the `158-10` revert loop. Redux stays R4-strict (record only what's printed); reconstruction inference still lives in the ev1 layer, but corrections to print are now auditable errata rather than silent overrides.

**Operational lesson:** background capture/verify subagents were dropped during a session-idle gap (~3.5h) with NO completion notification — a disk check (file mtimes + `ps` for the capture process) is how you distinguish "still working" from "silently died"; relaunch the dropped ones.
