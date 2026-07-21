---
name: ev1-suspension-geometry-capture
description: EV1 suspension specs captured into the redux geometry/ domain (PR #60) — dimensioned alignment/torque/vehicle geometry from the chassis manual PLUS a structured "measurement-needed" backlog of the undimensioned part geometry (the build-to-spec gaps).
metadata:
  type: project
---

EV1 suspension geometry deep pass, delivered 2026-07-20 as redux **PR #60** (branch `claude/suspension-geometry-capture`, ready-for-review, owner-paced merge). Extends the `geometry/` domain (see [[ev1-frame-datum-capture-geometry-domain]]).

**Coverage verdict (the chassis manual is a SERVICE manual, not an engineering-dimension manual):**
- DIMENSIONED + captured: front+rear **alignment tables** (camber/caster/toe + tol + First→Seventh adjustment order, chassis-61); **29 pivot/joint torque specs** (front+rear, incl. torque-at-curb-height rule for links); **vehicle/ride-height geometry** (wheelbase 2512, track F1470/R1244, OAL 4309/W1766/H1281, curb 1350kg, 175/65R14 @345kPa, steering 16.38:1; trim F166.4/R173.0 nom + measuring pts A650/B550); **hub runout/end-play**; **19 components** (architecture + joint types + P/N cross-refs to parts/lists).
- ABSENT (pictorial-only) → captured as a structured **`geometry/data/measurement_backlog.yaml`** (12 `measurement_needed` records: component, missing dims, `enables`, `how_to_close` by donor measurement, priority). This is the build-to-spec deliverable — the owner's fallback is CV/sway-bar/axle shops that build to spec, so the SPEC (donor or not) is the goal. **3 HIGH gaps:** front CV halfshaft (LH/RH lengths, inner tri-pot + outer Rzeppa spline counts, boot ODs, plunge travel), front sway bar (diameter, bend, arm/end-link length, mount X), rear beam axle (tube OD/wall, length, pivot/bushing bores + locations). MED (9): control-arm pivot-to-pivot + bushing bores, ball-joint tapers, tie-rod thread/length, bespoke cradle mount+pivot XYZ, rear 5-link eye-to-eye, rear spring rate/free-length, rear damper eye-to-eye/stroke, rear wheel PCD.

**Architecture (printed facts):** FRONT = SLA double-wishbone (upper+lower control arms, aluminum steering knuckle with factory-attached ball joints, coil-over shock, rack-and-pinion 16.38:1); halfshaft inner = **tri-pot**, outer = **Rzeppa**; front hub **7470014 = confirmed J/N-body 5×100 cross** (don't re-derive). REAR = aluminum **beam axle** (tubular center + end castings, non-driven), 2 coil springs + 2 twin-tube shocks + 5 links (2 upper leading, 2 lower trailing, 1 track/Panhard bar), non-replaceable link bushings, rear hub 07470525 w/ wheel-speed sensor. **Confirmed NEGATIVES (recorded as facts): no rear anti-roll bar** (roll control via track bar), **no rear halfshafts** (non-driven).

**New `geometry/schema/suspension.schema.json` record types:** suspension_alignment, alignment_adjustment_order, vehicle_dimension, suspension_torque, hub_spec, suspension_component, measurement_needed — wired into validate_geometry.py + geometry/ci/lint.sh.

**Fidelity:** all numeric values (alignment, vehicle dims, torque) passed an independent adversarial paired-fidelity re-read — **42/42 confirmed, 0 divergent**. R4 honesty note: the front-camber/cross-camber tolerance prints a genuine **double-minus typo** (`±-0.80`/`±-0.70`) in the actual manual — captured verbatim with a review_notes explanation, not fabricated/smoothed.

**Audit premise corrections (durable):** datum-b158-10/-12 are FRONT/mid-rail points (X≈1250–1999), NOT rear-suspension anchors; the body-repair 65-datum set is unlabeled body-side gage holes (no named suspension pivots) — some may coincide with pickups by location only. The only XYZ hardpoints in redux are those gage holes; no suspension pivot coordinates are printed anywhere.
