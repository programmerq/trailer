---
type: project
title: EV1 replica sourcing catalog — the procurement/BOM standing workstream
created: 2026-07-16
tags: [ev1-replica, sourcing, bom, provenance, redux]
---

# EV1 replica sourcing plan

The org decision for how the physical-replica build tracks part procurement.

- **Artifact:** a structured, provenance-tagged **YAML sourcing catalog**, one
  record per part (`sourcing/parts/<id>.yaml`), keyed to **redux part IDs**.
  Each record: authoritative EV1 P/N (`@source:redux` only) + a list of
  inferred `candidates` (substitute/donor/sibling, cost, status, confidence).
- **Lives in a build-program repo, NOT in redux core.** UPDATE 2026-07-17:
  that repo now EXISTS — **github.com/programmerq/ev1** (private, blank as
  of 2026-07-17), the umbrella repo for the whole physical replica program;
  the sourcing catalog belongs there. See [[ev1-umbrella-repo]].
  redux's prime directive
  **R4 forbids inference** — record only what the manual prints. Candidates,
  donors, "closest sibling", cost, and status are all engineering inference, so
  they must sit in a downstream *referencing* layer. Sourcing may cite a redux
  row; redux never learns about candidates. A redux error found while sourcing
  flows back via `notes/manual_errata.yaml`, not by editing sourcing.
- **git-repo-folder over a wiki:** provenance is machine-auditable (assert every
  EV1 P/N is `@source:redux`, every candidate carries an inference tag), every
  change is a reviewable diff, per-file records merge cleanly across branches.
- **Generator** emits (1) the readable catalog and (2) later a **BOM**
  (purchasing sheet — chosen candidate per part, P/N, cost, qty), each with a
  `--check` mode. Optional third view: a **fidelity ledger** tying it to the
  selective-fidelity requirement.
- **Ties:** redux (authoritative P/Ns) → sourcing (candidates) → compliance →
  fidelity ledger. This is the standing **procurement/BOM workstream**.

**Grounding fact:** redux's authoritative reference is the **EV1 Master Indexes
Parts & Illustration Catalog** (`ev1-manual-redux/parts/lists/*.yaml`, 868 GM
P/Ns, keyed `(section_code, printed_page, item)`, `model_codes: X07`). **All
~42 logical catalog lists are now captured (1103 GM P/Ns) in
`parts/lists/*.yaml` via PR #42 (2026-07-16)**, read high-zoom off the page
scans under R4 and adversarially fidelity-verified; the interior/HMI/
body-electrical null cells are now filled. **An EV1 part often carries a different P/N than its
sibling** — the EV1 number is authoritative; the sibling P/N is a family hint,
not an equivalence. **But not always:** some EV1 P/Ns are shared VERBATIM
with other GM vehicles (2026-07-17 — front turn-signal lamps, rear
reflectors), so the catalog needs a **"direct-lift" sourcing class**
alongside sibling-hint and bespoke (see
[[ev1-replica-lamps-and-panels-plan]]).

Related: [[ev1-replica-selective-fidelity]] · [[ev1-replica-brake-build-plan]] ·
[[ev1-replica-part-candidates]] · [[ev1-replica-definition-of-done]]

In-session draft (2026-07-16): scratchpad `ev1_sourcing_catalog.md` +
`ev1_sourcing_schema.md`.
