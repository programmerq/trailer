---
name: redux-s10-ingest-tooling
description: ev1-manual-redux S-10 Electric manual-ingest tooling — landed as draft PR #65 (branch claude/s10-ingest-tooling). Marker runbook, reserved per-vehicle S-10 roots + crosswalk, structural vehicle-boundary lint, figpair-at-ingest. Parked on owner's prefix-scheme decision.
metadata:
  type: project
  modified: 2026-07-21T00:23:19.753Z
---

S-10 Electric manual-ingest tooling for [[ev1-s10-electric-manual]] landed as **draft PR #65** in ev1-manual-redux (branch `claude/s10-ingest-tooling`, head `1d909c1b` as of 2026-07-21). Delivers the per-vehicle scaffolding decided 2026-07-20 (S-10 in-repo under hard per-vehicle roots, no S-10 in EV1 namespaces, crosswalk-only cross-vehicle links, figpair-at-ingest).

**What landed:**
- `docs/s10_ingest_runbook.md` — owner-facing marker runbook. Marker moved VikParuchuri→`datalab-to/marker`, latest is a ~2.x rewrite: `pip install marker-pdf` (Py3.10+); `marker_single <f.pdf> --output_dir out/ --output_format markdown --force_ocr` (one file) / `marker <folder> ... --skip_existing` (folder). `--force_ocr` is essential for scans (selective-OCR default trusts a garbage embedded text layer). `marker_chunk_convert`/`NUM_DEVICES` GONE → `VLLM_GPUS`/`--num_chunks`. Output = `out/<stem>/<stem>.md` + `<stem>_meta.json` + flat `_page_*` images; owner re-verifies version + image-filename pattern at run time.
- `scripts/vehicle_roots.yaml` + `scripts/check_vehicle_boundary.py` (`--check`, wired into `harness/ci/lint.sh`) — structural vehicle-boundary lint. Four checks: (a) every vehicle-shaped dir declared to one vehicle; (b) prefixes disjoint + regex-safe; (c) cross-vehicle page/figpair citation guard (crosswalk-exempt); (d) generator-scope existence tripwire.
- Reserved roots `S10/README.md`, `originals/S10/README.md`; crosswalk scaffold `crosswalk/README.md` + `crosswalk/ev1_s10.yaml` (empty `links: []`).
- `SCHEMATIC_READING_CONVENTIONS.md` §29 multi-vehicle addendum (page ids scoped to (vehicle,manual); S-10 NEVER an EV1 @source; crosswalk-only). `apply_ocr_corrections.py` narrowed `originals/*/`→`originals/EV1 */`.

**Key gotcha — page-marker prefix regex.** `gen_page_markers.py` MARKER_RE requires the page-marker prefix to be `[a-z]+` (pure lowercase, NO digits), so a literal `s10elec` prefix WILL NOT PARSE. Adopted scheme is `s`+EV1-stem: `selec, sprop, sbatt, sbrakes, shvac, sbody, schassis, ssir, sbus, sparts` — PROVISIONAL until the real S-10 volume set is seen. Alternative (literal `s10<domain>`) needs a one-line MARKER_RE widening `[a-z]+`→`[a-z0-9]+` (backward-compatible, all EV1 prefixes are alpha) — deliberately NOT made. **This is the owner-decision the PR is parked on (draft).**

**Boundary check (d) caveat (documented in-repo).** Check (d) re-derives its own filesystem walk mimicking the two vehicle-agnostic globs (Makefile depth-2 `*.md`; the OCR-corrections `originals/*/`) and fails closed if non-EV1 content appears under those shapes — it does NOT read the real generators, so scoping a generator does not by itself clear it; clearing at first real S-10 ingest needs a paired edit to `check_vehicle_boundary.py`. Deferrals: `BL-2026-07-20-makefile-vehicle-scope` (Makefile depth-2 glob not narrowed — legitimately sweeps non-EV1 dirs), `BL-2026-07-21-boundary-yaml-provenance-guard` (check (c) only scans manual-root .md; a cross-vehicle ref inside a harness/*.yaml @source string is not caught — future companion guard).

Gates all green at `1d909c1b` (lint.sh "All checks passed", make test-harness, dtc-catalog --check). gen_page_markers.py takes S-10 via explicit `_mc(...)`/`MANUALS` entries per manual (key/prefix/manual_md=`S10 <manual>/S10 <manual>.md`/scan_dir=`originals/S10 <manual>/`/disable_figref/offby2 decided after seeing scans) — document-only, no entries added (no manuals yet). See [[redux-page-marker-tooling]].
